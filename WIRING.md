# Wiring & Configuration Guide

## System Overview

```
Sensor Node (ESP32-C3)              Wroom (ESP32)                CrowPanel (ESP32-P4)
  GPIO6 TX ──── 3m wire ────── GPIO14 RX (Serial1)
                                GPIO17 TX (Serial2) ────── IO48 RX (Serial1)
                                GPIO16 RX (Serial2) ────── IO47 TX (Serial1)
                                GND ───────────────────── GND
```

All three devices share a common GND. The sensor node is ~3m away from the Wroom.
The Wroom and CrowPanel are in the same enclosure.

---

## 1. ESP32-C3 Sensor Node Wiring

### UART to Wroom
| ESP32-C3 Pin | Wroom Pin | Notes |
|-------------|-----------|-------|
| GPIO 6 (TX) | GPIO 14 (RX) | 3m wire, UART1 |
| GND | GND | Common ground |

> GPIO 7 (RX) is reserved but unused — sensor node only transmits.

### Sensors
| Sensor | ESP32-C3 Pin | Notes |
|--------|-------------|-------|
| Water Flow (pulse) | GPIO 3 | FALLING edge, INPUT_PULLUP, 450 pulses/liter |
| DS18B20 Data | GPIO 4 | Needs 4.7kΩ pull-up to 3.3V |
| Rain Sensor DO | GPIO 5 | Digital output (LOW = raining) |
| Rain Sensor AO | GPIO 0 | Analog output (0-4095) |

### Water Flow Sensor (YF-S201 or similar)
- Red → 5V (can use VIN)
- Black → GND
- Yellow → GPIO 3
- Calibration: 450 pulses per liter (adjust in code if different sensor)

### DS18B20 Waterproof Temperature Probe
- Red → 3.3V
- Black → GND
- Yellow → GPIO 4
- 4.7kΩ resistor between Data and 3.3V

### Rain Sensor Module
- VCC → 3.3V
- GND → GND
- DO (Digital Out) → GPIO 5
- AO (Analog Out) → GPIO 0

---

## 2. ESP32 Wroom Relay Controller Wiring

### UART1 — Sensor Node Input
| Wroom Pin | Sensor Node Pin | Notes |
|-----------|----------------|-------|
| GPIO 14 (RX) | GPIO 6 (TX) | Receives sensor data |
| GPIO 27 (TX) | — | Reserved, unused |

### UART2 — CrowPanel Communication
| Wroom Pin | CrowPanel Pin | Notes |
|-----------|--------------|-------|
| GPIO 17 (TX) | IO48 (RX) | Wroom sends status/sensors/time |
| GPIO 16 (RX) | IO47 (TX) | CrowPanel sends relay commands/alarms |
| GND | GND | Common ground |

### I2C Bus (SDA=GPIO 21, SCL=GPIO 22)
All peripherals share one I2C bus. Use 4.7kΩ pull-ups on SDA and SCL to 3.3V.

| Device | I2C Address | Notes |
|--------|-------------|-------|
| MCP23017 (16 relay GPIOs) | 0x20 | A0=A1=A2=GND, active LOW outputs |
| SSD1306 OLED 128x64 | 0x3C | 0.96" display via U8g2 |
| SHT20 Temp/Humidity | 0x40 | Via SHT2x library |

### Physical Buttons
| Button | Wroom Pin | Function |
|--------|-----------|----------|
| WiFi Reset | GPIO 12 | INPUT_PULLUP, resets WiFi provisioning |
| Select | GPIO 13 | INPUT_PULLUP, cycles through relays on OLED |
| Toggle | GPIO 26 | INPUT_PULLUP, toggles selected relay |
| Factory Reset | GPIO 0 | Hold >10s for full RainMaker factory reset |

### MCP23017 → 16 Relay Module
| MCP23017 Pin | Relay # | MCP23017 Pin | Relay # |
|-------------|---------|-------------|---------|
| GPA0 (pin 0) | 1 | GPB0 (pin 8) | 9 |
| GPA1 (pin 1) | 2 | GPB1 (pin 9) | 10 |
| GPA2 (pin 2) | 3 | GPB2 (pin 10) | 11 |
| GPA3 (pin 3) | 4 | GPB3 (pin 11) | 12 |
| GPA4 (pin 4) | 5 | GPB4 (pin 12) | 13 |
| GPA5 (pin 5) | 6 | GPB5 (pin 13) | 14 |
| GPA6 (pin 6) | 7 | GPB6 (pin 14) | 15 |
| GPA7 (pin 7) | 8 | GPB7 (pin 15) | 16 |

**MCP23017 wiring:**
- VDD → 3.3V
- VSS → GND
- SDA → GPIO 21 (shared I2C)
- SCL → GPIO 22 (shared I2C)
- A0, A1, A2 → GND (address 0x20)
- RESET → 3.3V (or GPIO for reset control)
- Relay modules are active-LOW (MCP output LOW = relay ON)

**SSD1306 OLED wiring:**
- VCC → 3.3V
- GND → GND
- SDA → GPIO 21 (shared I2C)
- SCL → GPIO 22 (shared I2C)

**SHT20 wiring:**
- VIN → 3.3V
- GND → GND
- SDA → GPIO 21 (shared I2C)
- SCL → GPIO 22 (shared I2C)

**Important I2C notes:**
- One pair of 4.7kΩ pull-ups on SDA & SCL is sufficient for the whole bus
- Keep I2C wires short (< 30cm) for reliable 400kHz operation
- Use separate 5V power supply for relay module — don't power from ESP32
- MCP23017 outputs can sink/source 25mA per pin (enough for relay coil drivers)

---

## 3. CrowPanel 7" ESP32-P4 Setup

### Hardware Overview
- **Display:** 1024×600 MIPI DSI with EK79007 LCD controller
- **Touch:** GT911 capacitive touch on I2C
- **MCU:** ESP32-P4 (no native WiFi — uses onboard ESP32-C6 coprocessor)
- **Library:** ESP32_Display_Panel with custom board config

### UART Pins (to Wroom)
| CrowPanel Pin | Wroom Pin | Notes |
|--------------|-----------|-------|
| IO47 (TX) | GPIO 16 (RX) | CrowPanel sends commands |
| IO48 (RX) | GPIO 17 (TX) | CrowPanel receives data |
| GND | GND | Common ground |

### Display & Touch Pins (on-board, no wiring needed)
| Function | Pin | Notes |
|----------|-----|-------|
| Touch I2C SCL | IO46 | GT911, 400kHz |
| Touch I2C SDA | IO45 | GT911 |
| Touch INT | IO42 | Interrupt |
| Touch RST | IO40 | Reset |
| Backlight PWM | IO31 | 30kHz PWM |
| LCD RST | IO41 | EK79007 reset |
| MIPI D-PHY power | LDO3 | 2.5V (configured in software) |
| I2C pull-up power | LDO4 | 3.3V (configured in software) |

### Display Configuration
The display is configured via `esp_panel_board_custom_conf.h` and `board_config.h`
in the `crowpanel_hmi/src/` folder. These files are pre-configured for the
CrowPanel Advanced 7" ESP32-P4. No modifications needed unless using a different revision.

---

## 4. First-Time Setup

### Step 1: Wire the Sensor Node
1. Connect sensors to ESP32-C3 as shown in section 1
2. Run a 3m wire from C3 GPIO6 (TX) to Wroom GPIO14 (RX)
3. Connect GND between the two boards

### Step 2: Wire the CrowPanel
1. Connect Wroom GPIO17 (TX) → CrowPanel IO48 (RX)
2. Connect CrowPanel IO47 (TX) → Wroom GPIO16 (RX)
3. Connect GND between the boards (if not already shared via power supply)

### Step 3: Flash All Devices
```bash
# Flash in any order:
cd sensor_node && pio run --target upload
cd relay_controller && pio run --target upload
cd crowpanel_hmi && pio run --target upload
```

### Step 4: RainMaker Provisioning
1. Install "ESP RainMaker" app on your phone
2. Power on the Wroom — it starts BLE provisioning automatically
3. Scan QR code from Serial output or search for "PROV_1234"
4. Enter POP: `abcd1234`
5. Connect to your WiFi network through the app
6. Relays and sensors appear in the RainMaker dashboard

### Step 5: Power On & Verify
- CrowPanel shows dashboard with "Sensors: LOST" and "Relays: LOST"
- Once sensor node is powered, water data appears within 2 seconds
- Once Wroom is powered, relay/temp/humidity data appears within 2 seconds
- Connection indicators turn green when data is flowing
- Time syncs from Wroom NTP every 30 seconds

---

## 5. Touch UI Guide

### Dashboard Tab
- Shows all sensor values in card format
- Water flow, temperature, rain, room temp, humidity
- Daily/monthly/yearly water volume counters (auto-reset)
- Connection status indicators for sensor node and relay controller

### Relays Tab
- 4×4 grid of 16 relay buttons
- **Tap** a button to toggle the relay ON/OFF
- **Long press** a button to rename it
- Green = ON, Gray = OFF
- Changes are sent immediately to the Wroom via UART

### Schedules Tab
- Per-relay scheduling: set days, start time, duration
- Optional rain sensor bypass (skip watering when raining)
- Repeat mode with configurable interval

### Actions Tab
- Sensor-based automation rules
- Water flow alarm: detects possible pipe leaks (flow > threshold)
- Rain auto-close: automatically activates relay when rain is detected
- Alarms forwarded to RainMaker cloud

### Settings Tab
- Reset daily/monthly/yearly volume counters
- Reset all relay names to defaults

---

## 6. Troubleshooting

| Problem | Check |
|---------|-------|
| "Sensors: LOST" on CrowPanel | Verify Wroom→CrowPanel UART wiring (GPIO17→IO48, IO47→GPIO16) |
| No sensor data on Wroom | Verify C3 GPIO6 → Wroom GPIO14 wire, check Serial1 output on Wroom |
| Relays not responding | Check MCP23017 I2C address (0x20), verify I2C pull-ups |
| Display blank | Check ESP32_Display_Panel board config, verify LDO3/LDO4 init in logs |
| RainMaker not connecting | Ensure WiFi is provisioned, check "PROV_1234" in BLE scan |
| Time not syncing | Wroom needs WiFi+NTP, sends time every 30s after connecting |
