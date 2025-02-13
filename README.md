# FilaMan - Filament Management System

A comprehensive filament management system combining ESP32-based hardware for weight measurement and NFC tag reading/writing with a web interface for managing filament spools in conjunction with Bambu Lab AMS and Spoolman.

## Project Overview

FilaMan is designed to streamline the management of filament spools for 3D printing. The system consists of an ESP32 microcontroller that handles weight measurement and NFC tag operations, and a web interface that allows users to manage filament spools, monitor AMS (Automatic Material System) status, and interact with Spoolman and Bambu Lab printers.

### ESP32 Hardware Features
- **Weight Measurement:** Using a load cell with HX711 amplifier for precise weight tracking.
- **NFC Tag Reading/Writing:** PN532 module for reading and writing filament data to NFC tags.
- **OLED Display:** Shows current weight, connection status (WiFi, Bambu Lab, Spoolman).
- **WiFi Connectivity:** WiFiManager for easy network configuration.
- **MQTT Integration:** Connects to Bambu Lab printer for AMS control.
- **Data Persistence:** Stores calibration data in EEPROM.
- **Watchdog Timer:** Ensures system stability.

### Web Interface Features
- **Real-time Updates:** WebSocket connection for live data updates.
- **NFC Tag Management:** Write filament data to NFC tags.
- **AMS Integration:** 
  - Display current AMS tray contents.
  - Assign filaments to AMS slots.
  - Support for external spool holder.
- **Spoolman Integration:**
  - List available filament spools.
  - Filter and select filaments.
  - Update spool weights automatically.
  - Track NFC tag assignments.

## Detailed Functionality

### ESP32 Functionality
- **Control and Monitor Print Jobs:** The ESP32 communicates with the Bambu Lab printer to control and monitor print jobs.
- **Printer Communication:** Uses MQTT for real-time communication with the printer.
- **User Interactions:** The OLED display provides immediate feedback on the system status, including weight measurements and connection status.

### Web Interface Functionality
- **User Interactions:** The web interface allows users to interact with the system, select filaments, write NFC tags, and monitor AMS status.
- **UI Elements:** Includes dropdowns for selecting manufacturers and filaments, buttons for writing NFC tags, and real-time status indicators.

## Installation

### Prerequisites
- **Software:**
  - [PlatformIO](https://platformio.org/) in VS Code
  - [Spoolman](https://github.com/Donkie/Spoolman) instance
  - Bambu Lab printer (optional for AMS integration)
- **Hardware:**
  - ESP32 Development Board
  - HX711 Load Cell Amplifier
  - Load Cell (weight sensor)
  - OLED Display (128x64 SSD1306)
  - PN532 NFC Module
  - Connecting wires

### Step-by-Step Installation
1. **Clone the Repository:**
    ```bash
    git clone https://github.com/yourusername/FilaMan.git
    cd FilaMan
    ```
2. **Install Dependencies:**
    ```bash
    pio lib install
    ```
3. **Flash the ESP32:**
    ```bash
    pio run --target upload
    ```
4. **Initial Setup:**
    - Connect to the "FilaMan" WiFi access point.
    - Configure WiFi settings through the captive portal.
    - Access the web interface at `http://filaman.local` or the IP address.

## Hardware Requirements

### Components
- **ESP32 Development Board:** Any ESP32 variant.
- **HX711 Load Cell Amplifier:** For weight measurement.
- **Load Cell:** Weight sensor.
- **OLED Display:** 128x64 SSD1306.
- **PN532 NFC Module:** For NFC tag operations.
- **Connecting Wires:** For connections.

### Pin Configuration
| Component          | ESP32 Pin |
|-------------------|-----------|
| HX711 DOUT        | 16        |
| HX711 SCK         | 17        |
| OLED SDA          | 21        |
| OLED SCL          | 22        |
| PN532 IRQ         | 32        |
| PN532 RESET       | 33        |
| PN532 SCK  	    | 14        |
| PN532 MOSI    	| 13        |
| PN532 MISO       	| 12        |
| PN532 CS/SS       | 15        |

## Software Dependencies

### ESP32 Libraries
- `WiFiManager`: Network configuration
- `ESPAsyncWebServer`: Web server functionality
- `ArduinoJson`: JSON parsing and creation
- `PubSubClient`: MQTT communication
- `Adafruit_PN532`: NFC functionality
- `Adafruit_SSD1306`: OLED display control
- `HX711`: Load cell communication

### External Services
- **Bambu Lab Printer:** For AMS integration.
- **Spoolman:** For filament management.

## API Communication

### Spoolman Integration
The system communicates with Spoolman using its REST API for:
- Fetching spool information.
- Updating spool weights.
- Managing NFC tag assignments.

### Data Format
```json
{
  "version": "2.0",
  "protocol": "openspool",
  "color_hex": "FFFFFF",
  "type": "PLA",
  "min_temp": 200,
  "max_temp": 220,
  "brand": "Vendor",
  "sm_id": "1234"
}
```

## Documentation

### Relevant Links
- [PlatformIO Documentation](https://docs.platformio.org/)
- [Spoolman Documentation](https://github.com/Donkie/Spoolman)
- [Bambu Lab Printer Documentation](https://www.bambulab.com/)

### Tutorials and Examples
- [PlatformIO Getting Started](https://docs.platformio.org/en/latest/tutorials/espressif32/arduino_debugging_unit_testing.html)
- [ESP32 Web Server Tutorial](https://randomnerdtutorials.com/esp32-web-server-arduino-ide/)

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

## Materials

### Useful Resources
- [ESP32 Official Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)
- [Arduino Libraries](https://www.arduino.cc/en/Reference/Libraries)
- [NFC Tag Information](https://learn.adafruit.com/adafruit-pn532-rfid-nfc/overview)

### Community and Support
- [PlatformIO Community](https://community.platformio.org/)
- [Arduino Forum](https://forum.arduino.cc/)
- [ESP32 Forum](https://www.esp32.com/)

## Availability

The code can be tested and the application can be downloaded from the [GitHub repository](https://github.com/yourusername/FilaMan).
