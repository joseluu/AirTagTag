#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <U8g2lib.h>
#include <unordered_map>
#include <map>

// OLED display for WiFi Kit (SSD1306 on I2C pins 4 and 5)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ 16, /* clock=*/ 15, /* data=*/ 4);

BLEScan* pBLEScan;
AsyncWebServer server(80);

// Store detected AirTags and counts
std::unordered_map<std::string, int> airtagCounts; // To store AirTag addresses and detection counts
std::unordered_map<std::string, int> airtagRSSI;   // To store AirTag RSSI values
std::unordered_map<std::string, String> airtagTrend; // To store AirTag trends (increasing, decreasing, stable)
std::unordered_map<std::string, String> airtagDetails; // To store additional AirTag details
bool isScanning = false; // Flag to indicate scanning state
unsigned long lastDisplayUpdate = 0; // For updating OLED periodically
const unsigned long DISPLAY_UPDATE_INTERVAL = 1000; // Update display every second

// Function to estimate distance based on RSSI
float calculateDistance(int rssi) {
  int txPower = -59; // Typical value for 1 meter
  if (rssi == 0) {
    return -1.0; // If we cannot determine distance
  }
  float ratio = rssi * 1.0 / txPower;
  if (ratio < 1.0) {
    return pow(ratio, 10);
  } else {
    return (0.89976) * pow(ratio, 7.7095) + 0.111;
  }
}

// Function to update OLED display with BLE devices summary
void updateOLEDDisplay() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  
  int deviceCount = airtagCounts.size();
  
  // Display header
  u8g2.drawStr(0, 10, "BLE Devices");
  
  if (deviceCount == 0) {
    u8g2.drawStr(0, 25, "Scanning...");
    u8g2.drawStr(0, 40, "No devices found");
  } else {
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "Found: %d device(s)", deviceCount);
    u8g2.drawStr(0, 25, buffer);
    
    // Display first 3 devices with RSSI
    int displayCount = 0;
    int yPos = 40;
    for (const auto& entry : airtagCounts) {
      if (displayCount >= 3) break;
      
      int rssi = airtagRSSI[entry.first];
      String addr = String(entry.first.c_str());
      addr = addr.substring(addr.length() - 5); // Show last 5 chars of MAC
      
      snprintf(buffer, sizeof(buffer), "%s: %d dBm", addr.c_str(), rssi);
      u8g2.drawStr(0, yPos, buffer);
      yPos += 12;
      displayCount++;
    }
  }
  
  u8g2.sendBuffer();
}

// Custom callback to handle detected devices
class CustomAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    std::string address = advertisedDevice.getAddress().toString();
    String details = "";

    // Check for AirTag using manufacturer data
    if (advertisedDevice.haveManufacturerData()) {
      std::string manufacturerData = advertisedDevice.getManufacturerData();
      if (manufacturerData.size() > 2 && manufacturerData[0] == 0x4C && manufacturerData[1] == 0x00) {
        int rssi = advertisedDevice.getRSSI();
        float distance = calculateDistance(rssi);

        // Update trend based on previous RSSI
        if (airtagRSSI.find(address) != airtagRSSI.end()) {
          if (rssi > airtagRSSI[address]) {
            airtagTrend[address] = "red"; // Increasing
          } else if (rssi < airtagRSSI[address]) {
            airtagTrend[address] = "blue"; // Decreasing
          } else {
            airtagTrend[address] = "green"; // Stable
          }
        } else {
          airtagTrend[address] = "green"; // Default to stable for new devices
        }

        // Update RSSI and counts
        airtagRSSI[address] = rssi;
        if (airtagCounts.find(address) == airtagCounts.end()) {
          airtagCounts[address] = 1;
        } else {
          airtagCounts[address]++;
        }

        // Construct details
        details += "RSSI: " + String(rssi) + " | Distance: " + String(distance) + " meters";
        if (advertisedDevice.haveName()) {
          details += " | Name: " + String(advertisedDevice.getName().c_str());
        }
        if (advertisedDevice.haveServiceUUID()) {
          details += " | Service UUID: " + String(advertisedDevice.getServiceUUID().toString().c_str());
        }
        airtagDetails[address] = details;

        // Print AirTag details
        Serial.printf("AirTag Detected - Address: %s | %s | Count: %d | Trend: %s\n", 
                      address.c_str(), details.c_str(), airtagCounts[address], airtagTrend[address].c_str());
      }
    }
  }
};

void setup() {
  Serial.begin(115200);
  Serial.println("Starting BLE Scanner with OLED Display...");
  
  // Initialize OLED display
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 15, "Initializing...");
  u8g2.sendBuffer();
  
  BLEDevice::init("ESP32_BLE_Scanner");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new CustomAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true); // Active scan for more data
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(50);

  // Initialize WiFi for Captive Portal
  WiFi.softAP("ESP32-BLE-Portal");
  WiFi.softAPConfig(IPAddress(192, 168, 11, 29), IPAddress(192, 168, 11, 29), IPAddress(255, 255, 255, 0));
  delay(100);

  // Display IP on OLED
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 15, "WiFi AP Started");
  u8g2.drawStr(0, 30, "IP: 192.168.11.29");
  u8g2.sendBuffer();
  
  // Open captive portal automatically
  Serial.printf("AP IP Address: http://192.168.11.29\n");

  // Define Captive Portal Content
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = "<html><head><title>BLE Devices</title></head><body><h1>BLE Devices</h1>";
    html += "<h2>AirTags Detected:</h2><ul>";
    for (const auto& entry : airtagCounts) {
      String trendColor = airtagTrend[entry.first].c_str();
      html += "<li><span style='color:" + trendColor + "'>";
      html += "Address: " + String(entry.first.c_str()) + " | Count: " + String(entry.second);
      html += " | " + airtagDetails[entry.first];
      html += "</span></li>";
    }
    html += "</ul><button onclick=\"location.href='/clear'\">Clear</button></body></html>";
    request->send(200, "text/html", html);
  });

  server.on("/clear", HTTP_GET, [](AsyncWebServerRequest *request) {
    airtagCounts.clear();
    airtagRSSI.clear();
    airtagTrend.clear();
    airtagDetails.clear();
    request->redirect("/");
  });

  server.begin();

  // Start continuous scanning
  isScanning = true;
  xTaskCreatePinnedToCore(
    [] (void* pvParameters) {
      while (true) {
        pBLEScan->start(5, false);
        delay(100);
      }
    },
    "BLEScanTask",
    4096,
    nullptr,
    1,
    nullptr,
    0
  );
}

void loop() {
  // Update OLED display periodically
  unsigned long currentTime = millis();
  if (currentTime - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    updateOLEDDisplay();
    lastDisplayUpdate = currentTime;
  }
}