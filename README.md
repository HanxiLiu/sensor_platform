# sensor_platform

This is firmware of sensor_platform designing for wbk of Karlsruhe Institute of Technology written by Liu Hanxi .

All included third party libraries are now listed below and I sincerely appreciate all the work of them

BMP280       https://github.com/adafruit/Adafruit_BMP280_Library

DHT11        https://github.com/adafruit/DHT-sensor-library

ArduinoJson  https://github.com/bblanchon/ArduinoJson

PubSubClient https://github.com/knolleary/pubsubclient

Ticker       https://github.com/esp8266/Arduino/tree/master/libraries/Ticker

NTP          https://github.com/arduino-libraries/NTPClient

esp8266core  https://github.com/esp8266/Arduino

To realize some cofiguration functions of the sensor platform the bmp280 and dht11 libraries are slightly modified.
It includes SPI pin setup, SPI and I2C connection checking and single wire transmission pin setup.

Before using the firmware file please substitute the original src files in libraries with those modified files.
