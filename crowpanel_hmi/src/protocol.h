#pragma once

#include <stdint.h>

// ============================================================
// System Architecture (UART-only, no ESP-NOW)
// ============================================================
//   Sensor Node (ESP32-C3) --UART--> Wroom (ESP32) --UART--> CrowPanel (ESP32-P4)
//                                    Wroom --WiFi--> RainMaker
//
// Sensor Node: reads flow, DS18B20, rain → sends CMD_SENSOR_DATA to Wroom via UART
// Wroom: receives sensor data, manages relays/SHT20/RainMaker, forwards everything to CrowPanel
// CrowPanel: pure display, receives all data from Wroom via UART

// ============================================================
// UART Configuration
// ============================================================
#define UART_BAUD_RATE 115200

// Sensor Node (ESP32-C3) UART1 pins → Wroom
#define SENSOR_NODE_UART_TX  6   // C3 GPIO6 TX → Wroom RX
#define SENSOR_NODE_UART_RX  7   // C3 GPIO7 RX (unused, but reserved)

// Wroom UART1 pins ← Sensor Node
#define WROOM_SENSOR_UART_RX 14  // Wroom GPIO14 RX ← Sensor Node TX
#define WROOM_SENSOR_UART_TX 27  // Wroom GPIO27 TX → Sensor Node RX (unused)

// Wroom UART2 pins → CrowPanel
#define WROOM_PANEL_UART_TX  17  // Wroom GPIO17 TX → CrowPanel RX
#define WROOM_PANEL_UART_RX  16  // Wroom GPIO16 RX ← CrowPanel TX

// CrowPanel (ESP32-P4) UART1 pins ← Wroom
#define PANEL_UART_TX  47  // P4 IO47 TX → Wroom RX
#define PANEL_UART_RX  48  // P4 IO48 RX ← Wroom TX

// ============================================================
// Packet Framing
// ============================================================
#define PACKET_START_BYTE 0xAA
#define PACKET_END_BYTE   0x55
#define MAX_PACKET_SIZE   128

// Command IDs
#define CMD_RELAY_STATUS    0x01  // Wroom -> CrowPanel: relay states
#define CMD_SENSOR_REPORT   0x02  // Wroom -> CrowPanel: temp/humidity
#define CMD_RELAY_SET       0x03  // CrowPanel -> Wroom: set relay
#define CMD_RELAY_GET       0x04  // CrowPanel -> Wroom: request states
#define CMD_FULL_STATUS     0x05  // Wroom -> CrowPanel: all data combined
#define CMD_ACK             0x06  // Acknowledgement
#define CMD_ALARM_UPDATE    0x07  // CrowPanel -> Wroom: alarm state change
#define CMD_TIME_SYNC       0x08  // Wroom -> CrowPanel: current date/time from NTP
#define CMD_SENSOR_DATA     0x09  // Sensor Node -> Wroom (and Wroom -> CrowPanel): sensor readings

// Maximum number of alarms
#define MAX_ALARMS 8

// UART Packet structure:
// [START_BYTE][CMD][LENGTH][PAYLOAD...][CHECKSUM][END_BYTE]
typedef struct __attribute__((packed)) {
    uint8_t start;      // PACKET_START_BYTE
    uint8_t cmd;        // Command ID
    uint8_t length;     // Length of payload
    uint8_t payload[MAX_PACKET_SIZE];
    uint8_t checksum;   // XOR of cmd + length + payload bytes
    uint8_t end;        // PACKET_END_BYTE
} uart_packet_t;

// Relay status payload (CMD_RELAY_STATUS)
typedef struct __attribute__((packed)) {
    uint16_t relay_states;  // Bitmask: bit0=relay1, bit15=relay16
} relay_status_payload_t;

// Sensor report payload (CMD_SENSOR_REPORT)
typedef struct __attribute__((packed)) {
    float temperature;   // °C from DHT22
    float humidity;      // % from DHT22
} sensor_report_payload_t;

// Full status payload (CMD_FULL_STATUS) - sent every 2 seconds
typedef struct __attribute__((packed)) {
    uint16_t relay_states;  // Bitmask
    float temperature;      // °C
    float humidity;         // %
} full_status_payload_t;

// Relay set command payload (CMD_RELAY_SET)
typedef struct __attribute__((packed)) {
    uint8_t relay_index;    // 0-15
    uint8_t state;          // 0=OFF, 1=ON
} relay_set_payload_t;

// Alarm types (matches action_type_t in ui.h)
#define ALARM_TYPE_WATER_FLOW   0   // Pipe leak alarm
#define ALARM_TYPE_RAIN_AUTO    1   // Rain auto-close
#define ALARM_TYPE_NO_WATER     2   // No water flow during scheduled run
#define ALARM_TYPE_FREEZING     3   // Freezing temperature emergency
#define ALARM_TYPE_WIND_SPEED   4   // High wind speed

// Alarm update payload (CMD_ALARM_UPDATE)
typedef struct __attribute__((packed)) {
    uint8_t alarm_index;    // 0-based alarm index
    uint8_t alarm_type;     // ALARM_TYPE_*
    uint8_t triggered;      // 0=cleared, 1=triggered
    uint8_t permanent;      // 0=auto-revert, 1=permanent (needs dismiss)
    float sensor_value;     // Current sensor reading that caused it
    float threshold;        // Threshold that was exceeded
} alarm_update_payload_t;

// Time sync payload (CMD_TIME_SYNC) - sent from Wroom (has NTP) to CrowPanel
typedef struct __attribute__((packed)) {
    uint16_t year;          // e.g. 2026
    uint8_t  month;         // 1-12
    uint8_t  day;           // 1-31
    uint8_t  hour;          // 0-23
    uint8_t  minute;        // 0-59
    uint8_t  second;        // 0-59
    uint8_t  dow;           // 0=Sunday, 1=Monday ... 6=Saturday (matches tm_wday)
} time_sync_payload_t;

// Sensor data payload (CMD_SENSOR_DATA) - sensor node readings
typedef struct __attribute__((packed)) {
    float totalLiters;      // Cumulative water volume in liters
    float flowRate;         // Current flow rate in L/min
    float temperature;      // Water temperature in °C (DS18B20)
    uint8_t isRaining;      // 1 = raining (digital rain sensor)
    uint16_t rainAnalog;    // Analog rain sensor raw value (0-4095)
    float windSpeed;        // Wind speed in m/s (0-60)
} sensor_data_payload_t;

// ============================================================
// Utility Functions
// ============================================================

static inline uint8_t calc_checksum(const uint8_t* data, uint8_t len) {
    uint8_t cs = 0;
    for (uint8_t i = 0; i < len; i++) {
        cs ^= data[i];
    }
    return cs;
}


