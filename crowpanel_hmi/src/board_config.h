/*
 * board_config.h - CrowPanel Advanced 7" ESP32-P4 Pin Configuration
 * 1024x600 MIPI DSI display with EK79007 LCD + GT911 touch
 */

#pragma once

// GPIO pins for GT911 touch panel
#define Touch_GPIO_RST      (40)
#define Touch_GPIO_INT      (42)

// GPIO pins for I2C (touch GT911)
#define I2C_GPIO_SCL        (46)
#define I2C_GPIO_SDA        (45)

// GPIO pins for display backlight
#define LCD_GPIO_BLIGHT     (31)
#define BLIGHT_PWM_Hz       (30000)
#define BLIGHT_ON_LEVEL     (1)

// GPIO pins for display
#define LCD_GPIO_RST        (41)

// Panel parameters
#define V_size              (600)
#define H_size              (1024)

// MIPI DSI timing
#define LCD_CLK_MHZ         (52)
#define LCD_HPW             (10)
#define LCD_HBP             (160)
#define LCD_HFP             (160)
#define LCD_VPW             (1)
#define LCD_VBP             (23)
#define LCD_VFP             (12)
