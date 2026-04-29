# CrowPanel HMI Irrigation System

A three-device IoT irrigation system using UART communication with a 7" touch HMI display and ESP RainMaker cloud integration.

## Architecture

```
┌─────────────────────┐     UART (3m wire)   ┌─────────────────────┐      UART        ┌──────────────────┐
│  ESP32-C3 DevKitM-1 │ ──────────────────── │  ESP32 Wroom        │ ──────────────── │                  │
│  (Sensor Node)      │  TX=GPIO6 → RX=GP14  │  (Relay Controller) │ TX=GP17 → RX=48  │   CrowPanel 7"   │
│  - Water meter      │  every 2 seconds     │  - 16 Relays (MCP)  │ every 2 seconds  │   ESP32-P4 HMI   │
│  - DS18B20 temp     │                      │  - SHT20 temp/hum   │                  │   1024x600 Touch  │
│  - Rain sensor      │                      │  - SSD1306 OLED     │                  │   MIPI DSI        │
└─────────────────────┘                      │  - Physical buttons  │                  │                  │
                                             │  - WiFi → RainMaker │                  │  - Dashboard     │
                                             └─────────────────────┘                  │  - Relay Control │
                                                                                      │  - Schedules     │
                                                                                      │  - Actions/Alarms│
                                                                                      │  - Settings      │
                                                                                      └──────────────────┘
```

**Data flow:** Sensor Node → Wroom → CrowPanel (all via UART)  
**Cloud:** Wroom → WiFi → ESP RainMaker (relays, sensors, alarms)

## Projects

| Folder | Device | Framework |
|--------|--------|-----------|
| `sensor_node/` | ESP32-C3 DevKitM-1 | Arduino, UART output |
| `relay_controller/` | ESP32 Wroom | Arduino + RainMaker |
| `crowpanel_hmi/` | CrowPanel 7" ESP32-P4 | Arduino + LVGL 8 + ESP32_Display_Panel |
| `common/` | Shared headers | Protocol definitions |

## Communication

All communication uses a framed UART protocol: `[0xAA][CMD][LEN][PAYLOAD...][XOR_CHECKSUM][0x55]`

### Sensor Node → Wroom (UART1)
- **Baud:** 115200, 8N1
- **Pins:** C3 TX=GPIO6 → Wroom RX=GPIO14
- Sends `CMD_SENSOR_DATA` (0x09) every 2 seconds: flow rate, total liters, water temp, rain status

### Wroom → CrowPanel (UART2)
- **Baud:** 115200, 8N1
- **Pins:** Wroom TX=GPIO17 → CrowPanel RX=IO48
- Wroom sends: `CMD_FULL_STATUS` (relays+temp+humidity), `CMD_TIME_SYNC` (NTP time), `CMD_SENSOR_DATA` (forwarded from sensor node)
- CrowPanel sends: `CMD_RELAY_SET` (toggle relays), `CMD_ALARM_UPDATE` (alarm state changes)

### Wroom → ESP RainMaker (WiFi)
- BLE provisioning (service: "PROV_1234", POP: "abcd1234")
- 16 relay switches, room temp/humidity, water flow/temp/rain sensors, alarm devices
- Timezone: Europe/Bucharest

## Building

Each project is a standalone PlatformIO project. Build/upload independently.

```bash
cd sensor_node && pio run --target upload
cd relay_controller && pio run --target upload
cd crowpanel_hmi && pio run --target upload
```

> **Note:** The CrowPanel ESP32-P4 target may require Arduino IDE with ESP32 board manager v3.x if PlatformIO ESP32-P4 support is not yet stable on your system.
