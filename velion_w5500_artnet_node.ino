#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>

// --- PIN DEFINITIONS ---
#define W5500_CS_PIN    5

#define DMX1_TX_PIN     25
#define DMX1_DERE_PIN   26

#define DMX2_TX_PIN     27
#define DMX2_DERE_PIN   14

#define LED_PIN         4
#define NUM_LEDS        1

#define OLED_SDA        21
#define OLED_SCL        22
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1
#define OLED_ADDRESS    0x3C

// --- ARTNET CONFIGURATION ---
#define ARTNET_PORT     6454
#define UNIVERSE_1      0   // Art-Net Universe for Port 1 (0-indexed)
#define UNIVERSE_2      1   // Art-Net Universe for Port 2 (0-indexed)

// Network Setup
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
EthernetUDP Udp;

// Peripherals
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_NeoPixel pixel(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

HardwareSerial DMXSerial1(1);
HardwareSerial DMXSerial2(2);

// Thread-Safe DMX Memory Buffers
uint8_t dmxBuffer1[512];
uint8_t dmxBuffer2[512];
portMUX_TYPE dmxBufferMux = portMUX_INITIALIZER_UNLOCKED;

// Status & Timers
volatile unsigned long lastPacketUni1 = 0;
volatile unsigned long lastPacketUni2 = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastDMXTx = 0;

volatile uint32_t packetCountUni1 = 0;
volatile uint32_t packetCountUni2 = 0;

// LED State Tracker (Prevents unnecessary pixel.show calls)
uint32_t currentLedColor = 0xFFFFFFFF; 

// Task Handles
TaskHandle_t ArtNetTaskHandle = NULL;

// Function Declarations
void sendDMXFrame(HardwareSerial &serialPort, uint8_t *data, uint16_t len);
void parseArtnetPacket(uint16_t packetSize);
void sendArtPollReply();
void updateDisplay();
void updateStatusLED();
void setLedColor(uint32_t color);

// --- CORE 0: DEDICATED HIGH-PRIORITY ART-NET RECEIVER ---
void artnetTask(void *pvParameters) {
  for (;;) {
    int packetSize = Udp.parsePacket();
    if (packetSize) {
      parseArtnetPacket(packetSize);
    } else {
      vTaskDelay(1 / portTICK_PERIOD_MS); // Yield 1ms to feed Watchdog when idle
    }
  }
}

void setup() {
  Serial.begin(115200);

  // 1. Initialize RS485 Transceiver Enable Pins (Drive HIGH for transmit)
  pinMode(DMX1_DERE_PIN, OUTPUT);
  pinMode(DMX2_DERE_PIN, OUTPUT);
  digitalWrite(DMX1_DERE_PIN, HIGH); 
  digitalWrite(DMX2_DERE_PIN, HIGH);

  // 2. Initialize Status LED
  pixel.begin();
  pixel.setBrightness(40);
  setLedColor(pixel.Color(255, 140, 0)); // Orange = Booting

  // 3. Initialize OLED Display (Fast 400kHz I2C Clock)
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(400000); 

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println(F("OLED init failed!"));
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("Art-Net Node Booting..."));
  display.display();

  // 4. Initialize Hardware Serials (250kbps, 8N2)
  DMXSerial1.begin(250000, SERIAL_8N2, -1, DMX1_TX_PIN);
  DMXSerial2.begin(250000, SERIAL_8N2, -1, DMX2_TX_PIN);

  // 5. Initialize W5500 Ethernet
  Ethernet.init(W5500_CS_PIN);

  IPAddress ip(2, 0, 0, 100);       
  IPAddress subnet(255, 0, 0, 0);
  Ethernet.begin(mac, ip, IPAddress(0,0,0,0), IPAddress(0,0,0,0), subnet);

  Udp.begin(ARTNET_PORT);

  memset(dmxBuffer1, 0, 512);
  memset(dmxBuffer2, 0, 512);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("Node Ready!"));
  display.print(F("IP: "));
  display.println(Ethernet.localIP());
  display.display();
  delay(500);

  // 6. Spawn Dedicated Art-Net Receiver on Core 0
  xTaskCreatePinnedToCore(
    artnetTask,        
    "ArtNetTask",      
    4096,              
    NULL,              
    2,                 
    &ArtNetTaskHandle, 
    0                  
  );
}

// --- CORE 1: CONTINUOUS DMX OUTPUT & DISPLAY LOOP ---
void loop() {
  unsigned long now = millis();

  // 1. Continuous DMX Frame Output Loop (Runs at ~30 Hz / every 33 ms)
  if (now - lastDMXTx >= 33) {
    lastDMXTx = now;

    uint8_t txBuf1[512];
    uint8_t txBuf2[512];

    // Thread-safe memory copy
    portENTER_CRITICAL(&dmxBufferMux);
    memcpy(txBuf1, dmxBuffer1, 512);
    memcpy(txBuf2, dmxBuffer2, 512);
    portEXIT_CRITICAL(&dmxBufferMux);

    // Output DMX frames to Universe 1 and Universe 2
    sendDMXFrame(DMXSerial1, txBuf1, 512);
    sendDMXFrame(DMXSerial2, txBuf2, 512);
  }

  // 2. Asynchronous Display & Status LED Refresh (Every 500 ms)
  if (now - lastDisplayUpdate >= 500) {
    lastDisplayUpdate = now;
    updateDisplay();
    updateStatusLED();
  }
}

void sendDMXFrame(HardwareSerial &serialPort, uint8_t *data, uint16_t len) {
  serialPort.flush();

  // Generate DMX BREAK (100us LOW) and MAB (11us HIGH) via hardware baud rate modulation
  serialPort.updateBaudRate(90000);
  serialPort.write((uint8_t)0x00);
  serialPort.flush();

  // Return to standard DMX baud rate (250,000)
  serialPort.updateBaudRate(250000);

  // DMX Start Code (0x00) + 512 channels
  serialPort.write((uint8_t)0x00);
  serialPort.write(data, len);
}

void parseArtnetPacket(uint16_t packetSize) {
  if (packetSize < 10) return;

  uint8_t packetBuffer[530];
  Udp.read(packetBuffer, packetSize);

  // Verify Header ("Art-Net\0")
  if (memcmp(packetBuffer, "Art-Net\0", 8) != 0) return;

  uint16_t opCode = packetBuffer[8] | (packetBuffer[9] << 8);

  if (opCode == 0x2000) { // ArtPoll
    sendArtPollReply();
  } 
  else if (opCode == 0x5000 && packetSize >= 18) { // ArtDmx
    uint16_t incomingUniverse = packetBuffer[14] | (packetBuffer[15] << 8);
    uint16_t dmxLength = (packetBuffer[16] << 8) | packetBuffer[17];
    if (dmxLength > 512) dmxLength = 512;

    if (incomingUniverse == UNIVERSE_1) {
      portENTER_CRITICAL(&dmxBufferMux);
      memcpy(dmxBuffer1, &packetBuffer[18], dmxLength);
      portEXIT_CRITICAL(&dmxBufferMux);
      lastPacketUni1 = millis();
      packetCountUni1++;
    } 
    else if (incomingUniverse == UNIVERSE_2) {
      portENTER_CRITICAL(&dmxBufferMux);
      memcpy(dmxBuffer2, &packetBuffer[18], dmxLength);
      portEXIT_CRITICAL(&dmxBufferMux);
      lastPacketUni2 = millis();
      packetCountUni2++;
    }
  }
}

void setLedColor(uint32_t color) {
  // Only call pixel.show() when color changes to avoid interrupt conflicts with UART
  if (currentLedColor != color) {
    currentLedColor = color;
    pixel.setPixelColor(0, color);
    pixel.show();
  }
}

void updateStatusLED() {
  unsigned long now = millis();
  bool uni1Active = (now - lastPacketUni1 < 2000);
  bool uni2Active = (now - lastPacketUni2 < 2000);

  if (Ethernet.hardwareStatus() == EthernetNoHardware || Ethernet.linkStatus() == LinkOFF) {
    setLedColor(pixel.Color(255, 0, 0)); // Red = Cable Disconnected
  } else if (uni1Active || uni2Active) {
    setLedColor(pixel.Color(0, 255, 0)); // Green = Receiving Data
  } else {
    setLedColor(pixel.Color(0, 0, 200)); // Blue = Idle / Network Ready
  }
}

void sendArtPollReply() {
  uint8_t reply[239];
  memset(reply, 0, sizeof(reply));

  memcpy(reply, "Art-Net\0", 8);
  reply[8]  = 0x00;
  reply[9]  = 0x21;

  IPAddress myIP = Ethernet.localIP();
  reply[10] = myIP[0];
  reply[11] = myIP[1];
  reply[12] = myIP[2];
  reply[13] = myIP[3];

  reply[14] = 0x36;
  reply[15] = 0x19;
  reply[23] = 0x20;

  strncpy((char*)&reply[26], "ESP32-DMX-Node", 17);
  strncpy((char*)&reply[44], "ESP32 Dual-Universe ArtNet Node", 63);
  strncpy((char*)&reply[108], "#0001 [0000] OK", 63);

  reply[172] = 0x00;
  reply[173] = 0x02;
  reply[174] = 0x80;
  reply[175] = 0x80;
  reply[182] = 0x80;
  reply[183] = 0x80;

  reply[190] = UNIVERSE_1;
  reply[191] = UNIVERSE_2;

  Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
  Udp.write(reply, sizeof(reply));
  Udp.endPacket();
}

void updateDisplay() {
  display.clearDisplay();
  
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.print(F("IP: "));
  display.println(Ethernet.localIP());
  display.drawLine(0, 9, 128, 9, SSD1306_WHITE);

  unsigned long now = millis();
  bool uni1Active = (now - lastPacketUni1 < 2000);
  bool uni2Active = (now - lastPacketUni2 < 2000);

  // Universe 1
  display.setCursor(0, 15);
  display.print(F("U1 (Uni "));
  display.print(UNIVERSE_1);
  display.print(F("): "));
  if (uni1Active) {
    display.print(F("RCV ["));
    display.print((packetCountUni1 / 5) % 10);
    display.print(F("]"));
  } else {
    display.print(F("OFFLINE"));
  }

  // Universe 2
  display.setCursor(0, 32);
  display.print(F("U2 (Uni "));
  display.print(UNIVERSE_2);
  display.print(F("): "));
  if (uni2Active) {
    display.print(F("RCV ["));
    display.print((packetCountUni2 / 5) % 10);
    display.print(F("]"));
  } else {
    display.print(F("OFFLINE"));
  }

  // Status Bar
  display.drawLine(0, 50, 128, 50, SSD1306_WHITE);
  display.setCursor(0, 54);
  if (uni1Active || uni2Active) {
    display.print(F("Art-Net Signal OK"));
  } else {
    display.print(F("Waiting for Art-Net..."));
  }

  display.display();
}
