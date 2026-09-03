# openhasp-mvp
Minimal Viable Project for openHASP testing

## Tooling and Framework

- ESP IDF 6.1.0
- Board Manager 0.7.2
- LVGL 9.5

## Services and testing

- LVGL display and touch
- WiFi / networking
- HTTP(S) and WS(s)
- MQTT(S)
- LittleFS flash
- SD Card (FAT)
- FTP (optional)

## Tested hardware

- CYD ESP32-8040S043C (@fvanroie)
- CYD ESP32-8040S050C (@fvanroie)
- CYD ESP32-8040S070C (@fvanroie)
- Wireless-Tag WT32-SC01 Plus (@fvanroie)

## Contributing

If you want to add/test a new device, please open an issue with the board title. This will serve as a tasl, follow-up, collaboration and support hub.
When a hardware board is working using the [Espressif Board Manager](https://board-manager.espressif.com/) ([Guide](https://docs.espressif.com/projects/esp-board-manager/en/latest/index.html)), it can be merged via PR.
