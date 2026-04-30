/*
 * esp_panel_board_custom_conf.h
 * ESP32_Display_Panel library configuration for CrowPanel Advanced 7" ESP32-P4
 * MIPI DSI with EK79007 LCD + GT911 touch on I2C
 *
 * Place this file in the src/ folder so the ESP32_Display_Panel library picks it up.
 */

#pragma once

#include "board_config.h"

// Enable custom board (NOT a built-in board)
#define ESP_PANEL_BOARD_USE_CUSTOM              (1)

// ============================================================
// LCD Configuration - EK79007 via MIPI DSI
// ============================================================
#define ESP_PANEL_BOARD_USE_LCD                 (1)
#define ESP_PANEL_BOARD_LCD_NAME                "EK79007"
#define ESP_PANEL_BOARD_LCD_WIDTH               (H_size)
#define ESP_PANEL_BOARD_LCD_HEIGHT              (V_size)
#define ESP_PANEL_BOARD_LCD_COLOR_BITS          (16)     // RGB565
#define ESP_PANEL_BOARD_LCD_BUS_TYPE            (4)      // ESP_PANEL_BUS_TYPE_MIPI_DSI

// MIPI DSI lanes
#define ESP_PANEL_BOARD_LCD_MIPI_DSI_LANE_NUM       (2)
#define ESP_PANEL_BOARD_LCD_MIPI_DSI_LANE_RATE_MBPS (1000)

// MIPI DPI timing
#define ESP_PANEL_BOARD_LCD_MIPI_DPI_CLK_MHZ        (LCD_CLK_MHZ)
#define ESP_PANEL_BOARD_LCD_MIPI_DPI_PIXEL_BITS     (16)
#define ESP_PANEL_BOARD_LCD_MIPI_DPI_HPW            (LCD_HPW)
#define ESP_PANEL_BOARD_LCD_MIPI_DPI_HBP            (LCD_HBP)
#define ESP_PANEL_BOARD_LCD_MIPI_DPI_HFP            (LCD_HFP)
#define ESP_PANEL_BOARD_LCD_MIPI_DPI_VPW            (LCD_VPW)
#define ESP_PANEL_BOARD_LCD_MIPI_DPI_VBP            (LCD_VBP)
#define ESP_PANEL_BOARD_LCD_MIPI_DPI_VFP            (LCD_VFP)

// LDO3 for MIPI D-PHY power (2.5V)
#define ESP_PANEL_BOARD_LCD_MIPI_PHY_LDO_ID         (3)

// LCD reset pin
#define ESP_PANEL_BOARD_LCD_RST_IO              (LCD_GPIO_RST)
#define ESP_PANEL_BOARD_LCD_RST_LEVEL           (0)

// ============================================================
// Touch Configuration - GT911 via I2C
// ============================================================
#define ESP_PANEL_BOARD_USE_TOUCH               (1)
#define ESP_PANEL_BOARD_TOUCH_NAME              "GT911"
#define ESP_PANEL_BOARD_TOUCH_BUS_TYPE          (1)      // ESP_PANEL_BUS_TYPE_I2C
#define ESP_PANEL_BOARD_TOUCH_I2C_ADDRESS       (0x5D)
#define ESP_PANEL_BOARD_TOUCH_I2C_CLK_HZ       (400000)
#define ESP_PANEL_BOARD_TOUCH_I2C_SCL_IO       (I2C_GPIO_SCL)
#define ESP_PANEL_BOARD_TOUCH_I2C_SDA_IO       (I2C_GPIO_SDA)
#define ESP_PANEL_BOARD_TOUCH_I2C_PULL_UP      (1)      // LDO4 provides 3.3V for pull-up
#define ESP_PANEL_BOARD_TOUCH_RST_IO           (Touch_GPIO_RST)
#define ESP_PANEL_BOARD_TOUCH_RST_LEVEL        (0)
#define ESP_PANEL_BOARD_TOUCH_INT_IO           (Touch_GPIO_INT)
#define ESP_PANEL_BOARD_TOUCH_INT_LEVEL        (0)

// ============================================================
// Backlight - Managed manually (not by library)
// ============================================================
#define ESP_PANEL_BOARD_USE_BACKLIGHT           (0)
