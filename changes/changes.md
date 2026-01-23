## 1. context
The module is an esp32 wifi dev kit with an oled module, implement oled initialisation and display of important information
## 2. improvements to display
Use font u8g2_font_6x10_tf
Remove the first 2 lines of display
for each device detected, add the rssi and the distance in meters with 1 decimal, as the last character of the line make it a + if distance is narrowing, a - is distance is lengthening, no character if distance does not changeread
## 3. connect to wifi router
use credentials from the included file network_creds.h in variables ssid and password to connect the a wifi router
log trace messages on serial so that success or failure is reported on serial.     
In case of failure, report it on the oled display, wait 5 seconds and restart the microcontroller
## 4. advertise service with mDns
Do not force ip address to 192.168.11.29 but use address given by dhcp when connecting to the router to advertise it via mDNS
Force web browser to refresh every 15s
