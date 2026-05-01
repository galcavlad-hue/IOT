/**
 * lv_conf.h - LVGL Configuration for CrowPanel 7" (1024x600)
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Display resolution */
#define LV_HOR_RES_MAX          1024
#define LV_VER_RES_MAX          600

/* Color depth: 16-bit (RGB565) */
#define LV_COLOR_DEPTH          16
#define LV_COLOR_16_SWAP        0

/* Memory */
#define LV_MEM_CUSTOM           1
#define LV_MEM_CUSTOM_INCLUDE   <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC     malloc
#define LV_MEM_CUSTOM_FREE      free
#define LV_MEM_CUSTOM_REALLOC   realloc

/* Display buffer size (1/10 of screen) */
#define LV_DISP_DEF_REFR_PERIOD    16
#define LV_INDEV_DEF_READ_PERIOD    30

/* Drawing */
#define LV_DRAW_COMPLEX         1
#define LV_SHADOW_CACHE_SIZE    0

/* Logging */
#define LV_USE_LOG              0

/* Tick */
#define LV_TICK_CUSTOM          1
#define LV_TICK_CUSTOM_INCLUDE  <Arduino.h>
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

/* Default font */
#define LV_FONT_MONTSERRAT_12  1
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_16  1
#define LV_FONT_MONTSERRAT_18  1
#define LV_FONT_MONTSERRAT_20  1
#define LV_FONT_MONTSERRAT_22  1
#define LV_FONT_MONTSERRAT_24  1
#define LV_FONT_MONTSERRAT_28  1
#define LV_FONT_MONTSERRAT_32  1
#define LV_FONT_MONTSERRAT_36  1
#define LV_FONT_DEFAULT         &lv_font_montserrat_18

/* Widgets */
#define LV_USE_ARC              1
#define LV_USE_BAR              1
#define LV_USE_BTN              1
#define LV_USE_BTNMATRIX        1
#define LV_USE_CANVAS           0
#define LV_USE_CHECKBOX         1
#define LV_USE_DROPDOWN         1
#define LV_USE_IMG              1
#define LV_USE_LABEL            1
#define LV_USE_LINE             1
#define LV_USE_ROLLER           1
#define LV_USE_SLIDER           1
#define LV_USE_SWITCH           1
#define LV_USE_TEXTAREA         1
#define LV_USE_TABLE            1

/* Extra widgets */
#define LV_USE_ANIMIMG          0
#define LV_USE_CALENDAR         0
#define LV_USE_CHART            0
#define LV_USE_COLORWHEEL       0
#define LV_USE_IMGBTN           0
#define LV_USE_KEYBOARD         1
#define LV_USE_LED              1
#define LV_USE_LIST             1
#define LV_USE_MENU             0
#define LV_USE_METER            0
#define LV_USE_MSGBOX           1
#define LV_USE_SPAN             0
#define LV_USE_SPINBOX          0
#define LV_USE_SPINNER          1
#define LV_USE_TABVIEW          1
#define LV_USE_TILEVIEW         0
#define LV_USE_WIN              0

/* Themes */
#define LV_USE_THEME_DEFAULT    1
#define LV_THEME_DEFAULT_DARK   1

/* GPU - Not applicable for ESP32-P4 MIPI DSI */
#define LV_USE_GPU_STM32_DMA2D  0
#define LV_USE_GPU_NXP_PXP      0
#define LV_USE_GPU_NXP_VG_LITE  0

#endif /* LV_CONF_H */
