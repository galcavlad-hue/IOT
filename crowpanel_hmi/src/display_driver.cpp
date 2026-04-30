/*
 * display_driver.cpp - Display/Touch/LVGL initialization for CrowPanel 7" ESP32-P4
 * Uses ESP32_Display_Panel Board class for MIPI DSI + GT911 touch
 * Provides LVGL display/input drivers and a background FreeRTOS task
 */

#include "display_driver.h"

// ============================================================
// Globals
// ============================================================
Board* panel_board = nullptr;

static LCD* lcd = nullptr;
static Touch* touch = nullptr;
static SemaphoreHandle_t lvgl_mutex = nullptr;
static TaskHandle_t lvgl_task_handle = nullptr;

// LVGL v8 driver structs
static lv_disp_draw_buf_t draw_buf;
static lv_color_t* buf1 = nullptr;
static lv_color_t* buf2 = nullptr;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

// Screen sleep state
static volatile uint32_t last_activity_ms = 0;
static bool screen_asleep = false;
static uint8_t active_brightness = 200;  // Brightness to restore on wake

// ============================================================
// LVGL Display flush callback
// ============================================================
static IRAM_ATTR bool on_draw_finish(void* user_data) {
    lv_disp_flush_ready((lv_disp_drv_t*)user_data);
    return false;
}

static void flush_callback(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_map) {
    lcd->drawBitmap(area->x1, area->y1, area->x2 - area->x1 + 1,
                    area->y2 - area->y1 + 1, (const uint8_t*)color_map);
}

// ============================================================
// LVGL Touch read callback
// ============================================================
static void touchpad_read(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    TouchPoint point;
    int num = touch->readPoints(&point, 1);

    if (num > 0) {
        if (screen_asleep) {
            // Wake up the screen — consume this touch (don't pass to UI)
            screen_asleep = false;
            last_activity_ms = millis();
            display_set_brightness(active_brightness);
            data->state = LV_INDEV_STATE_RELEASED;
            return;
        }
        last_activity_ms = millis();
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = point.x;
        data->point.y = point.y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ============================================================
// LVGL background task (FreeRTOS)
// ============================================================
static void lvgl_task(void* arg) {
    while (true) {
        if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            lv_timer_handler();

            // Check for screen sleep timeout
            if (!screen_asleep &&
                (millis() - last_activity_ms) >= DISPLAY_SLEEP_TIMEOUT_MS) {
                screen_asleep = true;
                display_set_brightness(0);
            }

            xSemaphoreGive(lvgl_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(LVGL_PORT_TICK_PERIOD_MS));
    }
}

// ============================================================
// Backlight PWM
// ============================================================
void display_set_brightness(uint8_t brightness) {
    analogWrite(LCD_GPIO_BLIGHT, brightness);
    if (brightness > 0) {
        active_brightness = brightness;
    }
}

bool display_is_asleep(void) {
    return screen_asleep;
}

void display_reset_activity(void) {
    last_activity_ms = millis();
    if (screen_asleep) {
        screen_asleep = false;
        display_set_brightness(active_brightness);
    }
}

// ============================================================
// display_init - Main initialization
// ============================================================
bool display_init(void) {
    // Create LVGL mutex
    lvgl_mutex = xSemaphoreCreateMutex();
    if (!lvgl_mutex) return false;

    // Initialize Board (LCD + Touch via ESP32_Display_Panel)
    panel_board = new Board();
    if (!panel_board->init()) {
        Serial.println("Board init failed!");
        return false;
    }
    if (!panel_board->begin()) {
        Serial.println("Board begin failed!");
        return false;
    }

    lcd = panel_board->getLCD();
    touch = panel_board->getTouch();

    // Register DMA completion callback for non-blocking flush
    if (lcd) {
        lcd->attachDrawBitmapFinishCallback(on_draw_finish, (void*)&disp_drv);
    }

    // Initialize backlight
    pinMode(LCD_GPIO_BLIGHT, OUTPUT);
    display_set_brightness(200);
    last_activity_ms = millis();

    // Initialize LVGL
    lv_init();

    // Allocate draw buffers — use PSRAM for double buffering
    uint32_t buf_size = H_size * 40;  // 40 rows per buffer
    buf1 = (lv_color_t*)heap_caps_malloc(buf_size * sizeof(lv_color_t),
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    buf2 = (lv_color_t*)heap_caps_malloc(buf_size * sizeof(lv_color_t),
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (!buf1 || !buf2) {
        Serial.println("PSRAM alloc failed, using internal RAM");
        if (buf1) free(buf1);
        if (buf2) free(buf2);
        buf_size = H_size * 10;
        buf1 = (lv_color_t*)malloc(buf_size * sizeof(lv_color_t));
        buf2 = nullptr;
    }

    lv_disp_draw_buf_init(&draw_buf, buf1, buf2,
                           buf2 ? H_size * 40 : buf_size);

    // Display driver
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = H_size;
    disp_drv.ver_res = V_size;
    disp_drv.flush_cb = flush_callback;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // Touch input driver
    if (touch) {
        lv_indev_drv_init(&indev_drv);
        indev_drv.type = LV_INDEV_TYPE_POINTER;
        indev_drv.read_cb = touchpad_read;
        lv_indev_drv_register(&indev_drv);
    }

    // Start LVGL background task
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", LVGL_PORT_TASK_STACK_SIZE,
                            NULL, LVGL_PORT_TASK_PRIORITY, &lvgl_task_handle, 1);

    Serial.println("Display + LVGL initialized");
    return true;
}

// ============================================================
// Mutex helpers
// ============================================================
bool display_lock(int timeout_ms) {
    if (!lvgl_mutex) return false;
    TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(lvgl_mutex, ticks) == pdTRUE;
}

void display_unlock(void) {
    if (lvgl_mutex) {
        xSemaphoreGive(lvgl_mutex);
    }
}
