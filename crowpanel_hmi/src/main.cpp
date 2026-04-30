/*
 * CrowPanel 7" HMI - Main Firmware (ESP32-P4)
 *
 * Receives ALL data from ESP32 Wroom relay controller via UART
 * (sensor data, relay status, time sync)
 * Displays everything on 1024x600 LVGL touch interface
 *
 * UART1: TX=IO47, RX=IO48 (connected to Wroom UART2)
 */

#include <Arduino.h>
#include <lvgl.h>
#include <time.h>
#include <sys/time.h>
#include "display_driver.h"
#include "ui.h"
#include "protocol.h"

// ============================================================
// Configuration
// ============================================================
#define SCREEN_WIDTH    1024
#define SCREEN_HEIGHT   600
#define LV_BUF_SIZE     (SCREEN_WIDTH * 40)  // 40 rows buffer

// UART1 pins for Wroom communication (ESP32-P4 external UART)
#define WROOM_UART_TX   PANEL_UART_TX  // IO47
#define WROOM_UART_RX   PANEL_UART_RX  // IO48

#define UI_UPDATE_INTERVAL 500  // Update UI every 500ms
#define SCHEDULE_CHECK_INTERVAL 1000 // Check schedules every 1 second
#define ACTION_CHECK_INTERVAL 500    // Check actions every 500ms

// Time sync state
static bool timeSynced = false;
static uint32_t timeBaseMs = 0;
static uint8_t baseHour = 6;
static uint8_t baseMinute = 0;
static uint8_t baseDow = 0; // 0=Monday

// ============================================================
// Display & Touch (handled by display_driver.cpp)
// ============================================================
// No LovyanGFX or manual LVGL driver setup needed —
// display_init() creates Board, LVGL drivers, and background task

// ============================================================
// UART receive state
// ============================================================
static uint8_t uartRxBuf[256];
static uint8_t uartRxIdx = 0;
static bool uartPacketStarted = false;

// ============================================================
// UART Communication with Wroom
// ============================================================
void sendRelayCommand(uint8_t relay_index, uint8_t state) {
    relay_set_payload_t cmd;
    cmd.relay_index = relay_index;
    cmd.state = state;

    uint8_t packet[MAX_PACKET_SIZE + 5];
    uint8_t idx = 0;

    packet[idx++] = PACKET_START_BYTE;
    packet[idx++] = CMD_RELAY_SET;
    packet[idx++] = sizeof(relay_set_payload_t);

    uint8_t checksum = CMD_RELAY_SET ^ sizeof(relay_set_payload_t);
    const uint8_t* payload = (const uint8_t*)&cmd;
    for (uint8_t i = 0; i < sizeof(relay_set_payload_t); i++) {
        packet[idx++] = payload[i];
        checksum ^= payload[i];
    }

    packet[idx++] = checksum;
    packet[idx++] = PACKET_END_BYTE;

    Serial1.write(packet, idx);

    Serial.printf("UART TX: Set relay %d -> %s\n", relay_index + 1, state ? "ON" : "OFF");
}

void sendAlarmUpdate(uint8_t alarm_index, uint8_t alarm_type, bool triggered,
                     bool permanent, float sensor_value, float threshold) {
    alarm_update_payload_t alarm;
    alarm.alarm_index = alarm_index;
    alarm.alarm_type = alarm_type;
    alarm.triggered = triggered ? 1 : 0;
    alarm.permanent = permanent ? 1 : 0;
    alarm.sensor_value = sensor_value;
    alarm.threshold = threshold;

    uint8_t packet[MAX_PACKET_SIZE + 5];
    uint8_t idx = 0;

    packet[idx++] = PACKET_START_BYTE;
    packet[idx++] = CMD_ALARM_UPDATE;
    packet[idx++] = sizeof(alarm_update_payload_t);

    uint8_t checksum = CMD_ALARM_UPDATE ^ sizeof(alarm_update_payload_t);
    const uint8_t* payload = (const uint8_t*)&alarm;
    for (uint8_t i = 0; i < sizeof(alarm_update_payload_t); i++) {
        packet[idx++] = payload[i];
        checksum ^= payload[i];
    }

    packet[idx++] = checksum;
    packet[idx++] = PACKET_END_BYTE;

    Serial1.write(packet, idx);

    Serial.printf("UART TX: Alarm %d type=%d triggered=%d\n",
                   alarm_index, alarm_type, triggered ? 1 : 0);
}

void processUartPacket(uint8_t cmd, const uint8_t* payload, uint8_t len) {
    switch (cmd) {
        case CMD_FULL_STATUS: {
            if (len >= sizeof(full_status_payload_t)) {
                const full_status_payload_t* status = (const full_status_payload_t*)payload;
                ui_set_uart_data(status->temperature, status->humidity, status->relay_states);

                Serial.printf("UART RX: T=%.1f H=%.1f Relays=0x%04X\n",
                               status->temperature, status->humidity, status->relay_states);
            }
            break;
        }
        case CMD_RELAY_STATUS: {
            if (len >= sizeof(relay_status_payload_t)) {
                const relay_status_payload_t* rs = (const relay_status_payload_t*)payload;
                ui_data.relay_states = rs->relay_states;
                ui_data.last_uart_time = millis();
            }
            break;
        }
        case CMD_SENSOR_REPORT: {
            if (len >= sizeof(sensor_report_payload_t)) {
                const sensor_report_payload_t* sr = (const sensor_report_payload_t*)payload;
                ui_data.room_temperature = sr->temperature;
                ui_data.room_humidity = sr->humidity;
                ui_data.last_uart_time = millis();
            }
            break;
        }
        case CMD_TIME_SYNC: {
            if (len >= sizeof(time_sync_payload_t)) {
                const time_sync_payload_t* ts = (const time_sync_payload_t*)payload;

                // Set system time from relay controller's NTP
                struct tm t = {};
                t.tm_year  = ts->year - 1900;
                t.tm_mon   = ts->month - 1;
                t.tm_mday  = ts->day;
                t.tm_hour  = ts->hour;
                t.tm_min   = ts->minute;
                t.tm_sec   = ts->second;
                t.tm_isdst = -1;
                time_t epoch = mktime(&t);
                struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
                settimeofday(&tv, nullptr);

                if (!timeSynced) {
                    timeSynced = true;
                    Serial.printf("Time synced: %04d-%02d-%02d %02d:%02d:%02d\n",
                                   ts->year, ts->month, ts->day,
                                   ts->hour, ts->minute, ts->second);
                }

                // Auto-reset daily/monthly/yearly volumes
                ui_check_volume_reset(ts->day, ts->month, ts->year);
            }
            break;
        }
        case CMD_SENSOR_DATA: {
            if (len >= sizeof(sensor_data_payload_t)) {
                const sensor_data_payload_t* sd = (const sensor_data_payload_t*)payload;

                ui_set_sensor_data(sd->flowRate, sd->temperature, sd->totalLiters,
                                   sd->isRaining, sd->rainAnalog, sd->windSpeed);

                Serial.printf("Sensor RX: Flow=%.2f L/min  Temp=%.1f°C  Total=%.2f L  Rain=%s  Wind=%.1f m/s\n",
                               sd->flowRate, sd->temperature, sd->totalLiters,
                               sd->isRaining ? "YES" : "NO", sd->windSpeed);
            }
            break;
        }
        default:
            Serial.printf("Unknown UART cmd: 0x%02X\n", cmd);
            break;
    }
}

void handleUartReceive() {
    while (Serial1.available()) {
        uint8_t b = Serial1.read();

        if (!uartPacketStarted) {
            if (b == PACKET_START_BYTE) {
                uartPacketStarted = true;
                uartRxIdx = 0;
                uartRxBuf[uartRxIdx++] = b;
            }
            continue;
        }

        if (uartRxIdx < sizeof(uartRxBuf)) {
            uartRxBuf[uartRxIdx++] = b;
        } else {
            uartPacketStarted = false;
            uartRxIdx = 0;
            continue;
        }

        if (b == PACKET_END_BYTE && uartRxIdx >= 5) {
            uint8_t cmd = uartRxBuf[1];
            uint8_t len = uartRxBuf[2];

            if (uartRxIdx == (uint8_t)(len + 5)) {
                // Verify checksum
                uint8_t checksum = cmd ^ len;
                for (uint8_t i = 0; i < len; i++) {
                    checksum ^= uartRxBuf[3 + i];
                }

                if (checksum == uartRxBuf[3 + len]) {
                    processUartPacket(cmd, &uartRxBuf[3], len);
                } else {
                    Serial.println("UART checksum error");
                }
            }

            uartPacketStarted = false;
            uartRxIdx = 0;
        }
    }
}

// ============================================================
// Relay command callback (called from UI)
// ============================================================
void onRelayCommand(uint8_t relay_index, uint8_t state) {
    sendRelayCommand(relay_index, state);
}

// ============================================================
// Alarm notification callback (called from action engine)
// ============================================================
void onAlarmNotify(uint8_t alarm_index, uint8_t alarm_type, bool triggered,
                   bool permanent, float sensor_value, float threshold) {
    sendAlarmUpdate(alarm_index, alarm_type, triggered, permanent,
                    sensor_value, threshold);
}

// ============================================================
// Setup
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("=== CrowPanel HMI Starting ===");

    // --- Initialize Display + Touch + LVGL ---
    if (!display_init()) {
        Serial.println("FATAL: Display init failed!");
        while (1) delay(1000);
    }
    Serial.println("Display initialized");

    // --- Initialize UART1 for Wroom ---
    Serial1.begin(UART_BAUD_RATE, SERIAL_8N1, WROOM_UART_RX, WROOM_UART_TX);
    Serial.println("UART1 initialized for relay controller");

    // --- Initialize UI (lock LVGL since background task is running) ---
    display_lock(-1);
    ui_init(onRelayCommand, onAlarmNotify);
    display_unlock();
    Serial.println("UI initialized");

    // --- Initialize time ---
    // Timezone: Europe/Bucharest (EET-2EEST,M3.5.0/3,M10.5.0/4)
    setenv("TZ", "EET-2EEST,M3.5.0/3,M10.5.0/4", 1);
    tzset();
    timeBaseMs = millis();
    Serial.println("Time initialized (will sync from relay controller via UART)");

    Serial.println("=== Setup Complete ===");
}

// ============================================================
// Time helpers (millis-based until RTC is set)
// ============================================================
static void getCurrentTime(uint8_t* hour, uint8_t* minute, uint8_t* dow) {
    struct tm timeinfo;
    if (timeSynced && getLocalTime(&timeinfo, 0)) {
        // Real time from relay controller's NTP
        *hour = timeinfo.tm_hour;
        *minute = timeinfo.tm_min;
        // tm_wday: 0=Sunday. Convert to 0=Monday
        *dow = (timeinfo.tm_wday + 6) % 7;
    } else {
        // Fallback: millis-based offset from base time
        uint32_t elapsed = (millis() - timeBaseMs) / 1000;
        uint32_t totalMinutes = baseHour * 60 + baseMinute + elapsed / 60;
        *hour = (totalMinutes / 60) % 24;
        *minute = totalMinutes % 60;
        *dow = (baseDow + (totalMinutes / (24 * 60))) % 7;
    }
}

// ============================================================
// Main Loop
// ============================================================
void loop() {
    // LVGL timer runs in background FreeRTOS task (display_driver.cpp)

    // Handle UART receive from Wroom
    handleUartReceive();

    // Periodic UI refresh (lock LVGL for thread safety)
    static uint32_t lastUIUpdate = 0;
    if (millis() - lastUIUpdate >= UI_UPDATE_INTERVAL) {
        lastUIUpdate = millis();
        display_lock(-1);
        ui_update();
        display_unlock();
    }

    // Process actions (sensor-based alarms)
    static uint32_t lastActionCheck = 0;
    if (millis() - lastActionCheck >= ACTION_CHECK_INTERVAL) {
        lastActionCheck = millis();
        display_lock(-1);
        ui_process_actions();
        display_unlock();
    }

    // Process schedules
    static uint32_t lastSchedCheck = 0;
    if (millis() - lastSchedCheck >= SCHEDULE_CHECK_INTERVAL) {
        lastSchedCheck = millis();
        uint8_t h, m, dow;
        getCurrentTime(&h, &m, &dow);
        display_lock(-1);
        ui_process_schedules(h, m, dow);
        display_unlock();
    }

    delay(5); // Small delay to prevent watchdog
}
