/*
 * ui.cpp - LVGL User Interface Implementation
 * Creates and manages the touch UI for CrowPanel 7" (1024x600)
 */

#include "ui.h"
#include "display_driver.h"
#include <Arduino.h>
#include <stdio.h>

ui_data_t ui_data;
ui_objects_t ui_obj;
Preferences ui_prefs;
relay_cmd_cb_t on_relay_command = nullptr;
alarm_notify_cb_t on_alarm_notify = nullptr;

// Threshold roller option strings (built once in create_action_overlay)
static char flow_thr_opts[20 * 8];
static char temp_thr_opts[11 * 8];
static char wind_thr_opts[12 * 8];

// Volume tracking
static float lastTotalLiters = -1.0f; // Last totalLiters from sensor node

// Time tracking for volume reset
static int lastDay = -1;
static int lastMonth = -1;
static int lastYear = -1;

// ============================================================
// Forward declarations
// ============================================================
static void create_dashboard_tab(lv_obj_t* parent);
static void create_relays_tab(lv_obj_t* parent);
static void create_schedules_tab(lv_obj_t* parent);
static void create_actions_tab(lv_obj_t* parent);
static void create_settings_tab(lv_obj_t* parent);
static void create_rename_overlay(void);
static void create_schedule_overlay(void);
static void create_action_overlay(void);
static lv_obj_t* create_sensor_card(lv_obj_t* parent, const char* title,
                                     const char* icon, lv_color_t color);
static void refresh_schedule_list(void);
static void refresh_action_cards(void);

// Action type display names (needed early for handle_manual_relay_change)
static const char* action_type_names[] = {
    "Pipe Leak Alarm",
    "Rain Auto-Close (Shed)",
    "No Water Alarm",
    "Freezing Temperature",
    "High Wind Speed"
};

// ============================================================
// Relay command helper — tracks cmd_pending for external change detection
// ============================================================
static void relay_command(uint8_t relay, uint8_t state) {
    if (on_relay_command) on_relay_command(relay, state);
    if (relay < NUM_RELAYS) ui_data.cmd_pending |= (1 << relay);
}

// ============================================================
// Manual override logic — called when user manually toggles a relay
// (from CrowPanel UI or detected external change from Wroom)
// ============================================================
static void handle_manual_relay_change(int relay_idx, bool turning_on) {
    if (relay_idx < 0 || relay_idx >= NUM_RELAYS) return;

    if (!turning_on) {
        // Turning OFF: set manual override — relay stays off until user turns back on
        ui_data.manual_override |= (1 << relay_idx);
        return;
    }

    // Turning ON: check for active schedule to resume
    relay_schedule_t* s = &ui_data.schedules[relay_idx];
    if (s->enabled && s->currently_running) {
        uint32_t duration_ms = (uint32_t)(s->duration_hours * 3600000.0f);
        if (millis() - s->run_start_ms < duration_ms) {
            // Schedule still has time left — clear override, resume schedule
            ui_data.manual_override &= ~(1 << relay_idx);
            return;
        } else {
            // Schedule duration has passed — clean up stale state
            s->currently_running = false;
        }
    }

    // Check if any action targets this relay
    for (int i = 0; i < ui_data.num_actions && i < MAX_ACTIONS; i++) {
        action_rule_t* a = &ui_data.actions[i];
        if (!a->enabled || a->target_relay != relay_idx) continue;

        bool condition_met = false;
        switch (a->type) {
            case ACTION_WATER_FLOW_ALARM:
                condition_met = (ui_data.water_flow_rate > a->threshold);
                break;
            case ACTION_RAIN_AUTO:
                condition_met = (ui_data.rain_sensor >= a->threshold);
                break;
            case ACTION_NO_WATER: {
                bool any_sched_long = false;
                for (int j = 0; j < NUM_RELAYS; j++) {
                    if (ui_data.schedules[j].currently_running &&
                        (millis() - ui_data.schedules[j].run_start_ms > 30000)) {
                        any_sched_long = true;
                        break;
                    }
                }
                condition_met = any_sched_long && (ui_data.water_flow_rate < 0.01f);
                break;
            }
            case ACTION_FREEZING_TEMP:
                condition_met = (ui_data.water_temperature <= a->threshold);
                break;
            case ACTION_WIND_SPEED:
                condition_met = (ui_data.wind_speed > a->threshold);
                break;
            default:
                break;
        }

        if (condition_met) {
            // Action condition still active — clear override, let action manage
            ui_data.manual_override &= ~(1 << relay_idx);
            return;
        } else {
            // Action exists but condition cleared — suggest turning off
            snprintf(ui_data.suggestion_msg, sizeof(ui_data.suggestion_msg),
                     "%s: no alarm active - consider switching off",
                     action_type_names[a->type]);
            ui_data.suggestion_time_ms = millis();
        }
    }

    // Manual control — stays on until user turns off
    ui_data.manual_override |= (1 << relay_idx);
}

// ============================================================
// Styles
// ============================================================
static lv_style_t style_card;
static lv_style_t style_btn_on;
static lv_style_t style_btn_off;
static lv_style_t style_title;
static lv_style_t style_value;

static void init_styles() {
    // Card style
    lv_style_init(&style_card);
    lv_style_set_bg_color(&style_card, COLOR_CARD);
    lv_style_set_bg_opa(&style_card, LV_OPA_COVER);
    lv_style_set_radius(&style_card, 12);
    lv_style_set_pad_all(&style_card, 12);
    lv_style_set_border_width(&style_card, 1);
    lv_style_set_border_color(&style_card, COLOR_ACCENT);

    // Relay button ON style
    lv_style_init(&style_btn_on);
    lv_style_set_bg_color(&style_btn_on, COLOR_RELAY_ON);
    lv_style_set_bg_opa(&style_btn_on, LV_OPA_COVER);
    lv_style_set_radius(&style_btn_on, 10);
    lv_style_set_shadow_width(&style_btn_on, 15);
    lv_style_set_shadow_color(&style_btn_on, COLOR_RELAY_ON);
    lv_style_set_shadow_opa(&style_btn_on, LV_OPA_50);

    // Relay button OFF style
    lv_style_init(&style_btn_off);
    lv_style_set_bg_color(&style_btn_off, COLOR_RELAY_OFF);
    lv_style_set_bg_opa(&style_btn_off, LV_OPA_COVER);
    lv_style_set_radius(&style_btn_off, 10);
    lv_style_set_shadow_width(&style_btn_off, 0);

    // Title style
    lv_style_init(&style_title);
    lv_style_set_text_font(&style_title, &lv_font_montserrat_14);
    lv_style_set_text_color(&style_title, COLOR_TEXT_DIM);

    // Value style
    lv_style_init(&style_value);
    lv_style_set_text_font(&style_value, &lv_font_montserrat_28);
    lv_style_set_text_color(&style_value, COLOR_TEXT);
}

// ============================================================
// Load/Save relay names
// ============================================================
static void load_relay_names() {
    ui_prefs.begin("relay_names", true);  // read-only
    for (int i = 0; i < NUM_RELAYS; i++) {
        char key[8];
        snprintf(key, sizeof(key), "r%d", i);
        String name = ui_prefs.getString(key, "");
        if (name.length() > 0) {
            strncpy(ui_data.relay_names[i], name.c_str(), RELAY_NAME_MAX - 1);
            ui_data.relay_names[i][RELAY_NAME_MAX - 1] = '\0';
        } else {
            snprintf(ui_data.relay_names[i], RELAY_NAME_MAX, "Relay %d", i + 1);
        }
    }
    ui_prefs.end();
}

static void save_relay_name(int index) {
    ui_prefs.begin("relay_names", false);
    char key[8];
    snprintf(key, sizeof(key), "r%d", index);
    ui_prefs.putString(key, ui_data.relay_names[index]);
    ui_prefs.end();
}

// ============================================================
// Load/Save water volumes
// ============================================================
static void load_volumes() {
    ui_prefs.begin("volumes", true);
    ui_data.water_volume_day = ui_prefs.getFloat("day", 0.0f);
    ui_data.water_volume_month = ui_prefs.getFloat("month", 0.0f);
    ui_data.water_volume_year = ui_prefs.getFloat("year", 0.0f);
    lastTotalLiters = ui_prefs.getFloat("totalL", -1.0f);
    lastDay = ui_prefs.getInt("lastDay", -1);
    lastMonth = ui_prefs.getInt("lastMon", -1);
    lastYear = ui_prefs.getInt("lastYr", -1);
    ui_prefs.end();
}

static void save_volumes() {
    ui_prefs.begin("volumes", false);
    ui_prefs.putFloat("day", ui_data.water_volume_day);
    ui_prefs.putFloat("month", ui_data.water_volume_month);
    ui_prefs.putFloat("year", ui_data.water_volume_year);
    ui_prefs.putFloat("totalL", lastTotalLiters);
    ui_prefs.putInt("lastDay", lastDay);
    ui_prefs.putInt("lastMon", lastMonth);
    ui_prefs.putInt("lastYr", lastYear);
    ui_prefs.end();
}

// ============================================================
// Load/Save schedules
// ============================================================
void save_schedules() {
    ui_prefs.begin("schedules", false);
    for (int i = 0; i < NUM_RELAYS; i++) {
        char key[12];
        relay_schedule_t* s = &ui_data.schedules[i];
        snprintf(key, sizeof(key), "en%d", i);   ui_prefs.putBool(key, s->enabled);
        snprintf(key, sizeof(key), "dy%d", i);   ui_prefs.putUChar(key, s->days_mask);
        snprintf(key, sizeof(key), "sh%d", i);   ui_prefs.putUChar(key, s->start_hour);
        snprintf(key, sizeof(key), "sm%d", i);   ui_prefs.putUChar(key, s->start_minute);
        snprintf(key, sizeof(key), "du%d", i);   ui_prefs.putFloat(key, s->duration_hours);
        snprintf(key, sizeof(key), "rs%d", i);   ui_prefs.putBool(key, s->use_rain_sensor);
        snprintf(key, sizeof(key), "rp%d", i);   ui_prefs.putBool(key, s->repeat);
        snprintf(key, sizeof(key), "ri%d", i);   ui_prefs.putFloat(key, s->repeat_interval_hours);
    }
    ui_prefs.end();
}

static void load_schedules() {
    ui_prefs.begin("schedules", true);
    for (int i = 0; i < NUM_RELAYS; i++) {
        char key[12];
        relay_schedule_t* s = &ui_data.schedules[i];
        snprintf(key, sizeof(key), "en%d", i);   s->enabled = ui_prefs.getBool(key, false);
        snprintf(key, sizeof(key), "dy%d", i);   s->days_mask = ui_prefs.getUChar(key, 0);
        snprintf(key, sizeof(key), "sh%d", i);   s->start_hour = ui_prefs.getUChar(key, 6);
        snprintf(key, sizeof(key), "sm%d", i);   s->start_minute = ui_prefs.getUChar(key, 0);
        snprintf(key, sizeof(key), "du%d", i);   s->duration_hours = ui_prefs.getFloat(key, 1.0f);
        snprintf(key, sizeof(key), "rs%d", i);   s->use_rain_sensor = ui_prefs.getBool(key, false);
        snprintf(key, sizeof(key), "rp%d", i);   s->repeat = ui_prefs.getBool(key, false);
        snprintf(key, sizeof(key), "ri%d", i);   s->repeat_interval_hours = ui_prefs.getFloat(key, 4.0f);
        s->currently_running = false;
        s->run_start_ms = 0;
        s->last_repeat_ms = 0;
        s->blocked_by_rain = false;
    }
    ui_prefs.end();
}

// ============================================================
// Load/Save actions
// ============================================================
void save_actions() {
    ui_prefs.begin("actions2", false);
    ui_prefs.putInt("count", ui_data.num_actions);
    ui_prefs.putFloat("rain_thr", ui_data.rain_threshold);
    for (int i = 0; i < ui_data.num_actions; i++) {
        char key[12];
        action_rule_t* a = &ui_data.actions[i];
        snprintf(key, sizeof(key), "ae%d", i);  ui_prefs.putBool(key, a->enabled);
        snprintf(key, sizeof(key), "at%d", i);  ui_prefs.putUChar(key, (uint8_t)a->type);
        snprintf(key, sizeof(key), "ar%d", i);  ui_prefs.putUChar(key, a->target_relay);
        snprintf(key, sizeof(key), "av%d", i);  ui_prefs.putFloat(key, a->threshold);
    }
    ui_prefs.end();
}

static void load_actions() {
    ui_prefs.begin("actions2", true);
    ui_data.rain_threshold = ui_prefs.getFloat("rain_thr", 30.0f);
    ui_data.num_actions = ui_prefs.getInt("count", -1);

    if (ui_data.num_actions < 0) {
        // First boot: create default actions
        ui_data.num_actions = 5;

        // Action 0: Water flow alarm (pipe leak)
        ui_data.actions[0].enabled = true;
        ui_data.actions[0].type = ACTION_WATER_FLOW_ALARM;
        ui_data.actions[0].target_relay = 15; // Relay 16 (0-indexed)
        ui_data.actions[0].threshold = 50.0f; // L/min
        ui_data.actions[0].triggered = false;
        ui_data.actions[0].trigger_time_ms = 0;

        // Action 1: Rain → close shed
        ui_data.actions[1].enabled = true;
        ui_data.actions[1].type = ACTION_RAIN_AUTO;
        ui_data.actions[1].target_relay = 14; // Relay 15 (0-indexed)
        ui_data.actions[1].threshold = 30.0f; // Rain % threshold
        ui_data.actions[1].triggered = false;
        ui_data.actions[1].trigger_time_ms = 0;

        // Action 2: No water alarm
        ui_data.actions[2].enabled = true;
        ui_data.actions[2].type = ACTION_NO_WATER;
        ui_data.actions[2].target_relay = 13; // Relay 14 (0-indexed)
        ui_data.actions[2].threshold = 0.0f;
        ui_data.actions[2].triggered = false;
        ui_data.actions[2].trigger_time_ms = 0;

        // Action 3: Freezing temperature
        ui_data.actions[3].enabled = true;
        ui_data.actions[3].type = ACTION_FREEZING_TEMP;
        ui_data.actions[3].target_relay = 12; // Relay 13 (0-indexed)
        ui_data.actions[3].threshold = 0.0f;  // 0°C
        ui_data.actions[3].triggered = false;
        ui_data.actions[3].trigger_time_ms = 0;

        // Action 4: High wind speed
        ui_data.actions[4].enabled = true;
        ui_data.actions[4].type = ACTION_WIND_SPEED;
        ui_data.actions[4].target_relay = 14; // Same as rain (shed relay)
        ui_data.actions[4].threshold = 15.0f; // 15 m/s
        ui_data.actions[4].triggered = false;
        ui_data.actions[4].trigger_time_ms = 0;
    } else {
        for (int i = 0; i < ui_data.num_actions && i < MAX_ACTIONS; i++) {
            char key[12];
            action_rule_t* a = &ui_data.actions[i];
            snprintf(key, sizeof(key), "ae%d", i);  a->enabled = ui_prefs.getBool(key, false);
            snprintf(key, sizeof(key), "at%d", i);  a->type = (action_type_t)ui_prefs.getUChar(key, 0);
            snprintf(key, sizeof(key), "ar%d", i);  a->target_relay = ui_prefs.getUChar(key, 0);
            snprintf(key, sizeof(key), "av%d", i);  a->threshold = ui_prefs.getFloat(key, 50.0f);
            a->triggered = false;
            a->trigger_time_ms = 0;
        }
    }
    ui_prefs.end();
}

// ============================================================
// UI Initialization
// ============================================================
void ui_init(relay_cmd_cb_t relay_cb, alarm_notify_cb_t alarm_cb) {
    on_relay_command = relay_cb;
    on_alarm_notify = alarm_cb;

    // Initialize data
    memset(&ui_data, 0, sizeof(ui_data));
    memset(&ui_obj, 0, sizeof(ui_obj));

    load_relay_names();
    load_volumes();
    load_schedules();
    load_actions();
    init_styles();

    // Set dark background
    lv_obj_set_style_bg_color(lv_scr_act(), COLOR_BG, 0);

    // Create tab view (tabs at top)
    ui_obj.tabview = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, 50);
    lv_obj_set_style_bg_color(ui_obj.tabview, COLOR_BG, 0);

    // Style the tab buttons
    lv_obj_t* tab_btns = lv_tabview_get_tab_btns(ui_obj.tabview);
    lv_obj_set_style_bg_color(tab_btns, COLOR_CARD, 0);
    lv_obj_set_style_text_color(tab_btns, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(tab_btns, &lv_font_montserrat_16, 0);
    lv_obj_set_style_border_side(tab_btns, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(tab_btns, COLOR_HIGHLIGHT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(tab_btns, 3, LV_PART_ITEMS | LV_STATE_CHECKED);

    // Add tabs
    ui_obj.tab_dashboard = lv_tabview_add_tab(ui_obj.tabview, LV_SYMBOL_HOME " Home");
    ui_obj.tab_relays = lv_tabview_add_tab(ui_obj.tabview, LV_SYMBOL_SETTINGS " Relays");
    ui_obj.tab_schedules = lv_tabview_add_tab(ui_obj.tabview, LV_SYMBOL_LOOP " Schedules");
    ui_obj.tab_actions = lv_tabview_add_tab(ui_obj.tabview, LV_SYMBOL_WARNING " Actions");
    ui_obj.tab_settings = lv_tabview_add_tab(ui_obj.tabview, LV_SYMBOL_LIST " Settings");

    // Remove scrollbar from fixed-layout tabs
    lv_obj_clear_flag(ui_obj.tab_dashboard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(ui_obj.tab_relays, LV_OBJ_FLAG_SCROLLABLE);

    // Create tab contents
    create_dashboard_tab(ui_obj.tab_dashboard);
    create_relays_tab(ui_obj.tab_relays);
    create_schedules_tab(ui_obj.tab_schedules);
    create_actions_tab(ui_obj.tab_actions);
    create_settings_tab(ui_obj.tab_settings);

    // Create overlays (hidden initially)
    create_rename_overlay();
    create_schedule_overlay();
    create_action_overlay();
}

// ============================================================
// Dashboard Tab - Sensor readings and water volumes
// ============================================================
static lv_obj_t* create_sensor_card(lv_obj_t* parent, const char* title,
                                     const char* unit, lv_color_t accent_color,
                                     lv_obj_t** value_label) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_add_style(card, &style_card, 0);
    lv_obj_set_style_border_color(card, accent_color, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t* lbl_title = lv_label_create(card);
    lv_label_set_text(lbl_title, title);
    lv_obj_add_style(lbl_title, &style_title, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 0, 0);

    // Value
    *value_label = lv_label_create(card);
    lv_label_set_text(*value_label, "---");
    lv_obj_add_style(*value_label, &style_value, 0);
    lv_obj_set_style_text_color(*value_label, accent_color, 0);
    lv_obj_align(*value_label, LV_ALIGN_CENTER, 0, 5);

    // Unit
    lv_obj_t* lbl_unit = lv_label_create(card);
    lv_label_set_text(lbl_unit, unit);
    lv_obj_set_style_text_color(lbl_unit, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_unit, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_unit, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    return card;
}

static void create_dashboard_tab(lv_obj_t* parent) {
    lv_obj_set_style_pad_all(parent, 8, 0);

    // --- Alarm Banner (hidden by default, spans full width at top) ---
    ui_obj.lbl_alarm_banner = lv_obj_create(parent);
    lv_obj_set_size(ui_obj.lbl_alarm_banner, 960, 48);
    lv_obj_set_pos(ui_obj.lbl_alarm_banner, 0, 0);
    lv_obj_set_style_bg_color(ui_obj.lbl_alarm_banner, COLOR_WARN, 0);
    lv_obj_set_style_bg_opa(ui_obj.lbl_alarm_banner, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui_obj.lbl_alarm_banner, 0, 0);
    lv_obj_set_style_radius(ui_obj.lbl_alarm_banner, 8, 0);
    lv_obj_set_style_pad_all(ui_obj.lbl_alarm_banner, 0, 0);
    lv_obj_clear_flag(ui_obj.lbl_alarm_banner, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ui_obj.lbl_alarm_banner, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* alarm_icon = lv_label_create(ui_obj.lbl_alarm_banner);
    lv_label_set_text(alarm_icon, LV_SYMBOL_WARNING " ALARM");
    lv_obj_set_style_text_font(alarm_icon, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(alarm_icon, lv_color_white(), 0);
    lv_obj_align(alarm_icon, LV_ALIGN_LEFT_MID, 12, 0);

    // Alarm text (updated dynamically)
    lv_obj_t* alarm_text = lv_label_create(ui_obj.lbl_alarm_banner);
    lv_obj_set_style_text_font(alarm_text, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(alarm_text, lv_color_white(), 0);
    lv_label_set_text(alarm_text, "");
    lv_obj_align(alarm_text, LV_ALIGN_LEFT_MID, 140, 0);
    // Store text label as user_data so we can update it later
    lv_obj_set_user_data(ui_obj.lbl_alarm_banner, alarm_text);

    // Shift sensor cards down when banner is visible
    int banner_offset = 0;  // 0 when hidden, set dynamically in ui_update

    // Top row: Water sensors + wind + connection status
    // Row 1: Flow Rate | Water Temp | Rain Sensor | Wind Speed | Room Temp | Humidity
    // Row 2: Volume Day | Volume Month | Volume Year | Status

    int card_w = 152;
    int card_h = 100;
    int gap = 8;
    int row2_y = card_h + gap + 10;

    // --- Row 1: Live sensor values ---

    // Water Flow
    lv_obj_t* c1 = create_sensor_card(parent, "Water Flow", "L/min",
                                        COLOR_WATER, &ui_obj.lbl_flow_rate);
    lv_obj_set_size(c1, card_w, card_h);
    lv_obj_set_pos(c1, 0, 0);

    // Water Temperature
    lv_obj_t* c2 = create_sensor_card(parent, "Water Temp", "\xC2\xB0""C",
                                        COLOR_TEMP, &ui_obj.lbl_water_temp);
    lv_obj_set_size(c2, card_w, card_h);
    lv_obj_set_pos(c2, (card_w + gap), 0);

    // Rain Sensor
    lv_obj_t* c3 = create_sensor_card(parent, "Rain Sensor", "%",
                                        COLOR_RAIN, &ui_obj.lbl_rain);
    lv_obj_set_size(c3, card_w, card_h);
    lv_obj_set_pos(c3, 2 * (card_w + gap), 0);

    // Wind Speed
    lv_obj_t* c_wind = create_sensor_card(parent, "Wind Speed", "m/s",
                                        COLOR_ACCENT, &ui_obj.lbl_wind_speed);
    lv_obj_set_size(c_wind, card_w, card_h);
    lv_obj_set_pos(c_wind, 3 * (card_w + gap), 0);

    // Room Temperature (from Wroom SHT20)
    lv_obj_t* c4 = create_sensor_card(parent, "Room Temp", "\xC2\xB0""C",
                                        COLOR_TEMP, &ui_obj.lbl_room_temp);
    lv_obj_set_size(c4, card_w, card_h);
    lv_obj_set_pos(c4, 4 * (card_w + gap), 0);

    // Room Humidity
    lv_obj_t* c5 = create_sensor_card(parent, "Humidity", "%",
                                        COLOR_HUMID, &ui_obj.lbl_room_humid);
    lv_obj_set_size(c5, card_w, card_h);
    lv_obj_set_pos(c5, 5 * (card_w + gap), 0);

    // --- Row 2: Water volumes ---

    // Volume Today
    lv_obj_t* v1 = create_sensor_card(parent, "Today", "Liters",
                                        COLOR_WATER, &ui_obj.lbl_vol_day);
    lv_obj_set_size(v1, 220, card_h);
    lv_obj_set_pos(v1, 0, row2_y);

    // Volume This Month
    lv_obj_t* v2 = create_sensor_card(parent, "This Month", "Liters",
                                        COLOR_WATER, &ui_obj.lbl_vol_month);
    lv_obj_set_size(v2, 220, card_h);
    lv_obj_set_pos(v2, 228, row2_y);

    // Volume This Year
    lv_obj_t* v3 = create_sensor_card(parent, "This Year", "Liters",
                                        COLOR_WATER, &ui_obj.lbl_vol_year);
    lv_obj_set_size(v3, 220, card_h);
    lv_obj_set_pos(v3, 456, row2_y);

    // --- Connection Status Card ---
    lv_obj_t* status_card = lv_obj_create(parent);
    lv_obj_add_style(status_card, &style_card, 0);
    lv_obj_set_size(status_card, 280, card_h);
    lv_obj_set_pos(status_card, 684, row2_y);
    lv_obj_clear_flag(status_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl_conn_title = lv_label_create(status_card);
    lv_label_set_text(lbl_conn_title, "Connection Status");
    lv_obj_add_style(lbl_conn_title, &style_title, 0);
    lv_obj_align(lbl_conn_title, LV_ALIGN_TOP_LEFT, 0, 0);

    ui_obj.lbl_sensor_status = lv_label_create(status_card);
    lv_label_set_text(ui_obj.lbl_sensor_status, LV_SYMBOL_WIFI " Sensors: --");
    lv_obj_set_style_text_font(ui_obj.lbl_sensor_status, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ui_obj.lbl_sensor_status, COLOR_TEXT_DIM, 0);
    lv_obj_align(ui_obj.lbl_sensor_status, LV_ALIGN_LEFT_MID, 0, 5);

    ui_obj.lbl_uart_status = lv_label_create(status_card);
    lv_label_set_text(ui_obj.lbl_uart_status, LV_SYMBOL_USB " Relays: --");
    lv_obj_set_style_text_font(ui_obj.lbl_uart_status, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ui_obj.lbl_uart_status, COLOR_TEXT_DIM, 0);
    lv_obj_align(ui_obj.lbl_uart_status, LV_ALIGN_LEFT_MID, 0, 28);
}

// ============================================================
// Relay Control Tab - 16 toggle buttons in 4x4 grid
// ============================================================

static void relay_btn_click_cb(lv_event_t* e) {
    int relay_idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (relay_idx < 0 || relay_idx >= NUM_RELAYS) return;

    // Toggle state
    bool current = (ui_data.relay_states >> relay_idx) & 1;
    bool newState = !current;

    // Apply manual override logic BEFORE sending command
    handle_manual_relay_change(relay_idx, newState);

    if (on_relay_command) {
        on_relay_command(relay_idx, newState ? 1 : 0);
        ui_data.cmd_pending |= (1 << relay_idx);
    }
}

static void relay_btn_longpress_cb(lv_event_t* e) {
    int relay_idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (relay_idx < 0 || relay_idx >= NUM_RELAYS) return;

    // Open rename dialog
    ui_obj.rename_target_relay = relay_idx;
    lv_textarea_set_text(ui_obj.rename_textarea, ui_data.relay_names[relay_idx]);
    lv_obj_clear_flag(ui_obj.rename_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui_obj.rename_overlay);
}

static void create_relays_tab(lv_obj_t* parent) {
    lv_obj_set_style_pad_all(parent, 8, 0);

    // 4x4 grid of relay buttons
    int btn_w = 230;
    int btn_h = 115;
    int gap_x = 10;
    int gap_y = 10;
    int start_x = (1000 - (4 * btn_w + 3 * gap_x)) / 2; // Center grid
    int start_y = 5;

    for (int i = 0; i < NUM_RELAYS; i++) {
        int col = i % 4;
        int row = i / 4;
        int x = start_x + col * (btn_w + gap_x);
        int y = start_y + row * (btn_h + gap_y);

        // Button container
        lv_obj_t* btn = lv_obj_create(parent);
        lv_obj_set_size(btn, btn_w, btn_h);
        lv_obj_set_pos(btn, x, y);
        lv_obj_add_style(btn, &style_btn_off, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);

        // Relay name
        lv_obj_t* name = lv_label_create(btn);
        lv_label_set_text(name, ui_data.relay_names[i]);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(name, COLOR_RELAY_OFF_TEXT, 0);
        lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 5);

        // State indicator text
        lv_obj_t* state = lv_label_create(btn);
        lv_label_set_text(state, "OFF");
        lv_obj_set_style_text_font(state, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(state, COLOR_RELAY_OFF_TEXT, 0);
        lv_obj_align(state, LV_ALIGN_CENTER, 0, 10);

        // Tap to toggle
        lv_obj_add_event_cb(btn, relay_btn_click_cb, LV_EVENT_SHORT_CLICKED,
                            (void*)(intptr_t)i);
        // Long press to rename
        lv_obj_add_event_cb(btn, relay_btn_longpress_cb, LV_EVENT_LONG_PRESSED,
                            (void*)(intptr_t)i);

        ui_obj.relay_btns[i] = btn;
        ui_obj.relay_labels[i] = name;
        ui_obj.relay_state_labels[i] = state;
    }

    // Hint at bottom
    lv_obj_t* hint = lv_label_create(parent);
    lv_label_set_text(hint, "Tap to toggle  |  Long press to rename");
    lv_obj_set_style_text_color(hint, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -5);
}

// ============================================================
// Rename Overlay (fullscreen keyboard)
// ============================================================

static void rename_kb_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* kb = lv_event_get_target(e);

    if (code == LV_EVENT_READY) {
        // User pressed Enter/OK
        const char* text = lv_textarea_get_text(ui_obj.rename_textarea);
        int idx = ui_obj.rename_target_relay;
        if (idx >= 0 && idx < NUM_RELAYS && text && strlen(text) > 0) {
            strncpy(ui_data.relay_names[idx], text, RELAY_NAME_MAX - 1);
            ui_data.relay_names[idx][RELAY_NAME_MAX - 1] = '\0';
            save_relay_name(idx);
            lv_label_set_text(ui_obj.relay_labels[idx], ui_data.relay_names[idx]);
        }
        lv_obj_add_flag(ui_obj.rename_overlay, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(ui_obj.rename_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void rename_close_btn_cb(lv_event_t* e) {
    lv_obj_add_flag(ui_obj.rename_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void create_rename_overlay() {
    // Full screen overlay
    ui_obj.rename_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ui_obj.rename_overlay, 1024, 600);
    lv_obj_set_pos(ui_obj.rename_overlay, 0, 0);
    lv_obj_set_style_bg_color(ui_obj.rename_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ui_obj.rename_overlay, LV_OPA_90, 0);
    lv_obj_clear_flag(ui_obj.rename_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ui_obj.rename_overlay, LV_OBJ_FLAG_HIDDEN);

    // Title
    lv_obj_t* title = lv_label_create(ui_obj.rename_overlay);
    lv_label_set_text(title, "Rename Relay");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Close button
    lv_obj_t* close_btn = lv_btn_create(ui_obj.rename_overlay);
    lv_obj_set_size(close_btn, 50, 40);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_set_style_bg_color(close_btn, COLOR_WARN, 0);
    lv_obj_add_event_cb(close_btn, rename_close_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);
    lv_obj_center(close_lbl);

    // Text area for name input
    ui_obj.rename_textarea = lv_textarea_create(ui_obj.rename_overlay);
    lv_obj_set_size(ui_obj.rename_textarea, 500, 50);
    lv_obj_align(ui_obj.rename_textarea, LV_ALIGN_TOP_MID, 0, 70);
    lv_textarea_set_max_length(ui_obj.rename_textarea, RELAY_NAME_MAX - 1);
    lv_textarea_set_one_line(ui_obj.rename_textarea, true);
    lv_textarea_set_placeholder_text(ui_obj.rename_textarea, "Enter relay name...");
    lv_obj_set_style_text_font(ui_obj.rename_textarea, &lv_font_montserrat_22, 0);
    lv_obj_set_style_bg_color(ui_obj.rename_textarea, COLOR_CARD, 0);
    lv_obj_set_style_text_color(ui_obj.rename_textarea, COLOR_TEXT, 0);
    lv_obj_set_style_border_color(ui_obj.rename_textarea, COLOR_HIGHLIGHT, LV_STATE_FOCUSED);

    // LVGL Keyboard
    ui_obj.kb = lv_keyboard_create(ui_obj.rename_overlay);
    lv_obj_set_size(ui_obj.kb, 900, 340);
    lv_obj_align(ui_obj.kb, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_keyboard_set_textarea(ui_obj.kb, ui_obj.rename_textarea);
    lv_obj_set_style_bg_color(ui_obj.kb, COLOR_CARD, 0);
    lv_obj_set_style_bg_color(ui_obj.kb, COLOR_ACCENT, LV_PART_ITEMS);
    lv_obj_set_style_text_color(ui_obj.kb, COLOR_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_text_font(ui_obj.kb, &lv_font_montserrat_20, LV_PART_ITEMS);
    lv_obj_add_event_cb(ui_obj.kb, rename_kb_event_cb, LV_EVENT_ALL, NULL);

    ui_obj.rename_target_relay = -1;
}

// ============================================================
// Schedules Tab - List of relay schedules
// ============================================================

static const char* day_names_short[] = {"Mon","Tue","Wed","Thu","Fri","Sat","Sun"};

static void sched_edit_btn_cb(lv_event_t* e) {
    int relay_idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (relay_idx < 0 || relay_idx >= NUM_RELAYS) return;

    ui_obj.sched_edit_relay = relay_idx;
    relay_schedule_t* s = &ui_data.schedules[relay_idx];

    // Populate overlay with current schedule data
    lv_obj_set_state(ui_obj.sched_enable_sw, s->enabled ? LV_STATE_CHECKED : LV_STATE_DEFAULT, true);
    if (!s->enabled) lv_obj_clear_state(ui_obj.sched_enable_sw, LV_STATE_CHECKED);

    for (int d = 0; d < 7; d++) {
        if (s->days_mask & (1 << d))
            lv_obj_add_state(ui_obj.sched_day_cbs[d], LV_STATE_CHECKED);
        else
            lv_obj_clear_state(ui_obj.sched_day_cbs[d], LV_STATE_CHECKED);
    }

    lv_roller_set_selected(ui_obj.sched_hour_roller, s->start_hour, LV_ANIM_OFF);
    lv_roller_set_selected(ui_obj.sched_min_roller, s->start_minute, LV_ANIM_OFF);

    // Duration roller: index maps to 0.5h steps: 0=0.5, 1=1.0, ... 23=12.0
    int dur_idx = (int)(s->duration_hours / 0.5f) - 1;
    if (dur_idx < 0) dur_idx = 0;
    if (dur_idx > 23) dur_idx = 23;
    lv_roller_set_selected(ui_obj.sched_duration_roller, dur_idx, LV_ANIM_OFF);

    if (s->use_rain_sensor) lv_obj_add_state(ui_obj.sched_rain_cb, LV_STATE_CHECKED);
    else lv_obj_clear_state(ui_obj.sched_rain_cb, LV_STATE_CHECKED);

    if (s->repeat) lv_obj_add_state(ui_obj.sched_repeat_cb, LV_STATE_CHECKED);
    else lv_obj_clear_state(ui_obj.sched_repeat_cb, LV_STATE_CHECKED);

    // Repeat interval: index maps to 1h steps: 0=1, 1=2, ... 23=24
    int rep_idx = (int)(s->repeat_interval_hours) - 1;
    if (rep_idx < 0) rep_idx = 0;
    if (rep_idx > 23) rep_idx = 23;
    lv_roller_set_selected(ui_obj.sched_repeat_interval_roller, rep_idx, LV_ANIM_OFF);

    lv_obj_clear_flag(ui_obj.sched_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui_obj.sched_overlay);
}

static void refresh_schedule_list() {
    if (!ui_obj.sched_list) return;

    lv_obj_clean(ui_obj.sched_list);

    for (int i = 0; i < NUM_RELAYS; i++) {
        relay_schedule_t* s = &ui_data.schedules[i];

        lv_obj_t* row = lv_obj_create(ui_obj.sched_list);
        lv_obj_set_size(row, 950, 56);
        lv_obj_add_style(row, &style_card, 0);
        lv_obj_set_style_pad_all(row, 6, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 8, 0);

        if (s->enabled) {
            lv_obj_set_style_border_color(row, COLOR_SCHED, 0);
        }

        // Relay name
        lv_obj_t* name = lv_label_create(row);
        char name_buf[RELAY_NAME_MAX + 4];
        snprintf(name_buf, sizeof(name_buf), "%s", ui_data.relay_names[i]);
        lv_label_set_text(name, name_buf);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(name, s->enabled ? COLOR_SCHED : COLOR_TEXT_DIM, 0);
        lv_obj_set_width(name, 110);

        // Status/summary
        lv_obj_t* summary = lv_label_create(row);
        lv_obj_set_style_text_font(summary, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(summary, COLOR_TEXT_DIM, 0);
        lv_obj_set_width(summary, 540);

        if (s->enabled) {
            char days_str[32] = "";
            for (int d = 0; d < 7; d++) {
                if (s->days_mask & (1 << d)) {
                    if (strlen(days_str) > 0) strcat(days_str, ",");
                    strcat(days_str, day_names_short[d]);
                }
            }
            char sum_buf[128];
            snprintf(sum_buf, sizeof(sum_buf), "%02d:%02d  %.1fh  [%s]%s%s",
                     s->start_hour, s->start_minute, s->duration_hours,
                     strlen(days_str) > 0 ? days_str : "No days",
                     s->use_rain_sensor ? "  " LV_SYMBOL_TINT : "",
                     s->repeat ? "  " LV_SYMBOL_LOOP : "");
            lv_label_set_text(summary, sum_buf);
        } else {
            lv_label_set_text(summary, "Not configured");
        }

        // Running indicator
        lv_obj_t* status = lv_label_create(row);
        lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
        lv_obj_set_width(status, 80);
        if (s->currently_running) {
            lv_label_set_text(status, LV_SYMBOL_PLAY " ON");
            lv_obj_set_style_text_color(status, COLOR_RELAY_ON, 0);
        } else if (s->blocked_by_rain) {
            lv_label_set_text(status, LV_SYMBOL_TINT " Rain");
            lv_obj_set_style_text_color(status, COLOR_RAIN, 0);
        } else {
            lv_label_set_text(status, "");
        }

        // Edit button
        lv_obj_t* edit_btn = lv_btn_create(row);
        lv_obj_set_size(edit_btn, 90, 36);
        lv_obj_set_style_bg_color(edit_btn, COLOR_ACCENT, 0);
        lv_obj_set_style_radius(edit_btn, 6, 0);
        lv_obj_add_event_cb(edit_btn, sched_edit_btn_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        lv_obj_t* edit_lbl = lv_label_create(edit_btn);
        lv_label_set_text(edit_lbl, LV_SYMBOL_EDIT " Edit");
        lv_obj_set_style_text_font(edit_lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(edit_lbl);
    }
}

static void create_schedules_tab(lv_obj_t* parent) {
    lv_obj_set_style_pad_all(parent, 8, 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(parent, 4, 0);

    lv_obj_t* title = lv_label_create(parent);
    lv_label_set_text(title, LV_SYMBOL_LOOP " Relay Schedules — tap Edit to configure");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT, 0);

    // Scrollable list of 16 relay schedules
    ui_obj.sched_list = lv_obj_create(parent);
    lv_obj_set_size(ui_obj.sched_list, 980, 460);
    lv_obj_set_style_bg_opa(ui_obj.sched_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui_obj.sched_list, 0, 0);
    lv_obj_set_style_pad_all(ui_obj.sched_list, 0, 0);
    lv_obj_set_flex_flow(ui_obj.sched_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_obj.sched_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(ui_obj.sched_list, 4, 0);

    refresh_schedule_list();
}

// ============================================================
// Schedule Editor Overlay
// ============================================================

static void sched_save_cb(lv_event_t* e) {
    int idx = ui_obj.sched_edit_relay;
    if (idx < 0 || idx >= NUM_RELAYS) return;

    relay_schedule_t* s = &ui_data.schedules[idx];

    s->enabled = lv_obj_has_state(ui_obj.sched_enable_sw, LV_STATE_CHECKED);

    s->days_mask = 0;
    for (int d = 0; d < 7; d++) {
        if (lv_obj_has_state(ui_obj.sched_day_cbs[d], LV_STATE_CHECKED))
            s->days_mask |= (1 << d);
    }

    s->start_hour = lv_roller_get_selected(ui_obj.sched_hour_roller);
    s->start_minute = lv_roller_get_selected(ui_obj.sched_min_roller);
    s->duration_hours = (lv_roller_get_selected(ui_obj.sched_duration_roller) + 1) * 0.5f;
    s->use_rain_sensor = lv_obj_has_state(ui_obj.sched_rain_cb, LV_STATE_CHECKED);
    s->repeat = lv_obj_has_state(ui_obj.sched_repeat_cb, LV_STATE_CHECKED);
    s->repeat_interval_hours = (float)(lv_roller_get_selected(ui_obj.sched_repeat_interval_roller) + 1);

    save_schedules();
    refresh_schedule_list();

    lv_obj_add_flag(ui_obj.sched_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void sched_cancel_cb(lv_event_t* e) {
    lv_obj_add_flag(ui_obj.sched_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void create_schedule_overlay() {
    ui_obj.sched_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ui_obj.sched_overlay, 1024, 600);
    lv_obj_set_pos(ui_obj.sched_overlay, 0, 0);
    lv_obj_set_style_bg_color(ui_obj.sched_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ui_obj.sched_overlay, LV_OPA_90, 0);
    lv_obj_clear_flag(ui_obj.sched_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ui_obj.sched_overlay, LV_OBJ_FLAG_HIDDEN);

    // Title
    lv_obj_t* title = lv_label_create(ui_obj.sched_overlay);
    lv_label_set_text(title, "Edit Schedule");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    // Enable switch
    lv_obj_t* en_lbl = lv_label_create(ui_obj.sched_overlay);
    lv_label_set_text(en_lbl, "Enabled:");
    lv_obj_set_style_text_font(en_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(en_lbl, COLOR_TEXT, 0);
    lv_obj_set_pos(en_lbl, 30, 55);

    ui_obj.sched_enable_sw = lv_switch_create(ui_obj.sched_overlay);
    lv_obj_set_pos(ui_obj.sched_enable_sw, 140, 52);

    // ==== Row 1: Days of the week ====
    lv_obj_t* days_lbl = lv_label_create(ui_obj.sched_overlay);
    lv_label_set_text(days_lbl, "Days:");
    lv_obj_set_style_text_font(days_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(days_lbl, COLOR_TEXT, 0);
    lv_obj_set_pos(days_lbl, 30, 100);

    for (int d = 0; d < 7; d++) {
        ui_obj.sched_day_cbs[d] = lv_checkbox_create(ui_obj.sched_overlay);
        lv_checkbox_set_text(ui_obj.sched_day_cbs[d], day_names_short[d]);
        lv_obj_set_style_text_font(ui_obj.sched_day_cbs[d], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(ui_obj.sched_day_cbs[d], COLOR_TEXT, 0);
        lv_obj_set_pos(ui_obj.sched_day_cbs[d], 100 + d * 128, 98);
    }

    // ==== Row 2: Start time + Duration ====
    lv_obj_t* time_lbl = lv_label_create(ui_obj.sched_overlay);
    lv_label_set_text(time_lbl, "Start Time:");
    lv_obj_set_style_text_font(time_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(time_lbl, COLOR_TEXT, 0);
    lv_obj_set_pos(time_lbl, 30, 150);

    // Hour roller
    static char hour_opts[24 * 4]; // "00\n01\n...23"
    hour_opts[0] = '\0';
    for (int h = 0; h < 24; h++) {
        char tmp[5];
        snprintf(tmp, sizeof(tmp), "%s%02d", h > 0 ? "\n" : "", h);
        strcat(hour_opts, tmp);
    }
    ui_obj.sched_hour_roller = lv_roller_create(ui_obj.sched_overlay);
    lv_roller_set_options(ui_obj.sched_hour_roller, hour_opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(ui_obj.sched_hour_roller, 3);
    lv_obj_set_size(ui_obj.sched_hour_roller, 70, 90);
    lv_obj_set_pos(ui_obj.sched_hour_roller, 150, 140);
    lv_obj_set_style_text_font(ui_obj.sched_hour_roller, &lv_font_montserrat_18, 0);
    lv_obj_set_style_bg_color(ui_obj.sched_hour_roller, COLOR_CARD, 0);
    lv_obj_set_style_text_color(ui_obj.sched_hour_roller, COLOR_TEXT, 0);
    lv_obj_set_style_bg_color(ui_obj.sched_hour_roller, COLOR_ACCENT, LV_PART_SELECTED);

    lv_obj_t* colon = lv_label_create(ui_obj.sched_overlay);
    lv_label_set_text(colon, ":");
    lv_obj_set_style_text_font(colon, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(colon, COLOR_TEXT, 0);
    lv_obj_set_pos(colon, 225, 168);

    // Minute roller
    static char min_opts[60 * 4];
    min_opts[0] = '\0';
    for (int m = 0; m < 60; m++) {
        char tmp[5];
        snprintf(tmp, sizeof(tmp), "%s%02d", m > 0 ? "\n" : "", m);
        strcat(min_opts, tmp);
    }
    ui_obj.sched_min_roller = lv_roller_create(ui_obj.sched_overlay);
    lv_roller_set_options(ui_obj.sched_min_roller, min_opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(ui_obj.sched_min_roller, 3);
    lv_obj_set_size(ui_obj.sched_min_roller, 70, 90);
    lv_obj_set_pos(ui_obj.sched_min_roller, 240, 140);
    lv_obj_set_style_text_font(ui_obj.sched_min_roller, &lv_font_montserrat_18, 0);
    lv_obj_set_style_bg_color(ui_obj.sched_min_roller, COLOR_CARD, 0);
    lv_obj_set_style_text_color(ui_obj.sched_min_roller, COLOR_TEXT, 0);
    lv_obj_set_style_bg_color(ui_obj.sched_min_roller, COLOR_ACCENT, LV_PART_SELECTED);

    // Duration roller: 0.5h to 12h
    lv_obj_t* dur_lbl = lv_label_create(ui_obj.sched_overlay);
    lv_label_set_text(dur_lbl, "Duration:");
    lv_obj_set_style_text_font(dur_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(dur_lbl, COLOR_TEXT, 0);
    lv_obj_set_pos(dur_lbl, 350, 150);

    static char dur_opts[24 * 8];
    dur_opts[0] = '\0';
    for (int i = 0; i < 24; i++) {
        char tmp[10];
        float val = (i + 1) * 0.5f;
        snprintf(tmp, sizeof(tmp), "%s%.1fh", i > 0 ? "\n" : "", val);
        strcat(dur_opts, tmp);
    }
    ui_obj.sched_duration_roller = lv_roller_create(ui_obj.sched_overlay);
    lv_roller_set_options(ui_obj.sched_duration_roller, dur_opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(ui_obj.sched_duration_roller, 3);
    lv_obj_set_size(ui_obj.sched_duration_roller, 90, 90);
    lv_obj_set_pos(ui_obj.sched_duration_roller, 450, 140);
    lv_obj_set_style_text_font(ui_obj.sched_duration_roller, &lv_font_montserrat_18, 0);
    lv_obj_set_style_bg_color(ui_obj.sched_duration_roller, COLOR_CARD, 0);
    lv_obj_set_style_text_color(ui_obj.sched_duration_roller, COLOR_TEXT, 0);
    lv_obj_set_style_bg_color(ui_obj.sched_duration_roller, COLOR_ACCENT, LV_PART_SELECTED);

    // ==== Row 3: Rain sensor + Repeat ====
    ui_obj.sched_rain_cb = lv_checkbox_create(ui_obj.sched_overlay);
    lv_checkbox_set_text(ui_obj.sched_rain_cb, "Skip when raining " LV_SYMBOL_TINT);
    lv_obj_set_style_text_font(ui_obj.sched_rain_cb, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ui_obj.sched_rain_cb, COLOR_TEXT, 0);
    lv_obj_set_pos(ui_obj.sched_rain_cb, 30, 260);

    ui_obj.sched_repeat_cb = lv_checkbox_create(ui_obj.sched_overlay);
    lv_checkbox_set_text(ui_obj.sched_repeat_cb, "Repeat " LV_SYMBOL_LOOP);
    lv_obj_set_style_text_font(ui_obj.sched_repeat_cb, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ui_obj.sched_repeat_cb, COLOR_TEXT, 0);
    lv_obj_set_pos(ui_obj.sched_repeat_cb, 400, 260);

    // Repeat interval roller
    lv_obj_t* rep_lbl = lv_label_create(ui_obj.sched_overlay);
    lv_label_set_text(rep_lbl, "Every:");
    lv_obj_set_style_text_font(rep_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(rep_lbl, COLOR_TEXT, 0);
    lv_obj_set_pos(rep_lbl, 600, 260);

    static char rep_opts[24 * 8];
    rep_opts[0] = '\0';
    for (int i = 0; i < 24; i++) {
        char tmp[10];
        snprintf(tmp, sizeof(tmp), "%s%dh", i > 0 ? "\n" : "", i + 1);
        strcat(rep_opts, tmp);
    }
    ui_obj.sched_repeat_interval_roller = lv_roller_create(ui_obj.sched_overlay);
    lv_roller_set_options(ui_obj.sched_repeat_interval_roller, rep_opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(ui_obj.sched_repeat_interval_roller, 3);
    lv_obj_set_size(ui_obj.sched_repeat_interval_roller, 80, 90);
    lv_obj_set_pos(ui_obj.sched_repeat_interval_roller, 670, 245);
    lv_obj_set_style_text_font(ui_obj.sched_repeat_interval_roller, &lv_font_montserrat_18, 0);
    lv_obj_set_style_bg_color(ui_obj.sched_repeat_interval_roller, COLOR_CARD, 0);
    lv_obj_set_style_text_color(ui_obj.sched_repeat_interval_roller, COLOR_TEXT, 0);
    lv_obj_set_style_bg_color(ui_obj.sched_repeat_interval_roller, COLOR_ACCENT, LV_PART_SELECTED);

    // ==== Bottom: Save / Cancel buttons ====
    lv_obj_t* save_btn = lv_btn_create(ui_obj.sched_overlay);
    lv_obj_set_size(save_btn, 200, 55);
    lv_obj_set_pos(save_btn, 300, 520);
    lv_obj_set_style_bg_color(save_btn, COLOR_RELAY_ON, 0);
    lv_obj_set_style_radius(save_btn, 8, 0);
    lv_obj_add_event_cb(save_btn, sched_save_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* save_lbl = lv_label_create(save_btn);
    lv_label_set_text(save_lbl, LV_SYMBOL_OK " Save");
    lv_obj_set_style_text_font(save_lbl, &lv_font_montserrat_20, 0);
    lv_obj_center(save_lbl);

    lv_obj_t* cancel_btn = lv_btn_create(ui_obj.sched_overlay);
    lv_obj_set_size(cancel_btn, 200, 55);
    lv_obj_set_pos(cancel_btn, 530, 520);
    lv_obj_set_style_bg_color(cancel_btn, COLOR_WARN, 0);
    lv_obj_set_style_radius(cancel_btn, 8, 0);
    lv_obj_add_event_cb(cancel_btn, sched_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, LV_SYMBOL_CLOSE " Cancel");
    lv_obj_set_style_text_font(cancel_lbl, &lv_font_montserrat_20, 0);
    lv_obj_center(cancel_lbl);

    ui_obj.sched_edit_relay = -1;
}

// ============================================================
// Actions Tab
// ============================================================

// (action_type_names defined earlier for handle_manual_relay_change)

static void action_enable_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= ui_data.num_actions) return;
    ui_data.actions[idx].enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    save_actions();
}

static void action_dismiss_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= ui_data.num_actions) return;

    action_rule_t* a = &ui_data.actions[idx];
    if (a->triggered) {
        a->triggered = false;
        // Manual dismiss — set override so action won't re-trigger immediately
        ui_data.manual_override |= (1 << a->target_relay);
        if (!any_other_action_holds_relay(idx, a->target_relay))
            relay_command(a->target_relay, 0);
        if (on_alarm_notify) on_alarm_notify(idx, (uint8_t)a->type, false,
                                              true, 0.0f, a->threshold);
        refresh_action_cards();
    }
}

static void action_edit_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= ui_data.num_actions) return;

    ui_obj.action_edit_index = idx;
    action_rule_t* a = &ui_data.actions[idx];

    // Update overlay title
    char title_buf[64];
    snprintf(title_buf, sizeof(title_buf), "Edit: %s", action_type_names[a->type]);
    lv_label_set_text(ui_obj.action_overlay_title, title_buf);

    // Set relay roller to current target
    lv_roller_set_selected(ui_obj.action_relay_roller, a->target_relay, LV_ANIM_OFF);

    // Set threshold roller (maps threshold to index based on action type)
    if (a->type == ACTION_WATER_FLOW_ALARM) {
        // 5,10,15,...,100 → index = threshold/5 - 1
        lv_roller_set_options(ui_obj.action_threshold_roller, flow_thr_opts, LV_ROLLER_MODE_NORMAL);
        int thr_idx = (int)(a->threshold / 5.0f) - 1;
        if (thr_idx < 0) thr_idx = 0;
        if (thr_idx > 19) thr_idx = 19;
        lv_roller_set_selected(ui_obj.action_threshold_roller, thr_idx, LV_ANIM_OFF);
        lv_obj_clear_flag(ui_obj.action_threshold_roller, LV_OBJ_FLAG_HIDDEN);
    } else if (a->type == ACTION_FREEZING_TEMP) {
        // -5,-4,...,5 → index = threshold + 5
        lv_roller_set_options(ui_obj.action_threshold_roller, temp_thr_opts, LV_ROLLER_MODE_NORMAL);
        int thr_idx = (int)(a->threshold) + 5;
        if (thr_idx < 0) thr_idx = 0;
        if (thr_idx > 10) thr_idx = 10;
        lv_roller_set_selected(ui_obj.action_threshold_roller, thr_idx, LV_ANIM_OFF);
        lv_obj_clear_flag(ui_obj.action_threshold_roller, LV_OBJ_FLAG_HIDDEN);
    } else if (a->type == ACTION_WIND_SPEED) {
        // 5,10,15,...,60 m/s → index = threshold/5 - 1
        lv_roller_set_options(ui_obj.action_threshold_roller, wind_thr_opts, LV_ROLLER_MODE_NORMAL);
        int thr_idx = (int)(a->threshold / 5.0f) - 1;
        if (thr_idx < 0) thr_idx = 0;
        if (thr_idx > 11) thr_idx = 11;
        lv_roller_set_selected(ui_obj.action_threshold_roller, thr_idx, LV_ANIM_OFF);
        lv_obj_clear_flag(ui_obj.action_threshold_roller, LV_OBJ_FLAG_HIDDEN);
    } else {
        // Rain Auto and No Water don't have user-adjustable thresholds
        lv_obj_add_flag(ui_obj.action_threshold_roller, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_clear_flag(ui_obj.action_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui_obj.action_overlay);
}

static void update_action_description(int i) {
    if (i < 0 || i >= ui_data.num_actions) return;
    if (!ui_obj.action_desc_labels[i]) return;
    action_rule_t* a = &ui_data.actions[i];
    char desc_buf[160];
    if (a->type == ACTION_WATER_FLOW_ALARM) {
        snprintf(desc_buf, sizeof(desc_buf),
                 "Water flow > %.0f L/min: turn ON %s.\n"
                 "Blocks schedules. Clears when relay turned OFF.",
                 a->threshold, ui_data.relay_names[a->target_relay]);
    } else if (a->type == ACTION_RAIN_AUTO) {
        snprintf(desc_buf, sizeof(desc_buf),
                 "Rain detected: turn ON %s (close shed).\n"
                 "Auto-reverts when rain stops.",
                 ui_data.relay_names[a->target_relay]);
    } else if (a->type == ACTION_NO_WATER) {
        snprintf(desc_buf, sizeof(desc_buf),
                 "Scheduled relay ON 30s+ and no water flow:\n"
                 "turn ON %s. Clears when relay turned OFF.",
                 ui_data.relay_names[a->target_relay]);
    } else if (a->type == ACTION_FREEZING_TEMP) {
        snprintf(desc_buf, sizeof(desc_buf),
                 "Water temp <= %.0f\xC2\xB0""C: emergency dump via %s.\n"
                 "Clears when water flow drops to 0.",
                 a->threshold, ui_data.relay_names[a->target_relay]);
    } else if (a->type == ACTION_WIND_SPEED) {
        snprintf(desc_buf, sizeof(desc_buf),
                 "Wind > %.0f m/s: turn ON %s (protect).\n"
                 "Clears when 15-min avg < %.0f m/s.",
                 a->threshold, ui_data.relay_names[a->target_relay], a->threshold);
    } else {
        desc_buf[0] = '\0';
    }
    lv_label_set_text(ui_obj.action_desc_labels[i], desc_buf);
}

static void refresh_action_cards() {
    for (int i = 0; i < ui_data.num_actions && i < MAX_ACTIONS; i++) {
        action_rule_t* a = &ui_data.actions[i];
        if (!ui_obj.action_status_labels[i]) continue;

        if (!a->enabled) {
            lv_label_set_text(ui_obj.action_status_labels[i], "DISABLED");
            lv_obj_set_style_text_color(ui_obj.action_status_labels[i], COLOR_TEXT_DIM, 0);
        } else if (a->triggered) {
            lv_label_set_text(ui_obj.action_status_labels[i],
                              LV_SYMBOL_WARNING " ALARM ACTIVE");
            lv_obj_set_style_text_color(ui_obj.action_status_labels[i], COLOR_WARN, 0);
        } else {
            lv_label_set_text(ui_obj.action_status_labels[i], LV_SYMBOL_OK " OK");
            lv_obj_set_style_text_color(ui_obj.action_status_labels[i], COLOR_RELAY_ON, 0);
        }
    }
}

static void create_actions_tab(lv_obj_t* parent) {
    lv_obj_set_style_pad_all(parent, 10, 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(parent, 8, 0);

    lv_obj_t* title = lv_label_create(parent);
    lv_label_set_text(title, LV_SYMBOL_WARNING " Sensor-Based Actions & Alarms");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT, 0);

    static const lv_color_t card_colors[] = {
        COLOR_WARN, COLOR_RAIN, COLOR_WATER, COLOR_TEMP, COLOR_ACCENT
    };

    for (int i = 0; i < ui_data.num_actions && i < MAX_ACTIONS; i++) {
        action_rule_t* a = &ui_data.actions[i];

        lv_obj_t* card = lv_obj_create(parent);
        lv_obj_set_size(card, 960, 115);
        lv_obj_add_style(card, &style_card, 0);
        lv_obj_set_style_border_color(card, card_colors[a->type < 5 ? a->type : 0], 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        ui_obj.action_cards[i] = card;

        // Action name
        lv_obj_t* name = lv_label_create(card);
        lv_label_set_text(name, action_type_names[a->type]);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(name, COLOR_TEXT, 0);
        lv_obj_set_pos(name, 0, 0);

        // Description
        lv_obj_t* desc = lv_label_create(card);
        lv_obj_set_style_text_font(desc, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(desc, COLOR_TEXT_DIM, 0);
        lv_obj_set_width(desc, 620);
        lv_obj_set_pos(desc, 0, 22);
        ui_obj.action_desc_labels[i] = desc;
        update_action_description(i);

        // Right side: Enable switch
        lv_obj_t* en_lbl = lv_label_create(card);
        lv_label_set_text(en_lbl, "Enabled");
        lv_obj_set_style_text_font(en_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(en_lbl, COLOR_TEXT, 0);
        lv_obj_set_pos(en_lbl, 700, 0);

        lv_obj_t* en_sw = lv_switch_create(card);
        lv_obj_set_pos(en_sw, 770, -2);
        if (a->enabled) lv_obj_add_state(en_sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(en_sw, action_enable_cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)i);
        ui_obj.action_enable_sws[i] = en_sw;

        // Status label
        ui_obj.action_status_labels[i] = lv_label_create(card);
        lv_obj_set_style_text_font(ui_obj.action_status_labels[i], &lv_font_montserrat_18, 0);
        lv_obj_set_pos(ui_obj.action_status_labels[i], 700, 30);
        lv_label_set_text(ui_obj.action_status_labels[i], LV_SYMBOL_OK " OK");
        lv_obj_set_style_text_color(ui_obj.action_status_labels[i], COLOR_RELAY_ON, 0);

        // Dismiss button
        lv_obj_t* dismiss_btn = lv_btn_create(card);
        lv_obj_set_size(dismiss_btn, 100, 32);
        lv_obj_set_pos(dismiss_btn, 700, 62);
        lv_obj_set_style_bg_color(dismiss_btn, COLOR_WARN, 0);
        lv_obj_set_style_radius(dismiss_btn, 6, 0);
        lv_obj_add_event_cb(dismiss_btn, action_dismiss_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        lv_obj_t* dis_lbl = lv_label_create(dismiss_btn);
        lv_label_set_text(dis_lbl, "Dismiss");
        lv_obj_set_style_text_font(dis_lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(dis_lbl);

        // Edit button
        lv_obj_t* edit_btn = lv_btn_create(card);
        lv_obj_set_size(edit_btn, 100, 32);
        lv_obj_set_pos(edit_btn, 810, 62);
        lv_obj_set_style_bg_color(edit_btn, COLOR_ACCENT, 0);
        lv_obj_set_style_radius(edit_btn, 6, 0);
        lv_obj_add_event_cb(edit_btn, action_edit_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        lv_obj_t* edit_lbl = lv_label_create(edit_btn);
        lv_label_set_text(edit_lbl, LV_SYMBOL_EDIT " Edit");
        lv_obj_set_style_text_font(edit_lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(edit_lbl);
    }

    refresh_action_cards();
}

// ============================================================
// Action Editor Overlay
// ============================================================

static void action_save_cb(lv_event_t* e) {
    int idx = ui_obj.action_edit_index;
    if (idx < 0 || idx >= ui_data.num_actions) return;
    action_rule_t* a = &ui_data.actions[idx];

    a->target_relay = lv_roller_get_selected(ui_obj.action_relay_roller);

    if (a->type == ACTION_WATER_FLOW_ALARM) {
        a->threshold = (lv_roller_get_selected(ui_obj.action_threshold_roller) + 1) * 5.0f;
    } else if (a->type == ACTION_FREEZING_TEMP) {
        a->threshold = (float)((int)lv_roller_get_selected(ui_obj.action_threshold_roller) - 5);
    } else if (a->type == ACTION_WIND_SPEED) {
        a->threshold = (lv_roller_get_selected(ui_obj.action_threshold_roller) + 1) * 5.0f;
    }

    save_actions();
    update_action_description(idx);
    lv_obj_add_flag(ui_obj.action_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void action_cancel_cb(lv_event_t* e) {
    lv_obj_add_flag(ui_obj.action_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void create_action_overlay() {
    ui_obj.action_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ui_obj.action_overlay, 600, 360);
    lv_obj_center(ui_obj.action_overlay);
    lv_obj_set_style_bg_color(ui_obj.action_overlay, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(ui_obj.action_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ui_obj.action_overlay, COLOR_HIGHLIGHT, 0);
    lv_obj_set_style_border_width(ui_obj.action_overlay, 2, 0);
    lv_obj_set_style_radius(ui_obj.action_overlay, 16, 0);
    lv_obj_set_style_pad_all(ui_obj.action_overlay, 20, 0);
    lv_obj_clear_flag(ui_obj.action_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ui_obj.action_overlay, LV_OBJ_FLAG_HIDDEN);

    // Title
    ui_obj.action_overlay_title = lv_label_create(ui_obj.action_overlay);
    lv_label_set_text(ui_obj.action_overlay_title, "Edit Action");
    lv_obj_set_style_text_font(ui_obj.action_overlay_title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(ui_obj.action_overlay_title, COLOR_TEXT, 0);
    lv_obj_align(ui_obj.action_overlay_title, LV_ALIGN_TOP_MID, 0, 0);

    // --- Target Relay ---
    lv_obj_t* relay_lbl = lv_label_create(ui_obj.action_overlay);
    lv_label_set_text(relay_lbl, "Target Relay:");
    lv_obj_set_style_text_font(relay_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(relay_lbl, COLOR_TEXT, 0);
    lv_obj_set_pos(relay_lbl, 0, 50);

    // Build relay name options string
    static char relay_opts[NUM_RELAYS * (RELAY_NAME_MAX + 2)];
    relay_opts[0] = '\0';
    for (int i = 0; i < NUM_RELAYS; i++) {
        if (i > 0) strcat(relay_opts, "\n");
        char entry[RELAY_NAME_MAX + 8];
        snprintf(entry, sizeof(entry), "%d: %s", i + 1, ui_data.relay_names[i]);
        strcat(relay_opts, entry);
    }

    ui_obj.action_relay_roller = lv_roller_create(ui_obj.action_overlay);
    lv_roller_set_options(ui_obj.action_relay_roller, relay_opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(ui_obj.action_relay_roller, 3);
    lv_obj_set_size(ui_obj.action_relay_roller, 260, 100);
    lv_obj_set_pos(ui_obj.action_relay_roller, 180, 40);
    lv_obj_set_style_text_font(ui_obj.action_relay_roller, &lv_font_montserrat_16, 0);
    lv_obj_set_style_bg_color(ui_obj.action_relay_roller, COLOR_CARD, 0);
    lv_obj_set_style_text_color(ui_obj.action_relay_roller, COLOR_TEXT, 0);
    lv_obj_set_style_bg_color(ui_obj.action_relay_roller, COLOR_ACCENT, LV_PART_SELECTED);

    // --- Threshold ---
    lv_obj_t* thr_lbl = lv_label_create(ui_obj.action_overlay);
    lv_label_set_text(thr_lbl, "Threshold:");
    lv_obj_set_style_text_font(thr_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(thr_lbl, COLOR_TEXT, 0);
    lv_obj_set_pos(thr_lbl, 0, 160);

    // Threshold options: reused for both flow and temp, swapped when overlay opens
    // Flow: 5,10,...,100 L/min  (20 options)
    // Temp: -5,-4,...,5 °C       (11 options)
    // We build the flow options as default, swap in action_edit_cb via lv_roller_set_options
    // Flow: 5,10,...,100 L/min  (20 options)
    flow_thr_opts[0] = '\0';
    for (int i = 1; i <= 20; i++) {
        char tmp[10];
        snprintf(tmp, sizeof(tmp), "%s%d L/min", i > 1 ? "\n" : "", i * 5);
        strcat(flow_thr_opts, tmp);
    }

    // Temp: -5,-4,...,5 °C (11 options)
    temp_thr_opts[0] = '\0';
    for (int i = -5; i <= 5; i++) {
        char tmp[10];
        snprintf(tmp, sizeof(tmp), "%s%d\xC2\xB0""C", (i > -5) ? "\n" : "", i);
        strcat(temp_thr_opts, tmp);
    }

    // Wind: 5,10,15,...,60 m/s (12 options)
    wind_thr_opts[0] = '\0';
    for (int i = 1; i <= 12; i++) {
        char tmp[12];
        snprintf(tmp, sizeof(tmp), "%s%d m/s", i > 1 ? "\n" : "", i * 5);
        strcat(wind_thr_opts, tmp);
    }

    ui_obj.action_threshold_roller = lv_roller_create(ui_obj.action_overlay);
    lv_roller_set_options(ui_obj.action_threshold_roller, flow_thr_opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(ui_obj.action_threshold_roller, 3);
    lv_obj_set_size(ui_obj.action_threshold_roller, 160, 100);
    lv_obj_set_pos(ui_obj.action_threshold_roller, 180, 150);
    lv_obj_set_style_text_font(ui_obj.action_threshold_roller, &lv_font_montserrat_16, 0);
    lv_obj_set_style_bg_color(ui_obj.action_threshold_roller, COLOR_CARD, 0);
    lv_obj_set_style_text_color(ui_obj.action_threshold_roller, COLOR_TEXT, 0);
    lv_obj_set_style_bg_color(ui_obj.action_threshold_roller, COLOR_ACCENT, LV_PART_SELECTED);

    // Store option strings for swapping (use user_data on the threshold roller)
    // We'll just set them each time in action_edit_cb

    // --- Save / Cancel buttons ---
    lv_obj_t* save_btn = lv_btn_create(ui_obj.action_overlay);
    lv_obj_set_size(save_btn, 180, 50);
    lv_obj_set_pos(save_btn, 70, 270);
    lv_obj_set_style_bg_color(save_btn, COLOR_RELAY_ON, 0);
    lv_obj_set_style_radius(save_btn, 8, 0);
    lv_obj_add_event_cb(save_btn, action_save_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* save_lbl = lv_label_create(save_btn);
    lv_label_set_text(save_lbl, LV_SYMBOL_OK " Save");
    lv_obj_set_style_text_font(save_lbl, &lv_font_montserrat_18, 0);
    lv_obj_center(save_lbl);

    lv_obj_t* cancel_btn = lv_btn_create(ui_obj.action_overlay);
    lv_obj_set_size(cancel_btn, 180, 50);
    lv_obj_set_pos(cancel_btn, 290, 270);
    lv_obj_set_style_bg_color(cancel_btn, COLOR_WARN, 0);
    lv_obj_set_style_radius(cancel_btn, 8, 0);
    lv_obj_add_event_cb(cancel_btn, action_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, LV_SYMBOL_CLOSE " Cancel");
    lv_obj_set_style_text_font(cancel_lbl, &lv_font_montserrat_18, 0);
    lv_obj_center(cancel_lbl);

    ui_obj.action_edit_index = -1;
}

// ============================================================
// Settings Tab
// ============================================================

static void reset_day_cb(lv_event_t* e) {
    ui_data.water_volume_day = 0;
    save_volumes();
}

static void reset_month_cb(lv_event_t* e) {
    ui_data.water_volume_month = 0;
    save_volumes();
}

static void reset_year_cb(lv_event_t* e) {
    ui_data.water_volume_year = 0;
    save_volumes();
}

static void reset_all_names_cb(lv_event_t* e) {
    for (int i = 0; i < NUM_RELAYS; i++) {
        snprintf(ui_data.relay_names[i], RELAY_NAME_MAX, "Relay %d", i + 1);
        save_relay_name(i);
        if (ui_obj.relay_labels[i]) {
            lv_label_set_text(ui_obj.relay_labels[i], ui_data.relay_names[i]);
        }
    }
}

static lv_obj_t* create_settings_button(lv_obj_t* parent, const char* text,
                                          lv_event_cb_t cb, int y_pos) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 300, 50);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, y_pos);
    lv_obj_set_style_bg_color(btn, COLOR_ACCENT, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_center(label);

    return btn;
}

static void create_settings_tab(lv_obj_t* parent) {
    lv_obj_set_style_pad_all(parent, 15, 0);

    lv_obj_t* title = lv_label_create(parent);
    lv_label_set_text(title, "Settings & Maintenance");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    // Volume reset section
    lv_obj_t* vol_title = lv_label_create(parent);
    lv_label_set_text(vol_title, "Reset Water Volume Counters:");
    lv_obj_set_style_text_color(vol_title, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(vol_title, &lv_font_montserrat_16, 0);
    lv_obj_align(vol_title, LV_ALIGN_TOP_MID, 0, 45);

    create_settings_button(parent, LV_SYMBOL_REFRESH " Reset Daily Volume", reset_day_cb, 80);
    create_settings_button(parent, LV_SYMBOL_REFRESH " Reset Monthly Volume", reset_month_cb, 140);
    create_settings_button(parent, LV_SYMBOL_REFRESH " Reset Yearly Volume", reset_year_cb, 200);

    // Relay names section
    lv_obj_t* relay_title = lv_label_create(parent);
    lv_label_set_text(relay_title, "Relay Names:");
    lv_obj_set_style_text_color(relay_title, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(relay_title, &lv_font_montserrat_16, 0);
    lv_obj_align(relay_title, LV_ALIGN_TOP_MID, 0, 275);

    create_settings_button(parent, LV_SYMBOL_TRASH " Reset All Names to Default", reset_all_names_cb, 310);

    // Info
    lv_obj_t* info = lv_label_create(parent);
    lv_label_set_text(info, "Long-press any relay button to rename it.");
    lv_obj_set_style_text_color(info, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(info, &lv_font_montserrat_14, 0);
    lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 375);

    // Version info
    lv_obj_t* ver = lv_label_create(parent);
    lv_label_set_text(ver, "CrowPanel HMI System v1.0");
    lv_obj_set_style_text_color(ver, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(ver, &lv_font_montserrat_12, 0);
    lv_obj_align(ver, LV_ALIGN_BOTTOM_MID, 0, -10);
}

// ============================================================
// Update Relay Button Visuals
// ============================================================
static void update_relay_buttons() {
    for (int i = 0; i < NUM_RELAYS; i++) {
        bool on = (ui_data.relay_states >> i) & 1;
        lv_obj_t* btn = ui_obj.relay_btns[i];
        lv_obj_t* name_lbl = ui_obj.relay_labels[i];
        lv_obj_t* state_lbl = ui_obj.relay_state_labels[i];

        if (!btn) continue;

        if (on) {
            lv_obj_remove_style(btn, &style_btn_off, 0);
            lv_obj_add_style(btn, &style_btn_on, 0);
            lv_label_set_text(state_lbl, "ON");
            lv_obj_set_style_text_color(name_lbl, COLOR_RELAY_ON_TEXT, 0);
            lv_obj_set_style_text_color(state_lbl, COLOR_RELAY_ON_TEXT, 0);
        } else {
            lv_obj_remove_style(btn, &style_btn_on, 0);
            lv_obj_add_style(btn, &style_btn_off, 0);
            lv_label_set_text(state_lbl, "OFF");
            lv_obj_set_style_text_color(name_lbl, COLOR_RELAY_OFF_TEXT, 0);
            lv_obj_set_style_text_color(state_lbl, COLOR_RELAY_OFF_TEXT, 0);
        }
    }
}

// ============================================================
// Update UI with latest data
// ============================================================
void ui_update() {
    char buf[32];

    // Water flow
    snprintf(buf, sizeof(buf), "%.2f", ui_data.water_flow_rate);
    lv_label_set_text(ui_obj.lbl_flow_rate, buf);

    // Water temp
    snprintf(buf, sizeof(buf), "%.1f", ui_data.water_temperature);
    lv_label_set_text(ui_obj.lbl_water_temp, buf);

    // Rain
    snprintf(buf, sizeof(buf), "%.0f", ui_data.rain_sensor);
    lv_label_set_text(ui_obj.lbl_rain, buf);

    // Wind speed (show avg if available)
    if (ui_data.wind_avg_15min > 0.01f) {
        snprintf(buf, sizeof(buf), "%.1f", ui_data.wind_speed);
        lv_label_set_text(ui_obj.lbl_wind_speed, buf);
    } else {
        snprintf(buf, sizeof(buf), "%.1f", ui_data.wind_speed);
        lv_label_set_text(ui_obj.lbl_wind_speed, buf);
    }

    // Volumes
    snprintf(buf, sizeof(buf), "%.1f", ui_data.water_volume_day);
    lv_label_set_text(ui_obj.lbl_vol_day, buf);

    snprintf(buf, sizeof(buf), "%.1f", ui_data.water_volume_month);
    lv_label_set_text(ui_obj.lbl_vol_month, buf);

    snprintf(buf, sizeof(buf), "%.1f", ui_data.water_volume_year);
    lv_label_set_text(ui_obj.lbl_vol_year, buf);

    // Room temp & humidity
    snprintf(buf, sizeof(buf), "%.1f", ui_data.room_temperature);
    lv_label_set_text(ui_obj.lbl_room_temp, buf);

    snprintf(buf, sizeof(buf), "%.1f", ui_data.room_humidity);
    lv_label_set_text(ui_obj.lbl_room_humid, buf);

    // Connection status
    uint32_t now = millis();
    bool sensor_ok = (now - ui_data.last_sensor_time) < 10000;
    bool uart_ok = (now - ui_data.last_uart_time) < 5000;

    lv_label_set_text(ui_obj.lbl_sensor_status,
                      sensor_ok ? LV_SYMBOL_WIFI " Sensors: OK" : LV_SYMBOL_WIFI " Sensors: LOST");
    lv_obj_set_style_text_color(ui_obj.lbl_sensor_status,
                                 sensor_ok ? COLOR_RELAY_ON : COLOR_WARN, 0);

    lv_label_set_text(ui_obj.lbl_uart_status,
                      uart_ok ? LV_SYMBOL_USB " Relays: OK" : LV_SYMBOL_USB " Relays: LOST");
    lv_obj_set_style_text_color(ui_obj.lbl_uart_status,
                                 uart_ok ? COLOR_RELAY_ON : COLOR_WARN, 0);

    // Update relay buttons
    update_relay_buttons();

    // --- Alarm Banner ---
    if (ui_obj.lbl_alarm_banner) {
        bool any_alarm = false;
        const char* alarm_msg = nullptr;

        for (int i = 0; i < ui_data.num_actions && i < MAX_ACTIONS; i++) {
            if (ui_data.actions[i].enabled && ui_data.actions[i].triggered) {
                any_alarm = true;
                switch (ui_data.actions[i].type) {
                    case ACTION_WATER_FLOW_ALARM:
                        alarm_msg = "PIPE LEAK! Water flow above threshold.";
                        break;
                    case ACTION_RAIN_AUTO:
                        alarm_msg = "Rain detected \xe2\x80\x94 auto-close relay activated.";
                        break;
                    case ACTION_NO_WATER:
                        alarm_msg = "NO WATER! Scheduled relay running but no flow.";
                        break;
                    case ACTION_FREEZING_TEMP:
                        alarm_msg = "FREEZING! Emergency water dump in progress.";
                        break;
                    case ACTION_WIND_SPEED:
                        alarm_msg = "HIGH WIND! Protective relay activated.";
                        break;
                    default:
                        alarm_msg = "ALARM ACTIVE";
                        break;
                }
                break;  // Show first active alarm
            }
        }

        if (any_alarm && alarm_msg) {
            lv_obj_clear_flag(ui_obj.lbl_alarm_banner, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(ui_obj.lbl_alarm_banner, COLOR_WARN, 0);
            lv_obj_t* alarm_text = (lv_obj_t*)lv_obj_get_user_data(ui_obj.lbl_alarm_banner);
            if (alarm_text) {
                lv_label_set_text(alarm_text, alarm_msg);
            }
        } else if (ui_data.suggestion_time_ms > 0 &&
                   (millis() - ui_data.suggestion_time_ms < 8000)) {
            // Show suggestion (orange banner, 8 seconds)
            lv_obj_clear_flag(ui_obj.lbl_alarm_banner, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(ui_obj.lbl_alarm_banner, COLOR_SCHED, 0);
            lv_obj_t* alarm_text = (lv_obj_t*)lv_obj_get_user_data(ui_obj.lbl_alarm_banner);
            if (alarm_text) {
                lv_label_set_text(alarm_text, ui_data.suggestion_msg);
            }
        } else {
            if (ui_data.suggestion_time_ms > 0 &&
                (millis() - ui_data.suggestion_time_ms >= 8000)) {
                ui_data.suggestion_time_ms = 0; // Expire
            }
            lv_obj_add_flag(ui_obj.lbl_alarm_banner, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Update schedule list periodically (running status changes)
    static uint32_t lastSchedRefresh = 0;
    if (millis() - lastSchedRefresh > 2000) {
        lastSchedRefresh = millis();
        refresh_schedule_list();
    }
}

// ============================================================
// Data setters (called from main when data arrives)
// ============================================================
void ui_set_sensor_data(float flow, float temp, float totalLiters, bool isRaining, int rainAnalog, float windSpeed) {
    ui_data.water_flow_rate = flow;
    ui_data.water_temperature = temp;
    ui_data.rain_sensor = isRaining ? 100.0f : 0.0f;  // Map bool to percentage for UI thresholds
    ui_data.wind_speed = windSpeed;
    ui_data.last_sensor_time = millis();
    ui_data.sensor_node_connected = true;

    // --- Wind speed 15-minute rolling average (15 x 1-minute buckets) ---
    static float wind_buckets[15] = {0};
    static float wind_bucket_sum = 0.0f;
    static int wind_bucket_count = 0;
    static int wind_bucket_samples = 0;
    static int wind_bucket_idx = 0;
    static int wind_buckets_filled = 0;
    static uint32_t wind_bucket_start = 0;

    if (wind_bucket_start == 0) wind_bucket_start = millis();

    wind_bucket_sum += windSpeed;
    wind_bucket_samples++;

    // Every 60 seconds, store bucket avg and advance
    if (millis() - wind_bucket_start >= 60000) {
        float bucket_avg = (wind_bucket_samples > 0) ? (wind_bucket_sum / wind_bucket_samples) : 0.0f;
        wind_buckets[wind_bucket_idx] = bucket_avg;
        wind_bucket_idx = (wind_bucket_idx + 1) % 15;
        if (wind_buckets_filled < 15) wind_buckets_filled++;

        // Compute overall 15-min average
        float sum = 0.0f;
        for (int b = 0; b < wind_buckets_filled; b++) sum += wind_buckets[b];
        ui_data.wind_avg_15min = (wind_buckets_filled > 0) ? (sum / wind_buckets_filled) : windSpeed;

        // Reset current bucket accumulator
        wind_bucket_sum = 0.0f;
        wind_bucket_samples = 0;
        wind_bucket_start = millis();
    } else {
        // Before first bucket is full, use instantaneous as avg
        if (wind_buckets_filled == 0) {
            ui_data.wind_avg_15min = windSpeed;
        }
    }

    // Calculate volume delta from cumulative totalLiters
    if (lastTotalLiters >= 0.0f && totalLiters >= lastTotalLiters) {
        float delta = totalLiters - lastTotalLiters;
        ui_data.water_volume_day += delta;
        ui_data.water_volume_month += delta;
        ui_data.water_volume_year += delta;
    }
    lastTotalLiters = totalLiters;

    // Save every minute
    static uint32_t lastVolumeSave = 0;
    if (millis() - lastVolumeSave > 60000) {
        lastVolumeSave = millis();
        save_volumes();
    }
}

void ui_set_uart_data(float temp, float humidity, uint16_t relay_states) {
    uint16_t prev_states = ui_data.relay_states;
    ui_data.room_temperature = temp;
    ui_data.room_humidity = humidity;
    ui_data.relay_states = relay_states;
    ui_data.last_uart_time = millis();
    ui_data.uart_connected = true;

    // Detect relay state changes from Wroom (RainMaker / physical buttons)
    uint16_t changed = prev_states ^ relay_states;
    if (changed) {
        for (int i = 0; i < NUM_RELAYS; i++) {
            if (!(changed & (1 << i))) continue;

            if (ui_data.cmd_pending & (1 << i)) {
                // Expected change from CrowPanel command — no action needed
                ui_data.cmd_pending &= ~(1 << i);
            } else {
                // External change (RainMaker or physical button) — treat as manual
                bool new_state = (relay_states >> i) & 1;
                handle_manual_relay_change(i, new_state);
            }
        }
    }
}

// ============================================================
// Rain detection helper
// ============================================================
bool ui_is_raining() {
    // rain_sensor is 100.0 when raining, 0.0 when dry
    return ui_data.rain_sensor > 50.0f;
}

// ============================================================
// Time-based volume auto-reset
// Called from main.cpp when CMD_TIME_SYNC is received
// ============================================================
void ui_check_volume_reset(uint8_t day, uint8_t month, uint16_t year) {
    // First call: seed last values, don't reset
    if (lastDay < 0) {
        lastDay = day;
        lastMonth = month;
        lastYear = year;
        return;
    }

    // Day changed → reset daily volume
    if (day != lastDay) {
        Serial.printf("New day (%d->%d): resetting daily volume (was %.1f L)\n",
                       lastDay, day, ui_data.water_volume_day);
        ui_data.water_volume_day = 0;
        lastDay = day;
    }

    // Month changed → reset monthly volume
    if (month != lastMonth) {
        Serial.printf("New month (%d->%d): resetting monthly volume (was %.1f L)\n",
                       lastMonth, month, ui_data.water_volume_month);
        ui_data.water_volume_month = 0;
        lastMonth = month;
    }

    // Year changed → reset yearly volume
    if ((int)year != lastYear) {
        Serial.printf("New year (%d->%d): resetting yearly volume (was %.1f L)\n",
                       lastYear, year, ui_data.water_volume_year);
        ui_data.water_volume_year = 0;
        lastYear = year;
    }

    save_volumes();
}

// ============================================================
// Schedule Engine
// Called from main loop with current time (from RTC or millis-derived)
// current_dow: 0=Monday, 6=Sunday
// ============================================================
void ui_process_schedules(uint8_t current_hour, uint8_t current_minute, uint8_t current_dow) {
    // If global alarm is active, don't run any schedules
    if (ui_data.alarm_active) return;

    bool raining = ui_is_raining();
    uint32_t now = millis();

    for (int i = 0; i < NUM_RELAYS; i++) {
        relay_schedule_t* s = &ui_data.schedules[i];
        bool override = (ui_data.manual_override >> i) & 1;

        if (!s->enabled) {
            if (s->currently_running) {
                s->currently_running = false;
                if (!override) relay_command(i, 0);
            }
            continue;
        }

        // Check if today is a scheduled day
        bool today_active = (s->days_mask >> current_dow) & 1;
        if (!today_active) {
            if (s->currently_running) {
                s->currently_running = false;
                if (!override) relay_command(i, 0);
            }
            continue;
        }

        // Check rain block — still track state during override
        if (s->use_rain_sensor && raining) {
            s->blocked_by_rain = true;
            if (s->currently_running) {
                s->currently_running = false;
                if (!override) relay_command(i, 0);
            }
            continue;
        }
        s->blocked_by_rain = false;

        // Check if current time matches start time
        bool at_start_time = (current_hour == s->start_hour && current_minute == s->start_minute);

        // Duration in milliseconds
        uint32_t duration_ms = (uint32_t)(s->duration_hours * 3600000.0f);

        if (s->currently_running) {
            // Check if duration elapsed — always track, but only command if not overridden
            if (now - s->run_start_ms >= duration_ms) {
                s->currently_running = false;
                if (!override) relay_command(i, 0);
                s->last_repeat_ms = now;
            }
        } else {
            // Check if should start
            bool should_start = false;

            if (at_start_time && s->run_start_ms == 0) {
                should_start = true;
            } else if (s->repeat && s->last_repeat_ms > 0) {
                uint32_t interval_ms = (uint32_t)(s->repeat_interval_hours * 3600000.0f);
                if (now - s->last_repeat_ms >= interval_ms) {
                    should_start = true;
                }
            }

            if (should_start) {
                s->currently_running = true;
                s->run_start_ms = now;
                if (!override) relay_command(i, 1);
            }
        }
    }
}

// ============================================================
// Multi-Action Relay Sharing Helper
// Returns true if any OTHER enabled+triggered action targets the same relay.
// Used before turning OFF a relay — keeps it ON if another action still needs it.
// ============================================================
static bool any_other_action_holds_relay(int skip_idx, uint8_t relay) {
    for (int j = 0; j < ui_data.num_actions && j < MAX_ACTIONS; j++) {
        if (j == skip_idx) continue;
        action_rule_t* other = &ui_data.actions[j];
        if (other->enabled && other->triggered && other->target_relay == relay) {
            return true;
        }
    }
    return false;
}

// ============================================================
// Action Engine
// Called from main loop to evaluate sensor-based actions
// ============================================================
void ui_process_actions() {
    ui_data.alarm_active = false;
    uint32_t now = millis();

    for (int i = 0; i < ui_data.num_actions && i < MAX_ACTIONS; i++) {
        action_rule_t* a = &ui_data.actions[i];
        if (!a->enabled) continue;

        bool relay_is_off = !((ui_data.relay_states >> a->target_relay) & 1);
        bool override = (ui_data.manual_override >> a->target_relay) & 1;

        switch (a->type) {
            case ACTION_WATER_FLOW_ALARM: {
                // Trigger: flow > threshold
                // Clear: target relay turned OFF from any source (clears override too)
                bool condition = (ui_data.water_flow_rate > a->threshold);
                if (a->triggered && relay_is_off) {
                    // Alarm clears when relay turned OFF — allow re-trigger
                    a->triggered = false;
                    ui_data.manual_override &= ~(1 << a->target_relay);
                    if (on_alarm_notify) on_alarm_notify(i, ALARM_TYPE_WATER_FLOW, false,
                                                          true, ui_data.water_flow_rate, a->threshold);
                } else if (condition && !a->triggered && !override) {
                    a->triggered = true;
                    a->trigger_time_ms = now;
                    relay_command(a->target_relay, 1);
                    if (on_alarm_notify) on_alarm_notify(i, ALARM_TYPE_WATER_FLOW, true,
                                                          true, ui_data.water_flow_rate, a->threshold);
                    display_reset_activity();
                }
                if (a->triggered) ui_data.alarm_active = true;
                break;
            }

            case ACTION_RAIN_AUTO: {
                // Trigger: raining — but not if user overrode the relay
                // Clear: not raining (auto-revert, but don't touch relay if overridden)
                bool raining = (ui_data.rain_sensor >= a->threshold);
                if (raining && !a->triggered && !override) {
                    a->triggered = true;
                    a->trigger_time_ms = now;
                    relay_command(a->target_relay, 1);
                    if (on_alarm_notify) on_alarm_notify(i, ALARM_TYPE_RAIN_AUTO, true,
                                                          false, ui_data.rain_sensor, a->threshold);
                    display_reset_activity();
                } else if (!raining && a->triggered) {
                    a->triggered = false;
                    if (!override && !any_other_action_holds_relay(i, a->target_relay))
                        relay_command(a->target_relay, 0);
                    if (on_alarm_notify) on_alarm_notify(i, ALARM_TYPE_RAIN_AUTO, false,
                                                          false, ui_data.rain_sensor, a->threshold);
                }
                break;
            }

            case ACTION_NO_WATER: {
                // Trigger: any schedule running 30s+ AND flow == 0
                // Clear: target relay turned OFF from any source (clears override too)
                bool any_sched_long = false;
                for (int s = 0; s < NUM_RELAYS; s++) {
                    if (ui_data.schedules[s].currently_running &&
                        (now - ui_data.schedules[s].run_start_ms > 30000)) {
                        any_sched_long = true;
                        break;
                    }
                }
                bool no_water_condition = any_sched_long && (ui_data.water_flow_rate < 0.01f);
                if (a->triggered && relay_is_off) {
                    a->triggered = false;
                    ui_data.manual_override &= ~(1 << a->target_relay);
                    if (on_alarm_notify) on_alarm_notify(i, ALARM_TYPE_NO_WATER, false,
                                                          true, ui_data.water_flow_rate, 0.0f);
                } else if (no_water_condition && !a->triggered && !override) {
                    a->triggered = true;
                    a->trigger_time_ms = now;
                    relay_command(a->target_relay, 1);
                    if (on_alarm_notify) on_alarm_notify(i, ALARM_TYPE_NO_WATER, true,
                                                          true, ui_data.water_flow_rate, 0.0f);
                    display_reset_activity();
                }
                if (a->triggered) ui_data.alarm_active = true;
                break;
            }

            case ACTION_FREEZING_TEMP: {
                // Trigger: water temp <= threshold
                // Clear: flow drops to 0 after 60s (dump complete) — clears override too
                bool freezing = (ui_data.water_temperature <= a->threshold);
                if (freezing && !a->triggered && !override) {
                    a->triggered = true;
                    a->trigger_time_ms = now;
                    relay_command(a->target_relay, 1);
                    if (on_alarm_notify) on_alarm_notify(i, ALARM_TYPE_FREEZING, true,
                                                          true, ui_data.water_temperature, a->threshold);
                    display_reset_activity();
                } else if (a->triggered &&
                           ui_data.water_flow_rate < 0.01f &&
                           (now - a->trigger_time_ms > 60000)) {
                    a->triggered = false;
                    ui_data.manual_override &= ~(1 << a->target_relay);
                    if (!override && !any_other_action_holds_relay(i, a->target_relay))
                        relay_command(a->target_relay, 0);
                    if (on_alarm_notify) on_alarm_notify(i, ALARM_TYPE_FREEZING, false,
                                                          true, ui_data.water_temperature, a->threshold);
                }
                if (a->triggered) ui_data.alarm_active = true;
                break;
            }

            case ACTION_WIND_SPEED: {
                // Trigger: wind_speed > threshold
                // Clear: 15-min average < threshold (and not overridden)
                bool wind_high = (ui_data.wind_speed > a->threshold);
                if (wind_high && !a->triggered && !override) {
                    a->triggered = true;
                    a->trigger_time_ms = now;
                    relay_command(a->target_relay, 1);
                    if (on_alarm_notify) on_alarm_notify(i, ALARM_TYPE_WIND_SPEED, true,
                                                          false, ui_data.wind_speed, a->threshold);
                    display_reset_activity();
                } else if (a->triggered &&
                           ui_data.wind_avg_15min < a->threshold) {
                    a->triggered = false;
                    if (!override && !any_other_action_holds_relay(i, a->target_relay))
                        relay_command(a->target_relay, 0);
                    if (on_alarm_notify) on_alarm_notify(i, ALARM_TYPE_WIND_SPEED, false,
                                                          false, ui_data.wind_avg_15min, a->threshold);
                }
                break;
            }

            default:
                break;
        }
    }

    // Update action card status labels
    refresh_action_cards();
}

void ui_dismiss_alarm(int action_index) {
    if (action_index < 0 || action_index >= ui_data.num_actions) return;
    action_rule_t* a = &ui_data.actions[action_index];
    if (a->triggered) {
        a->triggered = false;
        ui_data.manual_override |= (1 << a->target_relay);
        if (!any_other_action_holds_relay(action_index, a->target_relay))
            relay_command(a->target_relay, 0);
        if (on_alarm_notify) on_alarm_notify(action_index, (uint8_t)a->type, false,
                                              true, 0.0f, a->threshold);
    }
}
