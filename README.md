# OpenEVSE_RAPI_WiFi_ESP8266
RAPI client/Server for ESP8266 WiFi Module

Changelog:

   - ADDED ESP8266 OTA programming support for Arduino IDE
   - Individual read/write functions have been broken out of loop()
   - ADDED gridServerRead() - read the current price (scale of 1-4), and requested charge percentage from OpenShift NodeJS server (http://gridserv-pwrgrid.rhcloud.com/), which follows scheme in the included .jpg
   - ADDED RAPIwrite() sends the current price to the EVSE via a new RAPI command $SP
     -- See OpenEVSE branch for new RAPI command and implementation
   - ADDED several functions for reading Wifi ELM327 CAN bus on Nission LEAF to get Accessory Voltage, SOC, and capacity 
     -- Still several issues. Only reliably reads Accessory voltage due to timing/parsing issues, and CAN bus reader activates some relay on LEAF during every read, which is too much for my liking.
