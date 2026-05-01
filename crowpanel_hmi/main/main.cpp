/*
 * CrowPanel 7" HMI - Main Firmware (ESP32-P4 with ESP-IDF)
 *
 * Receives ALL data from ESP32 Wroom relay controller via UART
 * (sensor data, relay status, time sync)
 * Displays everything on 1024x600 LVGL touch interface
 *
 * UART1: TX=IO47, RX=IO48 (connected to Wroom UART2)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/uart.h"
#include "lvgl.h"
#include "display_driver.h"
#include "ui.h"
#include "protocol.h"

static const char *TAG = "CROWPANEL_MAIN";

// ============================================================
// Display Configuration
// ============================================================
#define SCREEN_WIDTH    1024
#define SCREEN_HEIGHT   600

// ============================================================
// UART Configuration for Communication with Wroom
// ============================================================
#define UART_NUM        UART_NUM_1
#define UART_TX_PIN     WROOM_PANEL_UART_TX  // IO17 (from protocol.h)
#define UART_RX_PIN     WROOM_PANEL_UART_RX  // IO16 (from protocol.h)
#define UART_BAUD_RATE  UART_BAUD_RATE      // 115200 (from protocol.h)
#define UART_BUF_SIZE   (1024 * 2)

// ============================================================
// UART Receive State
// ============================================================
static uint8_t uart_rx_buf[256];
static uint16_t uart_rx_idx = 0;
static bool uart_packet_started = false;
static QueueHandle_t uart_queue = nullptr;

// ============================================================
// Time State
// ============================================================
static bool time_synced = false;
static uint32_t time_base_ms = 0;
static uint8_t base_hour = 6;
static uint8_t base_minute = 0;
static uint8_t base_dow = 0;

// ============================================================
// UART ISR Handler
// ============================================================

static void uart_isr_handler(void *arg)
{
    // Handle UART events
    uart_event_t event;
    BaseType_t high_task_woken = pdFALSE;
    
    // This is called from ISR context
    // TODO: Implement UART interrupt handling
}

// ============================================================
// UART Event Task
// ============================================================

static void uart_event_task(void *arg)
{
    ESP_LOGI(TAG, "UART event task started");
    
    while (1) {
        // Read available bytes from UART
        int len = uart_read_bytes(UART_NUM, uart_rx_buf, sizeof(uart_rx_buf), portMAX_DELAY);
        
        if (len > 0) {
            // TODO: Parse protocol packets from uart_rx_buf
            ESP_LOGI(TAG, "UART RX: %d bytes", len);
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ============================================================
// UART Initialization
// ============================================================

static bool uart_init(void)
{
    ESP_LOGI(TAG, "Initializing UART%d: TX=%d RX=%d", UART_NUM, UART_TX_PIN, UART_RX_PIN);
    
    // Configure UART parameters
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    
    // Install UART driver
    esp_err_t ret = uart_driver_install(UART_NUM, UART_BUF_SIZE, UART_BUF_SIZE, 0, nullptr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Configure UART
    ret = uart_param_config(UART_NUM, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Set UART pins
    ret = uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Create UART event task
    if (xTaskCreate(uart_event_task, "uart_task", 4096, nullptr, 5, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UART task");
        return false;
    }
    
    ESP_LOGI(TAG, "UART initialized successfully");
    return true;
}

// ============================================================
// Main App
// ============================================================

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "CrowPanel 7\" HMI Starting (ESP32-P4)");
    
    // Step 1: Initialize display & LVGL
    if (!display_init()) {
        ESP_LOGE(TAG, "Display initialization failed!");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Step 2: Initialize UI
    ui_init();
    
    // Step 3: Initialize UART communication with Wroom
    if (!uart_init()) {
        ESP_LOGE(TAG, "UART initialization failed!");
        return;
    }
    
    ESP_LOGI(TAG, "All systems initialized, entering main loop");
    
    // Main loop
    while (1) {
        // System is running, LVGL task handles display updates
        // UART task handles incoming data
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        ESP_LOGI(TAG, "Tick...");
    }
}
