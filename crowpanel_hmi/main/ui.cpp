/*
 * ui.cpp - LVGL User Interface Implementation (ESP-IDF version)
 * Creates and manages the touch UI for CrowPanel 7" (1024x600)
 */

#include "ui.h"
#include "display_driver.h"
#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "UI";

ui_data_t ui_data = {};
relay_cmd_cb_t on_relay_command = nullptr;
alarm_notify_cb_t on_alarm_notify = nullptr;

// ============================================================
// UI Initialization
// ============================================================

void ui_init(void)
{
    ESP_LOGI(TAG, "Initializing UI");
    
    // Create main screen
    lv_obj_t *main_screen = lv_scr_act();
    lv_obj_set_style_bg_color(main_screen, lv_color_hex(0x1a1a1a), 0);
    
    // Create a simple label for now
    lv_obj_t *label = lv_label_create(main_screen);
    lv_label_set_text(label, "CrowPanel 7\" HMI\n1024x600 MIPI DSI\nESP32-P4");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
}

// ============================================================
// Data Update Functions
// ============================================================

void ui_update_sensor_data(float temp, float humidity, float flow)
{
    ESP_LOGI(TAG, "Sensor: T=%.1f H=%.1f F=%.2f", temp, humidity, flow);
    ui_data.last_sensor_temp = temp;
    ui_data.last_sensor_humidity = humidity;
    ui_data.last_sensor_flow = flow;
}

void ui_update_relay_status(uint16_t relay_mask)
{
    ESP_LOGI(TAG, "Relay status: 0x%04x", relay_mask);
    ui_data.relay_status = relay_mask;
}

void ui_set_time(uint8_t hour, uint8_t minute, uint8_t second, uint8_t dow)
{
    ESP_LOGI(TAG, "Time sync: %02d:%02d:%02d DOW=%d", hour, minute, second, dow);
    ui_data.time_hour = hour;
    ui_data.time_minute = minute;
    ui_data.time_second = second;
    ui_data.time_dow = dow;
}

// ============================================================
// Callback Registration
// ============================================================

void ui_register_relay_callback(relay_cmd_cb_t cb)
{
    on_relay_command = cb;
    ESP_LOGI(TAG, "Relay callback registered");
}

void ui_register_alarm_callback(alarm_notify_cb_t cb)
{
    on_alarm_notify = cb;
    ESP_LOGI(TAG, "Alarm callback registered");
}

// ============================================================
// Volume Reset Check
// ============================================================

void ui_check_volume_reset(uint8_t day, uint8_t month, uint16_t year)
{
    // Placeholder for volume reset logic
    ESP_LOGI(TAG, "Daily volume check: %02d/%02d/%04d", month, day, year);
}
