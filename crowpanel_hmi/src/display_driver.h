/*
 * display_driver.h - Display and touch driver for CrowPanel 7" ESP32-P4
 * Uses ESP32_Display_Panel library for MIPI DSI (EK79007) + GT911 touch
 *
 * Usage in main.cpp:
 *   #include "display_driver.h"
 *   display_init();   // Call after Serial.begin(), before LVGL UI init
 *   // Then create LVGL widgets with display_lock()/display_unlock()
 *   // LVGL timer runs automatically in a FreeRTOS background task
 */

#pragma once

#include <Arduino.h>
#include "board_config.h"
#include <ESP_Panel_Library.h>
#include <lvgl.h>

// ============================================================
// LVGL Port Configuration
// ============================================================
#define LVGL_PORT_DISP_BUFFER_NUM   (2)
#define LVGL_PORT_TASK_PRIORITY     (2)
#define LVGL_PORT_TASK_STACK_SIZE   (8192)
#define LVGL_PORT_TICK_PERIOD_MS    (2)

// ============================================================
// Screen Sleep Configuration
// ============================================================
#define DISPLAY_SLEEP_TIMEOUT_MS    (60000)  // Turn off backlight after 60s of no touch

// ============================================================
// Global board pointer (initialized by display_init)
// ============================================================
extern Board* panel_board;

// ============================================================
// Functions
// ============================================================

/**
 * Initialize display, touch, and LVGL.
 * Creates a FreeRTOS task for LVGL timer processing.
 * Returns true on success.
 */
bool display_init(void);

/**
 * Lock LVGL mutex before accessing LVGL objects.
 * timeout_ms: -1 for infinite wait
 */
bool display_lock(int timeout_ms);

/**
 * Unlock LVGL mutex after accessing LVGL objects.
 */
void display_unlock(void);

/**
 * Set backlight brightness (0-255).
 */
void display_set_brightness(uint8_t brightness);

/**
 * Returns true if the display is currently asleep (backlight off).
 */
bool display_is_asleep(void);

/**
 * Reset the inactivity timer (e.g. call when UART data arrives).
 */
void display_reset_activity(void);

