# AirTagTag - Cat Home Presence Tracker

**Project Repurposing**: This project has been repurposed to track when cats are at home and when they have left home using Apple AirTag detection technology.

## Overview

AirTagTag is an ESP32-based BLE scanner that detects Apple AirTags attached to cats and displays their presence status on both an OLED display and a web interface. The system tracks signal strength to estimate distance and alert you when your cats leave home (signal lost for 5+ minutes).

## Key Features

### Multi-Cat Support
- Configure multiple cats in `airtag_customisation.h` with their AirTag MAC addresses
- Display customizable names for OLED (short) and web page (full)
- Individual timeout settings for each cat

### Real-Time Presence Detection
- Continuous BLE scanning for configured AirTags
- Displays cat status on OLED and web page
- Automatic detection when cats arrive/leave home
- 5-minute timeout before marking a cat as "lost"

### OLED Display (128x64 SSD1306)
- **Line 1**: Primary cat information (RSSI, distance, trend)
- **Line 2**: Signal reacquisition time or loss time
- **Line 3+**: Secondary cats and other detected BLE devices
- Font: u8g2_font_6x10_tf for clear readability
- Automatic backlight support for TTGO devices (GPIO 14)

### Signal Trend Tracking
- Red indicator (+): Signal strength increasing (cat getting closer)
- Green indicator ( ): Signal strength stable
- Blue indicator (-): Signal strength decreasing (cat moving away)

### Distance Estimation
- Calculates approximate distance in meters based on RSSI
- Displayed with 1 decimal precision (e.g., 2.5 m)

### Time Tracking
- NTP time synchronization (daily)
- Configurable timezone (CET by default)
- Displays when cats arrive/leave home with timestamps
- Automatic DST (daylight saving time) support

### WiFi Router Integration
- Connects to home WiFi using credentials from `network_creds.h`
- DHCP-assigned IP address
- mDNS service advertised as `mysato.local` (configurable per first cat)
- Error handling with OLED and serial logging

### Web Interface
- Auto-refreshing web page (15-second intervals)
- Displays all configured cats with detailed information
- Color-coded trend indicators
- Clear button to reset data
- Accessible via: http://mysato.local

### Device Information
- Displays RSSI (signal strength in dBm)
- Shows distance in meters
- Indicates signal trend
- Service UUIDs and device names when available

## Hardware Requirements

- ESP32 development board (tested on ESP32-WROOM-32)
- SSD1306 OLED display (128x64) connected via I2C
- Apple AirTags or compatible BLE devices
- Optional: TTGO WiFi Kit for integrated display (backlight auto-enabled)

## Setup Instructions

### 1. Hardware Setup
- Connect OLED display to ESP32:
  - SDA → GPIO 4
  - SCL → GPIO 15
  - GND → GND
  - VCC → 3.3V

### 2. Configuration
- Edit `network_creds.h`:
  ```cpp
  const char* ssid = "your_wifi_name";
  const char* password = "your_wifi_password";
  ```

- Edit `include/airtag_customisation.h`:
  ```cpp
  const std::vector<CatConfig> CATS = {
    {
      .macAddress = "d6:e8:3b:00:e1:4f",
      .displayName = "Sato",
      .webName = "Satoshi",
      .timeoutMs = 300000
    }
    // Add more cats as needed
  };
  ```

- Configure timezone (if not in France/CET):
  ```cpp
  const long GMT_OFFSET_SEC = 3600;      // UTC+1
  const int DAYLIGHT_OFFSET_SEC = 3600;  // DST offset
  ```

### 3. Building & Flashing
```bash
pio run -e esp32dev     # Build for ESP32 Dev Kit
pio run -e esp32dev -t upload  # Flash to device
```

### 4. Usage
1. Power on the ESP32
2. Device connects to WiFi automatically
3. OLED displays initialization progress
4. Once ready, configured cats will appear as they come into range
5. Access web interface at: http://mysato.local
6. Monitor serial output for detailed logs: 115200 baud

## Advanced Features

### Tracking Behavior
- **Initial Detection**: Shows "in: HH:MM:SS" (time when first detected)
- **Signal Lost**: Shows "out: HH:MM:SS" (time when signal last seen)
- **Reacquisition**: Updates "in: HH:MM:SS" when signal is detected again
- **Timeout**: Cat marked as "lost" after 5 minutes without signal

### Serial Logging
All events logged to serial console:
- WiFi connection status
- NTP time synchronization
- Cat signal acquisition/loss events
- BLE device detection details

### Adding More Cats
Simply add entries to the `CATS` vector in `airtag_customisation.h`:
```cpp
{
  .macAddress = "aa:bb:cc:dd:ee:ff",
  .displayName = "Milo",
  .webName = "Milo",
  .timeoutMs = 300000
}
```

## Technical Details

### BLE Scanning
- Active scan mode for detailed device information
- Interval: 100ms, Window: 50ms
- Detects Apple manufacturer data (0x4C) for AirTag identification

### Time Synchronization
- Uses pool.ntp.org for time sync
- Syncs automatically at startup and daily during operation
- Prevents time drift with explicit daily sync

### Web Server
- ESPAsyncWebServer running on port 80
- Auto-refresh every 15 seconds
- Responsive design with color-coded indicators
- HTML list view of all detected devices

## Dependencies

```
ESPAsyncWebServer @ 3.9.5
AsyncTCP @ 3.4.10
U8g2 @ 2.36.17
ESP32 BLE Arduino @ 2.0.0
ESPmDNS @ 2.0.0
WiFi @ 2.0.0
```

## Troubleshooting

### WiFi Connection Fails
- Check SSID and password in `network_creds.h`
- Verify 2.4GHz band (5GHz not supported on ESP32)
- Check router signal strength and range

### OLED Display Not Showing
- Verify I2C connections (SDA/SCL pins)
- Check power supply (3.3V)
- Try reset button on ESP32

### AirTags Not Detected
- Verify MAC address in `airtag_customisation.h`
- Ensure AirTag is in detection mode
- Check BLE is enabled on AirTag
- Monitor serial output for detection logs

### Time Not Syncing
- Verify internet connectivity
- Check timezone settings in `airtag_customisation.h`
- Monitor serial for NTP sync messages

----------  initial project readme ----------

# AirTagTag
ESP32 AirTag Detector with Captive Portal - Apple AirTag detection system using an ESP32. Bluetooth Low Energy (BLE) scans / detects AirTags and other BLE devices, CAPTIVE PORTAL hosted by the ESP32. Dynamic updates, trends based on RSSI values device names and distances.

Key Features:

Real-Time BLE Scanning: Continuously scans for BLE devices, specifically identifying AirTags.
AirTag Detection Counter: Maintains a count of each detected AirTag, updating only when the device is within range.
RSSI Trends: Displays RSSI trends with color coding:
    Red: Signal strength is increasing.
    Green: Signal strength is stable.
    Blue: Signal strength is decreasing.
Device Distance Estimation: Calculates the approximate distance of the AirTag based on RSSI.
Captive Portal:
    Lists all detected BLE devices with details.
    Displays AirTag detection counts and trends at the top.
    Accessible through the ESP32's WiFi hotspot.
Clear and Reset: A button to clear all data and start fresh.
Automatic Captive Portal: Opens automatically for easy access to device data.
Instructions:

Hardware Requirements:
    ESP32 development board.
    BLE-enabled AirTags or other BLE devices for testing.
Setup:
    Flash the provided firmware to your ESP32 using PlatformIO or Arduino IDE.
    Power the ESP32 and connect to the WiFi hotspot ESP32-BLE-Portal.
Access the Portal:
    Open a browser and navigate to http://192.168.4.1 (default IP address).
View AirTag Details:
    Check the count, RSSI trends, and other details of detected AirTags.
Clear Data:
    Use the "Clear" button to reset the detection counts and trends.
Enhancements:

Dynamic Trend Updates: Tracks RSSI trends for proximity estimation.
Device Details: Displays additional information like service UUIDs and names.
Responsive Captive Portal: Dynamically updates device data without restarting.
How It Works:

BLE Scanning:
    Runs continuously in a separate task.
    Detects devices with Apple manufacturer data (0x4C) to identify AirTags.
Captive Portal:
    Uses ESPAsyncWebServer to serve data over a local network.
    Displays data dynamically with trends and counts.
RSSI and Distance:
    RSSI values are used to estimate distance and calculate trends.
Future Improvements:

Add historical graphs for RSSI trends.
Enable JSON API for external integrations.
Optimize memory management for larger device counts.
Dependencies:

ESPAsyncWebServer
AsyncTCP
ESP32 BLE Arduino
Usage Scenarios:

Track AirTag proximity and presence in a given area.
Debugging and testing BLE devices.
Educational projects to explore BLE technology.
