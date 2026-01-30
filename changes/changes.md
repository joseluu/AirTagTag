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
## 5. fix for TTGO devices
TTGO devices have a led backlight signal on pin 14, activate the backlight
## 6. Signal sato
Track time of day, use the ntp protocol to initialize time of day, make sure the internal time of day does not drift by ensuring that ntp is called at least once per day, do it explicitly unless the NTP library does it itself. Reserve the first line of the display as well as the first line of web page to device d6:e8:3b:00:e1:4f, name this device "Sato" on the display and "Satoshi" on the web page. If device is not to be located for 5 minutes, replace rssi and distance by the time at which the signal was lost prefixed by "out: "
## 7. Time issues
The time of day shown is UTC, the device is in France so it should display CET time fix time display. Reserve a second line on the display
and on the web page to display the time when the signal was reacquired prefixed by "in: ". Change mDNS name to be mysato.local
## 8. Display issue
Never use the second line for devices display, if there has not been any reacquisition, leave a blank line.
## 9. Display initial
Upon initial acquisition of the signal, display as if it was reacquired, that is use the second line with prefix "in: "
## 10. Generalize for other cats
Add a airtag_customisation.h file to hold the cat name, short name, cat MAC and local timezone (CET in our case), use these informations to customize the code.
## 11. Update the README.md
The README.md file of the project directory is taken from the initial open source project and is divided into 2 parts
separated by "----------  initial project readme ----------". The second part is the initial readme file of the
original project and it should be kept unchanged. Rewrite the first part of the README.md file to account for
all the features that have been added up to paragraph 10. Mention first that the project has been repurposed to
track when cats are at home and when they left home.
## 12. Change MAC
Change the cat MAC address to E3:ED:26:C7:83:C4
## 13. support legacy trackers
support tracking a legacy tracker identified by LE General Discoverable and BR/EDR Not Supported flags
