#ifndef AIRTAG_CUSTOMISATION_H
#define AIRTAG_CUSTOMISATION_H

#include <vector>
#include <string>

// Timezone configuration
// CET = UTC+1, CEST = UTC+2
const long GMT_OFFSET_SEC = 3600;      // UTC+1 for CET
const int DAYLIGHT_OFFSET_SEC = 3600;   // DST adds another hour

// Cat configuration structure
struct CatConfig {
  std::string macAddress;      // MAC address of the AirTag (lowercase, colon-separated)
  std::string displayName;     // Short name for OLED display (max 8 chars recommended)
  std::string webName;         // Full name for web page
  unsigned long timeoutMs;     // Time in ms before marking as lost (default: 300000 = 5 minutes)
};

// Define cats with their AirTag MAC addresses and names
const std::vector<CatConfig> CATS = {
  {
    .macAddress = "e3:ed:26:c7:83:c4",  // lower case
    .displayName = "Sato",
    .webName = "Satoshi",
    .timeoutMs = 300000
  }
  // Add more cats below:
  // {
  //   .macAddress = "aa:bb:cc:dd:ee:ff",
  //   .displayName = "Milo",
  //   .webName = "Milo",
  //   .timeoutMs = 300000
  // }
};

// Get cat config by MAC address
inline const CatConfig* getCatByMAC(const std::string& macAddress) {
  for (const auto& cat : CATS) {
    if (cat.macAddress == macAddress) {
      return &cat;
    }
  }
  return nullptr;
}

// Get mDNS hostname based on first cat
inline std::string getMDNSHostname() {
  if (!CATS.empty()) {
    std::string hostname = "my" + CATS[0].displayName;
    // Convert to lowercase for mDNS
    for (auto& c : hostname) {
      if (c >= 'A' && c <= 'Z') {
        c = c - 'A' + 'a';
      }
    }
    return hostname;
  }
  return "myairtag";
}

#endif // AIRTAG_CUSTOMISATION_H
