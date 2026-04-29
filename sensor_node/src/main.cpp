/*
 * ESP32-C3 Sensor Node
 * Reads water meter, water temperature, and rain sensor.
 * Sends sensor data to Wroom via UART every 2 seconds using framed protocol.
 *
 * UART1: TX=GPIO6 → Wroom GPIO14 (RX)
 */

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "protocol.h"

// ---- Pin definitions ----
#define FLOW_PIN    3
#define DS18B20_PIN 4
#define RAIN_DO_PIN 5
#define RAIN_AO_PIN 0

// ---- DS18B20 ----
OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);

// ---- Flow sensor ----
volatile uint32_t pulseCount = 0;
float totalLiters = 0;

void IRAM_ATTR onPulse() {
    pulseCount++;
}

// ---- Sensor data ----
sensor_data_payload_t sensorData;

// ---- Read all sensors ----
void readSensors(uint32_t intervalMs) {
    // DS18B20
    ds18b20.requestTemperatures();
    sensorData.temperature = ds18b20.getTempCByIndex(0);

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
    Serial.printf("Temp: %.2f C | Total: %.3f L | Flow: %.2f L/min | Rain: %s | RainRaw: %d\n",
                  sensorData.temperature,
                  sensorData.totalLiters,
                  sensorData.flowRate,
                  sensorData.isRaining ? "YES" : "NO",
                  sensorData.rainAnalog);
}

void setup() {
    Serial.begin(115200);

    // UART1 to Wroom
    Serial1.begin(UART_BAUD_RATE, SERIAL_8N1, SENSOR_NODE_UART_RX, SENSOR_NODE_UART_TX);

    // Flow sensor
    pinMode(FLOW_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(FLOW_PIN), onPulse, FALLING);

    // Rain sensor
    pinMode(RAIN_DO_PIN, INPUT);

    // DS18B20
    ds18b20.begin();

    Serial.println("Sensor Node ready (UART mode)");
}

void loop() {
    static uint32_t lastSend = 0;
    if (millis() - lastSend >= 2000) {
        lastSend = millis();
        readSensors(2000);
        sendSensorPacket();
        printSensors();
    }
    delay(10);
}
