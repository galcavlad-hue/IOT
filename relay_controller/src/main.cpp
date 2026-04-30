/*
 * ESP32 Wroom - Relay Controller
 * Based on tested working RainMaker code, extended with UART to CrowPanel
 * and alarm forwarding.
 *
 * I2C bus (SDA=21, SCL=22):
 *   - MCP23017 GPIO expander (addr 0x20) → 16 relays (active LOW)
 *   - SSD1306 OLED 128x64 (addr via U8g2)
 *   - SHT20 temp/humidity sensor (addr 0x40)
 *
 * UART2 (TX=17, RX=16) → CrowPanel HMI
 * WiFi → ESP RainMaker (BLE provisioning)
 */

#include "RMaker.h"
#include "WiFi.h"
#include "WiFiProv.h"
#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_MCP23X17.h>
#include <SHT2x.h>
#include <time.h>
#include <esp_task_wdt.h>
#include "protocol.h"

// ============================================================
// Hardware Objects
// ============================================================
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
Adafruit_MCP23X17 mcp;
SHT2x sht;

bool mcpOnline = false;
bool shtOnline = false;
bool relayState[16] = { false };
float lastTemp = NAN;
float lastHum = NAN;

// ============================================================
// RainMaker Provisioning
// ============================================================
const char *service_name = "PROV_1234";
const char *pop = "abcd1234";

// ============================================================
// Physical Buttons
// ============================================================
#define BTN_WIFI 12
#define BTN_SELECT 13
#define BTN_TOGGLE 26
#define BTN_DEBOUNCE_MS 50

struct Button {
  uint8_t pin;
  bool lastState;
  unsigned long lastChange;
};

Button btnWifi = { BTN_WIFI, HIGH, 0 };
Button btnSelect = { BTN_SELECT, HIGH, 0 };
Button btnToggle = { BTN_TOGGLE, HIGH, 0 };

int8_t selectedRelay = 0;

#if CONFIG_IDF_TARGET_ESP32C3
static int gpio_0 = 9;
#else
static int gpio_0 = 0;
#endif

// ============================================================
// RainMaker Devices
// ============================================================
static Switch rm_relay[16];
static TemperatureSensor rm_temp("Temperature");
static Device rm_hum("Humidity", ESP_RMAKER_DEVICE_TEMP_SENSOR, NULL);

// Custom alarm devices (added for CrowPanel integration)
static Device* rmAlarmWaterFlow = nullptr;
static Device* rmAlarmRain = nullptr;
static Device* rmAlarmNoWater = nullptr;
static Device* rmAlarmFreezing = nullptr;
static Device* rmAlarmWind = nullptr;

// RainMaker devices for sensor node data
static TemperatureSensor rm_water_temp("Water_Temperature");
static Device rm_water_flow("Water_Flow", "custom.device.sensor", NULL);
static Device rm_rain_sensor("Rain_Sensor", "custom.device.sensor", NULL);
static Device rm_wind_speed("Wind_Speed", "custom.device.sensor", NULL);

// ============================================================
// UART1 from Sensor Node
// ============================================================
// Serial1: RX=GPIO14 (from sensor node TX), TX=GPIO27 (to sensor node, unused)
uint8_t sensorRxBuffer[128];
uint8_t sensorRxIndex = 0;
bool sensorPacketStarted = false;

// Latest sensor node readings
sensor_data_payload_t latestSensorData;
bool sensorNodeOnline = false;
uint32_t lastSensorDataTime = 0;
#define SENSOR_NODE_TIMEOUT_MS 10000  // Consider offline if no data for 10s

// ============================================================
// UART2 to CrowPanel
// ============================================================
#define STATUS_INTERVAL_MS 2000  // Send full status every 2 seconds

// UART receive buffer (from CrowPanel)
uint8_t rxBuffer[256];
uint8_t rxIndex = 0;
bool packetStarted = false;

// ============================================================
// Alarm State (received from CrowPanel via UART)
// ============================================================
typedef struct {
    bool active;
    uint8_t alarm_type;
    bool permanent;
    float sensor_value;
    float threshold;
} alarm_state_t;

alarm_state_t alarms[MAX_ALARMS];
int numAlarms = 0;

// ============================================================
// Forward Declarations
// ============================================================
void sendFullStatus();
void updateDisplay();
void rainmakerUpdateAlarm(uint8_t alarm_index, bool triggered, uint8_t alarm_type,
                           float sensor_value);

// ============================================================
// Button Helper
// ============================================================
bool btnCheck(Button &b) {
  bool reading = digitalRead(b.pin);
  unsigned long now = millis();
  if (reading != b.lastState && (now - b.lastChange) >= BTN_DEBOUNCE_MS) {
    b.lastState = reading;
    b.lastChange = now;
    if (reading == LOW) return true;
  }
  return false;
}

// ============================================================
// Display Helpers
// ============================================================
const char* wifiBarStr(int rssi) {
  if (WiFi.status() != WL_CONNECTED) return "----";
  if (rssi >= -55) return "||||";
  if (rssi >= -65) return "|||.";
  if (rssi >= -75) return "||..";
  return "|...";
}

// ============================================================
// I2C Probe Functions
// ============================================================
bool probeMcp() {
  Wire.beginTransmission(0x20);
  return (Wire.endTransmission() == 0);
}

bool probeSht() {
  Wire.beginTransmission(0x40);
  return (Wire.endTransmission() == 0);
}

// ============================================================
// Relay Control via MCP23017 (active LOW)
// ============================================================
void flushRelays() {
  if (!mcpOnline) return;
  for (uint8_t i = 0; i < 16; i++) {
    mcp.digitalWrite(i, relayState[i] ? LOW : HIGH);
  }
}

void setRelay(uint8_t idx, bool on) {
  if (idx >= 16) return;
  relayState[idx] = on;
  if (mcpOnline) mcp.digitalWrite(idx, on ? LOW : HIGH);
}

uint16_t getRelayBitmask() {
  uint16_t mask = 0;
  for (int i = 0; i < 16; i++) {
    if (relayState[i]) mask |= (1 << i);
  }
  return mask;
}

// ============================================================
// WiFi Reset
// ============================================================
void startWifiReset() {
  Serial.println("[WiFi] Reset requested");
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(0, 8, "WiFi Reset");
  u8g2.drawStr(0, 20, "Connect to AP:");
  u8g2.drawStr(0, 32, "IrrigationSetup");
  u8g2.drawStr(0, 44, "Then open");
  u8g2.drawStr(0, 56, "192.168.4.1");
  u8g2.sendBuffer();
  RMakerWiFiReset(2);
}

// ============================================================
// OLED Relay Box Drawing
// ============================================================
void drawRelayBox(uint8_t x, uint8_t yBox, uint8_t idx) {
  bool selected = (idx == (uint8_t)selectedRelay);
  bool on = relayState[idx];
  uint8_t bh = 9, hw = 6;
  u8g2.drawFrame(x, yBox, 13, bh);
  if (selected) u8g2.drawBox(x + 1, yBox + 1, hw - 1, bh - 2);
  u8g2.drawVLine(x + hw, yBox + 1, bh - 2);
  if (on) {
    u8g2.drawBox(x + hw + 1, yBox + 1, hw - 1, bh - 2);
  } else {
    u8g2.drawLine(x + hw + 1, yBox + 1, x + 12, yBox + bh - 2);
    u8g2.drawLine(x + 12, yBox + 1, x + hw + 1, yBox + bh - 2);
  }
}

// ============================================================
// OLED Display Update (U8g2)
// ============================================================
void updateDisplay() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tr);

  // Line 1: WiFi + Clock
  const char *bars = wifiBarStr(WiFi.RSSI());
  const char *wstate = (WiFi.status() == WL_CONNECTED) ? "OK" : "--";
  struct tm t;
  char timeBuf[18] = "--/-- --:--:--";
  if (getLocalTime(&t, 50))
    snprintf(timeBuf, sizeof(timeBuf), "%02d/%02d %02d:%02d:%02d",
             t.tm_mday, t.tm_mon + 1, t.tm_hour, t.tm_min, t.tm_sec);
  char line1[32];
  snprintf(line1, sizeof(line1), "%s %s %s", bars, wstate, timeBuf);
  u8g2.drawStr(0, 8, line1);
  u8g2.drawHLine(0, 10, 128);

  // Line 2: Temp & Humidity
  if (!shtOnline) {
    u8g2.drawStr(0, 22, "T:--- H:--- SHT20?");
  } else {
    char thBuf[24];
    if (isnan(lastTemp) || isnan(lastHum)) {
      snprintf(thBuf, sizeof(thBuf), "T:--- H:---");
    } else {
      char tStr[8], hStr[8];
      dtostrf(lastTemp, 4, 1, tStr);
      dtostrf(lastHum, 4, 1, hStr);
      snprintf(thBuf, sizeof(thBuf), "T:%sC H:%s%%", tStr, hStr);
    }
    u8g2.drawStr(0, 22, thBuf);
  }
  u8g2.drawHLine(0, 25, 128);

  // Relay grid
  if (!mcpOnline) {
    u8g2.drawStr(0, 35, "!! RELAY BOARD");
    u8g2.drawStr(0, 46, "NOT CONNECTED");
    u8g2.drawStr(0, 57, "MCP23017 @ 0x20");
  } else {
    for (uint8_t i = 0; i < 16; i++) {
      uint8_t col = i % 8, row = i / 8;
      uint8_t x = col * 16;
      uint8_t yLbl = (row == 0) ? 33 : 51;
      uint8_t yBox = (row == 0) ? 35 : 53;
      char lbl[4];
      snprintf(lbl, sizeof(lbl), "%02d", i + 1);
      u8g2.drawStr(x, yLbl, lbl);
      drawRelayBox(x, yBox, i);
    }
  }
  u8g2.sendBuffer();
}

// ============================================================
// UART Communication with CrowPanel
// ============================================================
void sendUARTPacket(uint8_t cmd, const uint8_t* payload, uint8_t payloadLen) {
    uint8_t packet[MAX_PACKET_SIZE + 5];
    uint8_t idx = 0;

    packet[idx++] = PACKET_START_BYTE;
    packet[idx++] = cmd;
    packet[idx++] = payloadLen;

    uint8_t checksum = cmd ^ payloadLen;
    for (uint8_t i = 0; i < payloadLen; i++) {
        packet[idx++] = payload[i];
        checksum ^= payload[i];
    }

    packet[idx++] = checksum;
    packet[idx++] = PACKET_END_BYTE;

    Serial2.write(packet, idx);
}

void sendFullStatus() {
    full_status_payload_t status;
    status.relay_states = getRelayBitmask();
    status.temperature = isnan(lastTemp) ? 0.0f : lastTemp;
    status.humidity = isnan(lastHum) ? 0.0f : lastHum;

    sendUARTPacket(CMD_FULL_STATUS, (uint8_t*)&status, sizeof(status));
}

void forwardSensorDataToPanel() {
    sendUARTPacket(CMD_SENSOR_DATA, (uint8_t*)&latestSensorData, sizeof(latestSensorData));
}

// ============================================================
// Sensor Node UART Receive (Serial1)
// ============================================================
void processSensorPacket(uint8_t cmd, const uint8_t* payload, uint8_t len) {
    if (cmd == CMD_SENSOR_DATA && len >= sizeof(sensor_data_payload_t)) {
        memcpy(&latestSensorData, payload, sizeof(sensor_data_payload_t));
        sensorNodeOnline = true;
        lastSensorDataTime = millis();

        // Forward to CrowPanel
        forwardSensorDataToPanel();

        // Update RainMaker with sensor node data
        rm_water_temp.updateAndReportParam(ESP_RMAKER_DEF_TEMPERATURE_NAME,
                                            latestSensorData.temperature);
        rm_water_flow.updateAndReportParam("Flow_Rate", latestSensorData.flowRate);
        rm_water_flow.updateAndReportParam("Total_Liters", latestSensorData.totalLiters);
        rm_rain_sensor.updateAndReportParam("Is_Raining",
                                             latestSensorData.isRaining ? true : false);
        rm_rain_sensor.updateAndReportParam("Rain_Analog",
                                             (int)latestSensorData.rainAnalog);
        rm_wind_speed.updateAndReportParam("Wind_Speed", latestSensorData.windSpeed);

        Serial.printf("[Sensor] Flow=%.2f L/min  Temp=%.1f°C  Total=%.2f L  Rain=%s  Wind=%.1f m/s\n",
                       latestSensorData.flowRate, latestSensorData.temperature,
                       latestSensorData.totalLiters,
                       latestSensorData.isRaining ? "YES" : "NO",
                       latestSensorData.windSpeed);
    } else {
        Serial.printf("[Sensor] Unknown cmd: 0x%02X\n", cmd);
    }
}

void handleSensorUARTReceive() {
    while (Serial1.available()) {
        uint8_t b = Serial1.read();

        if (!sensorPacketStarted) {
            if (b == PACKET_START_BYTE) {
                sensorPacketStarted = true;
                sensorRxIndex = 0;
                sensorRxBuffer[sensorRxIndex++] = b;
            }
            continue;
        }

        if (sensorRxIndex < sizeof(sensorRxBuffer)) {
            sensorRxBuffer[sensorRxIndex++] = b;
        } else {
            sensorPacketStarted = false;
            sensorRxIndex = 0;
            continue;
        }

        // Use length-based parsing: calculate expected packet size from length field
        if (sensorRxIndex >= 3) {
            uint8_t payloadLen = sensorRxBuffer[2];
            uint8_t expectedTotal = 5 + payloadLen;  // START + CMD + LEN + PAYLOAD + CHECKSUM + END
            
            if (sensorRxIndex == expectedTotal) {
                // Packet complete: validate checksum and end byte
                uint8_t cmd = sensorRxBuffer[1];
                uint8_t checksum = cmd ^ payloadLen;
                for (uint8_t i = 0; i < payloadLen; i++) {
                    checksum ^= sensorRxBuffer[3 + i];
                }

                if (sensorRxBuffer[expectedTotal - 1] == PACKET_END_BYTE &&
                    checksum == sensorRxBuffer[3 + payloadLen]) {
                    processSensorPacket(cmd, &sensorRxBuffer[3], payloadLen);
                } else if (sensorRxBuffer[expectedTotal - 1] != PACKET_END_BYTE) {
                    Serial.println("[Sensor] UART end byte missing");
                } else {
                    Serial.println("[Sensor] UART checksum error");
                }
                sensorPacketStarted = false;
                sensorRxIndex = 0;
            }
        }
    }
}

void processUARTPacket(uint8_t cmd, const uint8_t* payload, uint8_t len) {
    switch (cmd) {
        case CMD_RELAY_SET: {
            if (len >= sizeof(relay_set_payload_t)) {
                const relay_set_payload_t* setCmd = (const relay_set_payload_t*)payload;
                if (setCmd->relay_index < 16) {
                    setRelay(setCmd->relay_index, setCmd->state);
                    rm_relay[setCmd->relay_index].updateAndReportParam(
                        ESP_RMAKER_DEF_POWER_NAME, (bool)setCmd->state);
                    sendFullStatus();
                    updateDisplay();
                }
            }
            break;
        }
        case CMD_RELAY_GET: {
            relay_status_payload_t relayStatus;
            relayStatus.relay_states = getRelayBitmask();
            sendUARTPacket(CMD_RELAY_STATUS, (uint8_t*)&relayStatus, sizeof(relayStatus));
            break;
        }
        case CMD_ALARM_UPDATE: {
            if (len >= sizeof(alarm_update_payload_t)) {
                const alarm_update_payload_t* alm = (const alarm_update_payload_t*)payload;
                uint8_t idx = alm->alarm_index;
                if (idx < MAX_ALARMS) {
                    alarms[idx].active = alm->triggered;
                    alarms[idx].alarm_type = alm->alarm_type;
                    alarms[idx].permanent = alm->permanent;
                    alarms[idx].sensor_value = alm->sensor_value;
                    alarms[idx].threshold = alm->threshold;
                    // Note: numAlarms tracks the highest index+1. If alarm indices arrive
                    // out of order (e.g., index 5 before 3), intermediate slots remain
                    // initialized to 0 from memset(alarms,...) at boot. This is safe.
                    if (idx >= numAlarms) numAlarms = idx + 1;

                    Serial.printf("Alarm %d: type=%d triggered=%d value=%.1f threshold=%.1f\n",
                                   idx, alm->alarm_type, alm->triggered,
                                   alm->sensor_value, alm->threshold);

                    rainmakerUpdateAlarm(idx, alm->triggered, alm->alarm_type,
                                          alm->sensor_value);
                }
            }
            break;
        }
        default:
            Serial.printf("Unknown UART cmd: 0x%02X\n", cmd);
            break;
    }
}

void handleUARTReceive() {
    while (Serial2.available()) {
        uint8_t b = Serial2.read();

        if (!packetStarted) {
            if (b == PACKET_START_BYTE) {
                packetStarted = true;
                rxIndex = 0;
                rxBuffer[rxIndex++] = b;
            }
            continue;
        }

        if (rxIndex < sizeof(rxBuffer)) {
            rxBuffer[rxIndex++] = b;
        } else {
            packetStarted = false;
            rxIndex = 0;
            continue;
        }

        // Use length-based parsing: calculate expected packet size from length field
        if (rxIndex >= 3) {
            uint8_t payloadLen = rxBuffer[2];
            uint8_t expectedTotal = 5 + payloadLen;  // START + CMD + LEN + PAYLOAD + CHECKSUM + END
            
            if (rxIndex == expectedTotal) {
                // Packet complete: validate checksum and end byte
                uint8_t cmd = rxBuffer[1];
                uint8_t checksum = cmd ^ payloadLen;
                for (uint8_t i = 0; i < payloadLen; i++) {
                    checksum ^= rxBuffer[3 + i];
                }

                if (rxBuffer[expectedTotal - 1] == PACKET_END_BYTE &&
                    checksum == rxBuffer[3 + payloadLen]) {
                    processUARTPacket(cmd, &rxBuffer[3], payloadLen);
                } else if (rxBuffer[expectedTotal - 1] != PACKET_END_BYTE) {
                    Serial.println("UART end byte missing");
                } else {
                    Serial.println("UART checksum error");
                }
                packetStarted = false;
                rxIndex = 0;
            }
        }
    }
}

// ============================================================
// RainMaker Callbacks
// ============================================================
void write_callback(Device *device, Param *param, const param_val_t val,
                    void *priv_data, write_ctx_t *ctx) {
  if (strcmp(param->getParamName(), "Power") != 0) return;
  const char *name = device->getDeviceName();
  int idx = atoi(name + 6) - 1;  // "Relay 01" -> 0
  if (idx < 0 || idx > 15) return;
  bool on = val.val.b;
  Serial.printf("[RM] Relay %02d -> %s\n", idx + 1, on ? "ON" : "OFF");
  setRelay(idx, on);
  param->updateAndReport(val);
  sendFullStatus();
  updateDisplay();
}

void sysProvEvent(arduino_event_t *sys_event) {
  if (sys_event->event_id == ARDUINO_EVENT_PROV_START) {
    #if CONFIG_IDF_TARGET_ESP32S2
      printQR(service_name, pop, "softap");
    #else
      printQR(service_name, pop, "ble");
    #endif
  }
}

// ============================================================
// RainMaker Alarm Update (forwarded from CrowPanel)
// ============================================================
void rainmakerUpdateAlarm(uint8_t alarm_index, bool triggered, uint8_t alarm_type,
                           float sensor_value) {
    Device* dev = nullptr;
    const char* msg_ok = "";
    const char* msg_alarm = "";
    const char* type_name = "Unknown";

    switch (alarm_type) {
        case ALARM_TYPE_WATER_FLOW:
            dev = rmAlarmWaterFlow;
            msg_ok = "OK - No leak detected";
            msg_alarm = "ALARM - Possible pipe leak!";
            type_name = "WaterFlow";
            break;
        case ALARM_TYPE_RAIN_AUTO:
            dev = rmAlarmRain;
            msg_ok = "OK - No rain";
            msg_alarm = "ALERT - Rain detected, shed closed";
            type_name = "Rain";
            break;
        case ALARM_TYPE_NO_WATER:
            dev = rmAlarmNoWater;
            msg_ok = "OK - Water flow normal";
            msg_alarm = "ALARM - No water flow during scheduled run!";
            type_name = "NoWater";
            break;
        case ALARM_TYPE_FREEZING:
            dev = rmAlarmFreezing;
            msg_ok = "OK - Temperature normal";
            msg_alarm = "ALARM - Freezing temperature! Emergency water dump";
            type_name = "Freezing";
            break;
        case ALARM_TYPE_WIND_SPEED:
            dev = rmAlarmWind;
            msg_ok = "OK - Wind speed normal";
            msg_alarm = "ALERT - High wind speed! Protective relay activated";
            type_name = "Wind";
            break;
    }

    if (!dev) return;

    dev->updateAndReportParam("alarm_active", triggered);
    dev->updateAndReportParam("sensor_value", sensor_value);
    dev->updateAndReportParam("message", triggered ? msg_alarm : msg_ok);

    Serial.printf("RainMaker: Alarm %d (%s) -> %s (value=%.1f)\n",
                   alarm_index, type_name,
                   triggered ? "TRIGGERED" : "CLEARED", sensor_value);
}

// ============================================================
// Setup
// ============================================================
void setup() {
  Serial.begin(115200);
  
  // Hardware watchdog: reset if loop stalls > 10 seconds
  esp_task_wdt_init(10, true);
  esp_task_wdt_add(NULL);
  
  pinMode(gpio_0, INPUT);
  pinMode(BTN_WIFI, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);
  pinMode(BTN_TOGGLE, INPUT_PULLUP);
  Wire.begin(21, 22);

  // Initialize alarm states
  memset(alarms, 0, sizeof(alarms));
  numAlarms = 0;

  // OLED boot screen
  u8g2.begin();
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.clearBuffer();
  u8g2.drawStr(0, 8, "Irrigation Ctrl");
  u8g2.drawStr(0, 20, "Booting...");
  u8g2.sendBuffer();

  // MCP23017 relay board
  mcpOnline = probeMcp();
  if (mcpOnline) {
    mcp.begin_I2C(0x20);
    for (uint8_t i = 0; i < 16; i++) {
      mcp.pinMode(i, OUTPUT);
      mcp.digitalWrite(i, HIGH);  // All relays OFF (active LOW)
    }
    Serial.println("[MCP] Relay board online");
  }

  // SHT20 temperature/humidity sensor
  shtOnline = probeSht();
  if (shtOnline) {
    sht.begin();
    delay(100);
    // Discard first read (often returns -39.8°C after reset)
    sht.read();
    delay(100);
    if (sht.read()) {
      float t = sht.getTemperature();
      float h = sht.getHumidity();
      // Apply sanity check on initial read
      if (t > -40.0 && t < 125.0 && h >= 0.0 && h <= 100.0) {
        lastTemp = t;
        lastHum = h;
      } else {
        Serial.printf("[SHT] Initial read out of range: T=%.1f H=%.1f (sanitized)\n", t, h);
      }
    }
  }

  // UART1 for Sensor Node input
  Serial1.begin(UART_BAUD_RATE, SERIAL_8N1, WROOM_SENSOR_UART_RX, WROOM_SENSOR_UART_TX);
  Serial.println("UART1 initialized for Sensor Node");

  // UART2 for CrowPanel communication
  Serial2.begin(UART_BAUD_RATE, SERIAL_8N1, WROOM_PANEL_UART_RX, WROOM_PANEL_UART_TX);
  Serial.println("UART2 initialized for CrowPanel");

  // Initialize sensor data
  memset(&latestSensorData, 0, sizeof(latestSensorData));

  // ---- RainMaker Setup (matches tested working code) ----
  Node my_node = RMaker.initNode("Irrigation Controller");

  for (uint8_t i = 0; i < 16; i++) {
    char devName[12];
    snprintf(devName, sizeof(devName), "Relay %02d", i + 1);
    rm_relay[i] = Switch(devName, NULL);
    rm_relay[i].addCb(write_callback);
    rm_relay[i].updateAndReportParam(ESP_RMAKER_DEF_POWER_NAME, false);
    my_node.addDevice(rm_relay[i]);
  }

  my_node.addDevice(rm_temp);

  rm_hum.addParam(Param("Humidity", "esp.param.humidity",
                         value(0.0f), PROP_FLAG_READ | PROP_FLAG_TIME_SERIES));
  rm_hum.assignPrimaryParam(rm_hum.getParamByName("Humidity"));
  my_node.addDevice(rm_hum);

  // Alarm - Water Flow (virtual sensor for CrowPanel alarms)
  rmAlarmWaterFlow = new Device("Water_Leak_Alarm", "custom.device.alarm", nullptr);
  rmAlarmWaterFlow->addNameParam();
  rmAlarmWaterFlow->addParam(Param("alarm_active", "custom.param.alarm_active",
                                    value(false), PROP_FLAG_READ));
  rmAlarmWaterFlow->addParam(Param("sensor_value", "custom.param.sensor_value",
                                    value(0.0f), PROP_FLAG_READ));
  rmAlarmWaterFlow->addParam(Param("message", "custom.param.message",
                                    value("OK - No leak detected"), PROP_FLAG_READ));
  rmAlarmWaterFlow->assignPrimaryParam(rmAlarmWaterFlow->getParamByName("alarm_active"));
  my_node.addDevice(*rmAlarmWaterFlow);

  // Alarm - Rain (virtual sensor for CrowPanel alarms)
  rmAlarmRain = new Device("Rain_Alert", "custom.device.alarm", nullptr);
  rmAlarmRain->addNameParam();
  rmAlarmRain->addParam(Param("alarm_active", "custom.param.alarm_active",
                               value(false), PROP_FLAG_READ));
  rmAlarmRain->addParam(Param("sensor_value", "custom.param.sensor_value",
                               value(0.0f), PROP_FLAG_READ));
  rmAlarmRain->addParam(Param("message", "custom.param.message",
                               value("OK - No rain"), PROP_FLAG_READ));
  rmAlarmRain->assignPrimaryParam(rmAlarmRain->getParamByName("alarm_active"));
  my_node.addDevice(*rmAlarmRain);

  // Alarm - No Water (virtual sensor)
  rmAlarmNoWater = new Device("No_Water_Alarm", "custom.device.alarm", nullptr);
  rmAlarmNoWater->addNameParam();
  rmAlarmNoWater->addParam(Param("alarm_active", "custom.param.alarm_active",
                                  value(false), PROP_FLAG_READ));
  rmAlarmNoWater->addParam(Param("sensor_value", "custom.param.sensor_value",
                                  value(0.0f), PROP_FLAG_READ));
  rmAlarmNoWater->addParam(Param("message", "custom.param.message",
                                  value("OK - Water flow normal"), PROP_FLAG_READ));
  rmAlarmNoWater->assignPrimaryParam(rmAlarmNoWater->getParamByName("alarm_active"));
  my_node.addDevice(*rmAlarmNoWater);

  // Alarm - Freezing Temperature (virtual sensor)
  rmAlarmFreezing = new Device("Freezing_Temp_Alarm", "custom.device.alarm", nullptr);
  rmAlarmFreezing->addNameParam();
  rmAlarmFreezing->addParam(Param("alarm_active", "custom.param.alarm_active",
                                   value(false), PROP_FLAG_READ));
  rmAlarmFreezing->addParam(Param("sensor_value", "custom.param.sensor_value",
                                   value(0.0f), PROP_FLAG_READ));
  rmAlarmFreezing->addParam(Param("message", "custom.param.message",
                                   value("OK - Temperature normal"), PROP_FLAG_READ));
  rmAlarmFreezing->assignPrimaryParam(rmAlarmFreezing->getParamByName("alarm_active"));
  my_node.addDevice(*rmAlarmFreezing);

  // Water Flow sensor (from sensor node)
  rm_water_flow.addNameParam();
  rm_water_flow.addParam(Param("Flow_Rate", "custom.param.flow_rate",
                                value(0.0f), PROP_FLAG_READ | PROP_FLAG_TIME_SERIES));
  rm_water_flow.addParam(Param("Total_Liters", "custom.param.total_liters",
                                value(0.0f), PROP_FLAG_READ | PROP_FLAG_TIME_SERIES));
  rm_water_flow.assignPrimaryParam(rm_water_flow.getParamByName("Flow_Rate"));
  my_node.addDevice(rm_water_flow);

  // Water Temperature (from sensor node DS18B20)
  my_node.addDevice(rm_water_temp);

  // Rain Sensor (from sensor node)
  rm_rain_sensor.addNameParam();
  rm_rain_sensor.addParam(Param("Is_Raining", "custom.param.is_raining",
                                 value(false), PROP_FLAG_READ));
  rm_rain_sensor.addParam(Param("Rain_Analog", "custom.param.rain_analog",
                                 value(0), PROP_FLAG_READ));
  rm_rain_sensor.assignPrimaryParam(rm_rain_sensor.getParamByName("Is_Raining"));
  my_node.addDevice(rm_rain_sensor);

  // Wind Speed sensor (from sensor node)
  rm_wind_speed.addNameParam();
  rm_wind_speed.addParam(Param("Wind_Speed", "custom.param.wind_speed",
                                value(0.0f), PROP_FLAG_READ | PROP_FLAG_TIME_SERIES));
  rm_wind_speed.assignPrimaryParam(rm_wind_speed.getParamByName("Wind_Speed"));
  my_node.addDevice(rm_wind_speed);

  // Alarm - High Wind Speed (virtual sensor)
  rmAlarmWind = new Device("Wind_Speed_Alarm", "custom.device.alarm", nullptr);
  rmAlarmWind->addNameParam();
  rmAlarmWind->addParam(Param("alarm_active", "custom.param.alarm_active",
                               value(false), PROP_FLAG_READ));
  rmAlarmWind->addParam(Param("sensor_value", "custom.param.sensor_value",
                               value(0.0f), PROP_FLAG_READ));
  rmAlarmWind->addParam(Param("message", "custom.param.message",
                               value("OK - Wind speed normal"), PROP_FLAG_READ));
  rmAlarmWind->assignPrimaryParam(rmAlarmWind->getParamByName("alarm_active"));
  my_node.addDevice(*rmAlarmWind);

  RMaker.enableOTA(OTA_USING_PARAMS);
  RMaker.enableTZService();
  RMaker.enableSchedule();
  RMaker.setTimeZone("Europe/Bucharest");
  RMaker.start();

  WiFi.onEvent(sysProvEvent);
  #if CONFIG_IDF_TARGET_ESP32S2
    WiFiProv.beginProvision(WIFI_PROV_SCHEME_SOFTAP, WIFI_PROV_SCHEME_HANDLER_NONE,
                            WIFI_PROV_SECURITY_1, pop, service_name);
  #else
    WiFiProv.beginProvision(WIFI_PROV_SCHEME_BLE, WIFI_PROV_SCHEME_HANDLER_FREE_BTDM,
                            WIFI_PROV_SECURITY_1, pop, service_name);
  #endif
}

// ============================================================
// Main Loop
// ============================================================
void loop() {
  // Feed watchdog first (safety-critical)
  esp_task_wdt_reset();
  
  static unsigned long lastDisplay = 0, lastMcpProbe = 0, lastSensor = 0, lastStatusSend = 0;
  unsigned long now = millis();

  // Handle UART from Sensor Node (Serial1)
  handleSensorUARTReceive();

  // Check sensor node timeout
  if (sensorNodeOnline && (now - lastSensorDataTime > SENSOR_NODE_TIMEOUT_MS)) {
    sensorNodeOnline = false;
    Serial.println("[Sensor] Node offline (timeout)");
  }

  // Handle UART from CrowPanel (Serial2)
  handleUARTReceive();

  // Physical buttons
  if (btnCheck(btnWifi)) startWifiReset();
  if (btnCheck(btnSelect)) {
    selectedRelay = (selectedRelay + 1) % 16;
    updateDisplay();
  }
  if (btnCheck(btnToggle)) {
    bool newState = !relayState[selectedRelay];
    setRelay(selectedRelay, newState);
    rm_relay[selectedRelay].updateAndReportParam(ESP_RMAKER_DEF_POWER_NAME, newState);
    sendFullStatus();
    updateDisplay();
  }

  // MCP23017 hot-plug detection
  if (now - lastMcpProbe >= 5000) {
    lastMcpProbe = now;
    bool found = probeMcp();
    if (found && !mcpOnline) {
      mcpOnline = true;
      mcp.begin_I2C(0x20);
      for (uint8_t i = 0; i < 16; i++) mcp.pinMode(i, OUTPUT);
      flushRelays();
    } else if (!found && mcpOnline) {
      mcpOnline = false;
    }
  }

  // SHT20 reading (every 30 seconds, matches working code)
  if (now - lastSensor >= 30000) {
    lastSensor = now;
    if (probeSht() && sht.read()) {
      float t = sht.getTemperature();
      float h = sht.getHumidity();
      // Sanity check: valid SHT20 range is -40~125°C, 0~100%
      if (t > -40.0 && t < 125.0 && h >= 0.0 && h <= 100.0) {
        lastTemp = t;
        lastHum = h;
        rm_temp.updateAndReportParam(ESP_RMAKER_DEF_TEMPERATURE_NAME, lastTemp);
        rm_hum.updateAndReportParam("Humidity", lastHum);
      } else {
        Serial.printf("[SHT] Read out of range: T=%.1f H=%.1f (rejected)\n", t, h);
      }
    }
  }

  // Send full status to CrowPanel every 2 seconds
  if (now - lastStatusSend >= STATUS_INTERVAL_MS) {
    lastStatusSend = now;
    sendFullStatus();
  }

  // Send time sync to CrowPanel every 30 seconds (relay controller has NTP)
  static unsigned long lastTimeSync = 0;
  if (now - lastTimeSync >= 30000) {
    lastTimeSync = now;
    struct tm t;
    if (getLocalTime(&t, 50)) {
      time_sync_payload_t ts;
      ts.year   = t.tm_year + 1900;
      ts.month  = t.tm_mon + 1;
      ts.day    = t.tm_mday;
      ts.hour   = t.tm_hour;
      ts.minute = t.tm_min;
      ts.second = t.tm_sec;
      ts.dow    = t.tm_wday;
      sendUARTPacket(CMD_TIME_SYNC, (uint8_t*)&ts, sizeof(ts));
    }
  }

  // Display update
  if (now - lastDisplay >= 500) {
    lastDisplay = now;
    updateDisplay();
  }

  // Factory reset: hold GPIO 0 > 10 seconds
  if (digitalRead(gpio_0) == LOW) {
    delay(100);
    int t0 = millis();
    while (digitalRead(gpio_0) == LOW) delay(50);
    if (millis() - t0 > 10000) RMakerFactoryReset(2);
  }

  delay(20);
}
