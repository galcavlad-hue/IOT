idf_component_register(
    SRCS "main.cpp" "display_driver.cpp" "ui.cpp"
    INCLUDE_DIRS "."
    REQUIRES lvgl esp_lcd_mipi_dsi esp_lcd_ili9341 esp_lcd_st7789 
             driver esp_http_client esp_wifi esp_event
)
