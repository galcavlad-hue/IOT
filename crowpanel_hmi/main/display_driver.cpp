/*
 * display_driver.cpp - ESP-IDF Display & Touch Driver Implementation
 * Initializes RGB LCD and GT911 touch for CrowPanel 7" (1024x600)
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "lvgl.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_touch_gt911.h"
#include "display_driver.h"
#include "board_config.h"
#include "esp_panel_board_custom_conf.h"

static const char *TAG = "DISPLAY_DRIVER";

#define SCREEN_WIDTH    1024
#define SCREEN_HEIGHT   600

// ============================================================
// LVGL Globals
// ============================================================
static lv_disp_drv_t disp_drv;
static lv_disp_draw_buf_t draw_buf;
static lv_indev_drv_t indev_drv;
static SemaphoreHandle_t lvgl_mux = nullptr;
static TaskHandle_t lvgl_task_handle = nullptr;
static esp_lcd_touch_handle_t tp = nullptr;
static esp_lcd_panel_handle_t lcd_panel = nullptr;

// ============================================================
// LCD Callbacks (called by DSI driver, forward to LVGL)
// ============================================================

static void flush_callback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    // With RGB interface, the display updates automatically
    // This callback just marks the flush as complete
    lv_disp_flush_ready(drv);
}

static void touchpad_read(lv_indev_drv_t *indev, lv_indev_data_t *data)
{
    if (!tp) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    
    // Read touch points from GT911
    uint16_t x, y;
    uint8_t cnt = 0;
    
    esp_err_t ret = esp_lcd_touch_read_data(tp);
    if (ret != ESP_OK) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    
    // Get the number of touch points
    if (esp_lcd_touch_get_coordinates(tp, &x, &y, nullptr, &cnt, 1) > 0 && cnt > 0) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

// ============================================================
// LVGL Tick Timer Task (runs every LVGL_PORT_TICK_PERIOD_MS)
// ============================================================

static void lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task started");
    
    while (1) {
        // Acquire lock
        if (xSemaphoreTake(lvgl_mux, portMAX_DELAY)) {
            lv_task_handler();
            xSemaphoreGive(lvgl_mux);
        }
        vTaskDelay(pdMS_TO_TICKS(LVGL_PORT_TICK_PERIOD_MS));
    }
}

// ============================================================
// I2C Initialization (for GT911 touch)
// ============================================================

static esp_err_t i2c_init(void)
{
    ESP_LOGI(TAG, "Initializing I2C for GT911 touch");
    
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_GPIO_SDA,
        .scl_io_num = I2C_GPIO_SCL,
        .master = {
            .clk_speed = 400000,  // 400kHz
        },
    };
    
    esp_err_t ret = i2c_param_config(I2C_NUM_0, &conf);
    if (ret != ESP_OK) return ret;
    
    ret = i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);
    if (ret != ESP_OK) return ret;
    
    ESP_LOGI(TAG, "I2C initialized (SDA=%d, SCL=%d)", I2C_GPIO_SDA, I2C_GPIO_SCL);
    return ESP_OK;
}

// ============================================================
// LCD Backlight Control (PWM)
// ============================================================

static esp_err_t backlight_init(void)
{
    ESP_LOGI(TAG, "Initializing backlight PWM on GPIO %d", LCD_GPIO_BLIGHT);
    
    // Configure LEDC for backlight PWM
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = BLIGHT_PWM_Hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    
    esp_err_t ret = ledc_timer_config(&ledc_timer);
    if (ret != ESP_OK) return ret;
    
    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = LCD_GPIO_BLIGHT,
        .duty = 255,  // Max brightness
        .hpoint = 0,
    };
    
    ret = ledc_channel_config(&ledc_channel);
    if (ret != ESP_OK) return ret;
    
    // Set to max brightness initially
    ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 255);
    if (ret != ESP_OK) return ret;
    
    ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    
    ESP_LOGI(TAG, "Backlight PWM initialized, brightness: 255");
    return ret;
}

// ============================================================
// Public Functions
// ============================================================

bool display_init(void)
{
    ESP_LOGI(TAG, "Initializing CrowPanel 7\" display (1024x600 RGB)");
    
    // Step 1: Create LVGL mutex
    lvgl_mux = xSemaphoreCreateMutex();
    if (!lvgl_mux) {
        ESP_LOGE(TAG, "Failed to create LVGL mutex");
        return false;
    }
    ESP_LOGI(TAG, "LVGL mutex created");
    
    // Step 2: Initialize I2C for touch (GT911)
    if (i2c_init() != ESP_OK) {
        ESP_LOGE(TAG, "I2C initialization failed");
        return false;
    }
    
    // Step 3: Initialize backlight PWM
    if (backlight_init() != ESP_OK) {
        ESP_LOGE(TAG, "Backlight initialization failed");
        return false;
    }
    
    // Step 4: Initialize LVGL
    lv_init();
    ESP_LOGI(TAG, "LVGL initialized");
    
    // Step 5: Allocate LVGL draw buffers (from PSRAM or internal RAM)
    size_t buf_size = SCREEN_WIDTH * 40 * sizeof(lv_color_t);
    lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    lv_color_t *buf2 = NULL;
    
    if (!buf1) {
        ESP_LOGW(TAG, "PSRAM allocation failed, trying internal RAM");
        buf1 = (lv_color_t *)malloc(buf_size);
    }
    
    buf2 = (lv_color_t *)malloc(buf_size);
    
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "Display buffer allocation failed");
        return false;
    }
    
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, SCREEN_WIDTH * 40);
    ESP_LOGI(TAG, "Display buffers allocated: %zu bytes each", buf_size);
    
    // Step 6: Initialize display driver (RGB panel)
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCREEN_WIDTH;
    disp_drv.ver_res = SCREEN_HEIGHT;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.flush_cb = flush_callback;
    
    lv_disp_drv_register(&disp_drv);
    ESP_LOGI(TAG, "Display driver registered: %dx%d", SCREEN_WIDTH, SCREEN_HEIGHT);
    
    // Step 7: Register touch input device (GT911)
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    lv_indev_drv_register(&indev_drv);
    
    // Initialize GT911 touch driver
    esp_lcd_touch_config_t tp_cfg = {
        .i2c_num = I2C_NUM_0,
        .i2c_addr = 0x5D,  // GT911 I2C address (can be 0x14 or 0x5D depending on INT pin)
        .rst_gpio_num = Touch_GPIO_RST,
        .int_gpio_num = Touch_GPIO_INT,
        .levels = {0, 0},
        .flags = {0, 0},
    };
    
    if (esp_lcd_touch_new_i2c_gt911(&tp_cfg, &tp) == ESP_OK) {
        ESP_LOGI(TAG, "GT911 touch driver initialized");
    } else {
        ESP_LOGW(TAG, "GT911 touch driver initialization failed (continuing without touch)");
        tp = nullptr;
    }
    
    // Step 8: Create LVGL timer task
    if (xTaskCreate(lvgl_task, "lvgl_task", LVGL_PORT_TASK_STACK_SIZE,
                    nullptr, LVGL_PORT_TASK_PRIORITY, &lvgl_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL task");
        return false;
    }
    
    ESP_LOGI(TAG, "Display initialized successfully");
    return true;
}

void display_lock(void)
{
    if (lvgl_mux) {
        xSemaphoreTake(lvgl_mux, portMAX_DELAY);
    }
}

void display_unlock(void)
{
    if (lvgl_mux) {
        xSemaphoreGive(lvgl_mux);
    }
}

void display_set_brightness(uint8_t brightness)
{
    // brightness: 0-255 mapped to PWM duty cycle
    uint32_t duty = brightness;  // 0-255 maps directly to LEDC 8-bit duty
    
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    
    ESP_LOGI(TAG, "Backlight brightness set to %d", brightness);
}

void display_sleep(bool sleep)
{
    if (sleep) {
        // Turn backlight off (duty = 0)
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        ESP_LOGI(TAG, "Display entering sleep mode");
    } else {
        // Turn backlight on to full brightness
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 255);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        ESP_LOGI(TAG, "Display waking up");
    }
}
