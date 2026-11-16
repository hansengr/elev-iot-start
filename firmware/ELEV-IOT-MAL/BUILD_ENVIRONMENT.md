# ELEV-IOT-MAL – Build Environment

## Hardware

- Board: ESP8266 med 0.96" OLED (skjermen sitter på GPIO12 = klokke og GPIO14 = data)
- Board variant: Generic ESP8266 Module
- CPU Frequency: 160 MHz anbefales, https går da omtrent dobbelt så fort
- Board manager URL: http://arduino.esp8266.com/stable/package_esp8266com_index.json

## Sensorer

| Sensor | Pinne på sensoren | ESP8266 |
|---|---|---|
| MPU-6050 (GY-521) | VCC | 3V3 |
| | GND | GND |
| | SCL | D1 (GPIO5) |
| | SDA | D2 (GPIO4) |
| DHT11/DHT22 (valgfri, `USE_DHT 1`) | DATA | D4 (GPIO2), samme som på ECPC-IOT |
| | VCC / GND | 3V3 / GND |

## Arduino

- Arduino IDE: 2.3.7
- Board Support Package: esp8266 by ESP8266 Community, 3.1.2

## Libraries

- ArduinoJson: 7.4.3
- U8g2: 2.36
- MPU-6050: ingen, leses direkte med Wire (`mpu6050.h`)
- Bare med `USE_DHT 1`: DHT sensor library 1.4.7 og Adafruit Unified Sensor 1.1.15

## Filer

- `secrets.h`: WiFi og nøkkelen til boksen. Lages fra `secrets.example.h`, deles aldri.
- `mpu6050.h`: leser sensoren.

## Build

Kompilert 2026-11-13 med arduino-cli (Generic ESP8266), både `USE_DHT 0` og `USE_DHT 1`.

Hardware upload/test: ikke gjort ennå.
