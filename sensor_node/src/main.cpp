/*
 * ESP32-C3 Sensor Node
 * Reads water meter, water temperature, rain sensor, and wind speed.
 * Sends sensor data to Wroom via UART every 2 seconds using framed protocol.
 *
 * UART1: TX=GPIO6 → Wroom GPIO14 (RX)
 * I2C:   SDA=GPIO8, SCL=GPIO9 → ADS1115 (addr 0x49, ADDR→VDD)
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include "protocol.h"

// ---- Pin definitions ----
#define FLOW_PIN    3
#define DS18B20_PIN 4
#define RAIN_DO_PIN 5
#define RAIN_AO_PIN 0
#define I2C_SDA     8
#define I2C_SCL     9

// ---- ADS1115 (address 0x49: ADDR pin tied to VDD) ----
// Channel A0: Wind speed sensor (0-5V output, 0-60 m/s)
Adafruit_ADS1115 ads;
bool adsOnline = false;
uint32_t lastAdsProbe = 0;
#define ADS_REPROBE_INTERVAL_MS 30000  // Re-probe every 30s if offline

// ---- DS18B20 (non-blocking reads) ----
OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);
bool ds18b20ConversionPending = false;
uint32_t ds18b20RequestTime = 0;
#define DS18B20_CONVERSION_MS 750  // 12-bit resolution takes 750ms

// ---- Flow sensor ----
volatile uint32_t pulseCount = 0;
float totalLiters = 0;

void IRAM_ATTR onPulse() {
    pulseCount++;
}

// ---- Persistence (save totalLiters to flash) ----
Preferences prefs;
float lastSavedLiters = 0;
uint32_t lastLitersSave = 0;
#define LITERS_SAVE_INTERVAL_MS 300000  // Save every 5 minutes
#define LITERS_SAVE_DELTA       100.0f  // Or when 100L accumulated

// ---- Sensor data ----
sensor_data_payload_t sensorData;

// ---- Read all sensors (non-blocking) ----
void readSensors(uint32_t intervalMs) {
    // DS18B20: read result from previous cycle's request
    if (ds18b20ConversionPending &&
        (millis() - ds18b20RequestTime >= DS18B20_CONVERSION_MS)) {
        float temp = ds18b20.getTempCByIndex(0);
        if (temp != DEVICE_DISCONNECTED_C && temp > -50.0f && temp < 85.0f) {
            sensorData.temperature = temp;
        }
        ds18b20ConversionPending = false;
    }

    // DS18B20: request new conversion (will be ready next cycle)
    if (!ds18b20ConversionPending) {
        ds18b20.requestTemperatures();
        ds18b20ConversionPending = true;
        ds18b20RequestTime = millis();
    }

    // Flow
    noInterrupts();
    uint32_t pulses = pulseCount;
    pulseCount = 0;
    interrupts();

    totalLiters += pulses / 450.0;
    sensorData.totalLiters = totalLiters;
    sensorData.flowRate = (pulses / 450.0) * (60000.0 / intervalMs); // L/min

    // Rain
    sensorData.isRaining = (digitalRead(RAIN_DO_PIN) == LOW) ? 1 : 0;
    sensorData.rainAnalog = analogRead(RAIN_AO_PIN);

    // Wind speed via ADS1115 channel A0
    // Sensor output: 1-5V (1V = 0 m/s, 5V = 60 m/s, <1V = sensor error)
    // Gain = GAIN_TWOTHIRDS: ±6.144V range, 1 bit = 0.1875mV
    if (adsOnline) {
        int16_t rawAdc = ads.readADC_SingleEnded(0);
        float voltage = ads.computeVolts(rawAdc);
        if (voltage < 1.0f) {
            sensorData.windSpeed = 0.0f;
        } else {
            sensorData.windSpeed = ((voltage - 1.0f) / 4.0f) * 60.0f;
        }
    } else {
        sensorData.windSpeed = 0.0f;
    }

    // Persist totalLiters periodically to survive power loss
    uint32_t now = millis();
    if ((now - lastLitersSave >= LITERS_SAVE_INTERVAL_MS) ||
        (totalLiters - lastSavedLiters >= LITERS_SAVE_DELTA)) {
        prefs.begin("sensor", false);
        prefs.putFloat("totalL", totalLiters);
        prefs.end();
        lastSavedLiters = totalLiters;
        lastLitersSave = now;
    }
}

// ---- ADS1115 re-probe (recovers from I2C bus glitch) ----
void probeAds1115() {
    if (adsOnline) return;
    if (millis() - lastAdsProbe < ADS_REPROBE_INTERVAL_MS) return;
    lastAdsProbe = millis();

    Wire.beginTransmission(0x49);
    if (Wire.endTransmission() == 0) {
        if (ads.begin(0x49, &Wire)) {
            ads.setGain(GAIN_TWOTHIRDS);
            adsOnline = true;
            Serial.println("ADS1115 recovered at 0x49");
        }
    }
}

// ---- Send framed UART packet ----
void sendSensorPacket() {
    uint8_t packet[MAX_PACKET_SIZE + 5];
    uint8_t idx = 0;
    uint8_t payloadLen = sizeof(sensor_data_payload_t);

    packet[idx++] = PACKET_START_BYTE;
    packet[idx++] = CMD_SENSOR_DATA;
    packet[idx++] = payloadLen;

    uint8_t checksum = CMD_SENSOR_DATA ^ payloadLen;
    const uint8_t* payload = (const uint8_t*)&sensorData;
    for (uint8_t i = 0; i < payloadLen; i++) {
        packet[idx++] = payload[i];
        checksum ^= payload[i];
    }

    packet[idx++] = checksum;
    packet[idx++] = PACKET_END_BYTE;

    Serial1.write(packet, idx);
}

// ---- Serial print ----
void printSensors() {
    Serial.printf("Temp: %.2f C | Total: %.3f L | Flow: %.2f L/min | Rain: %s | RainRaw: %d | Wind: %.1f m/s\n",
                  sensorData.temperature,
                  sensorData.totalLiters,
                  sensorData.flowRate,
                  sensorData.isRaining ? "YES" : "NO",
                  sensorData.rainAnalog,
                  sensorData.windSpeed);
}

void setup() {
    Serial.begin(115200);

    // Hardware watchdog: reset if loop stalls > 10 seconds
    esp_task_wdt_init(10, true);
    esp_task_wdt_add(NULL);

    // UART1 to Wroom
    Serial1.begin(UART_BAUD_RATE, SERIAL_8N1, SENSOR_NODE_UART_RX, SENSOR_NODE_UART_TX);

    // I2C for ADS1115
    Wire.begin(I2C_SDA, I2C_SCL);

    // ADS1115 at address 0x49 (ADDR pin → VDD)
    if (ads.begin(0x49, &Wire)) {
        ads.setGain(GAIN_TWOTHIRDS);  // ±6.144V range (covers 0-5V sensor)
        adsOnline = true;
        Serial.println("ADS1115 online at 0x49");
    } else {
        adsOnline = false;
        Serial.println("WARNING: ADS1115 not found at 0x49!");
    }

    // Flow sensor
    pinMode(FLOW_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(FLOW_PIN), onPulse, FALLING);

    // Rain sensor
    pinMode(RAIN_DO_PIN, INPUT);

    // DS18B20 (non-blocking mode)
    ds18b20.begin();
    ds18b20.setWaitForConversion(false);

    // Restore totalLiters from flash
    prefs.begin("sensor", true);
    totalLiters = prefs.getFloat("totalL", 0.0f);
    lastSavedLiters = totalLiters;
    prefs.end();
    Serial.printf("Restored totalLiters: %.3f L\n", totalLiters);

    Serial.println("Sensor Node ready (UART mode)");
}

void loop() {
    // Feed watchdog
    esp_task_wdt_reset();

    static uint32_t lastSend = 0;
    if (millis() - lastSend >= 2000) {
        lastSend = millis();
        readSensors(2000);
        sendSensorPacket();
        printSensors();
    }

    // Re-probe ADS1115 if offline
    if (!adsOnline) probeAds1115();

    delay(10);
}
