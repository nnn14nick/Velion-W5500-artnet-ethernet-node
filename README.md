# Velion DMX

🌐 **Website:** [velion-dmx.netlify.app](https://velion-dmx.netlify.app/)

A DIY ESP32-based Art-Net to DMX node for controlling DMX lighting fixtures over Ethernet.

## Features

- ESP32-based controller
- W5500 Ethernet connectivity (Art-Net input)
- 2 DMX output universes using MAX485 modules
- OLED display for status information
- WS2812B status LED
- DIY and open-source hardware project

## Hardware

- ESP32 DevKit
- W5500 Ethernet module
- 2× MAX485 RS-485 modules
- I²C OLED display
- WS2812B LED
- XLR output connectors
- Suitable power supply and wiring



## Getting Started

1. Install the [Arduino IDE](https://www.arduino.cc/en/software).
2. Install the ESP32 board support package.
3. Install the libraries required by the firmware. (check below)
4. Open the `.ino` file in Arduino IDE.
5. Select your ESP32 board and the correct USB port.
6. Upload the firmware.
7. Connect the node to your Ethernet network and configure your lighting software to send Art-Net.

## How to install required libraries
In Arduino IDE, open Tools/sketch -> Include Library -> Manage Libraries (the exact menu wording varies by IDE version), then search for these libraries and install them:
1. Ethernet - by Arduino  (connects the ESP32 to the W5500 Ethernet module and provides network)
2. Adafruit GFX Library - by Adafruit  (Provides graphics and text drawing functions for the display)
3. Adafruit SSD1306 - by Adafruit  (controls the OLED display)
4. Adafruit NeoPixel - by Adafruit  (controls the WS2812B led pixel)

The SSD1306 library depends on GFX, and the IDE may automatically install any 

## Compatibility

Designed for use with Art-Net-compatible lighting software, such as:

- Resolume Arena
- TouchDesigner
- QLC+
- ChamSys MagicQ

Compatibility depends on the firmware configuration and network setup.

## Project Status

**Work in progress.**

Hardware and firmware are still being developed and tested. Some features may not work reliably yet.

## License

This project is licensed under the MIT License. See the `LICENSE` file for details.

## Disclaimer

This is a DIY electronics project. Check your wiring, voltage levels, and power supply before powering the device. Use appropriate electrical safety precautions when connecting lighting equipment. If not sure about anything you can ask us on any of our contacts.  
