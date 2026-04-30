/*
 * ui.h - LVGL User Interface for CrowPanel HMI
 * Manages all screens: Dashboard, Relay Control, Schedules, Actions, Settings
 */

#pragma once

#include <lvgl.h>
#include <Preferences.h>
#include "protocol.h"

// ============================================================
// UI Data Model
// ============================================================

#define NUM_RELAYS 16
#define RELAY_NAME_MAX 16
#define MAX_ACTIONS 8

// Day-of-week bitmask
#define DAY_MON  (1 << 0)
#define DAY_TUE  (1 << 1)
#define DAY_WED  (1 << 2)
#define DAY_THU  (1 << 3)
#define DAY_FRI  (1 << 4)
#define DAY_SAT  (1 << 5)
#define DAY_SUN  (1 << 6)

// Colors
#define COLOR_BG            lv_color_hex(0x1a1a2e)
#define COLOR_CARD          lv_color_hex(0x16213e)
#define COLOR_ACCENT        lv_color_hex(0x0f3460)
#define COLOR_HIGHLIGHT     lv_color_hex(0x00b4d8)
#define COLOR_RELAY_ON      lv_color_hex(0x2ecc71)
#define COLOR_RELAY_OFF     lv_color_hex(0x555555)
#define COLOR_RELAY_ON_TEXT lv_color_hex(0xffffff)
#define COLOR_RELAY_OFF_TEXT lv_color_hex(0xaaaaaa)
#define COLOR_TEXT          lv_color_hex(0xe0e0e0)
#define COLOR_TEXT_DIM      lv_color_hex(0x888888)
#define COLOR_WARN          lv_color_hex(0xe74c3c)
#define COLOR_WATER         lv_color_hex(0x3498db)
#define COLOR_TEMP          lv_color_hex(0xe67e22)
#define COLOR_RAIN          lv_color_hex(0x9b59b6)
#define COLOR_HUMID         lv_color_hex(0x1abc9c)
#define COLOR_SCHED         lv_color_hex(0xf39c12)
#define COLOR_ACTION        lv_color_hex(0xe74c3c)

// ============================================================
// Schedule per relay
// ============================================================
typedef struct {
    bool enabled;               // Schedule active
    uint8_t days_mask;          // Bitmask: bit0=Mon ... bit6=Sun
    uint8_t start_hour;         // 0-23
    uint8_t start_minute;       // 0-59
    float duration_hours;       // Duration in hours (0.5 = 30min)
    bool use_rain_sensor;       // If true, skip schedule when raining
    bool repeat;                // Repeat during the day
    float repeat_interval_hours;// Hours between repetitions

    // Runtime state (not saved)
    bool currently_running;     // Schedule turned relay ON
    uint32_t run_start_ms;      // millis() when relay was turned ON
    uint32_t last_repeat_ms;    // millis() of last repeat trigger
    bool blocked_by_rain;       // Currently skipped due to rain
    int8_t last_start_day;      // Day-of-week (0-6) when schedule last triggered (-1 = never)
} relay_schedule_t;

// ============================================================
// Action definition
// ============================================================
typedef enum {
    ACTION_WATER_FLOW_ALARM = 0,   // Flow > threshold → alarm (pipe leak)
    ACTION_RAIN_AUTO,              // Raining → turn on relay (close shed)
    ACTION_NO_WATER,               // Scheduled relay ON + flow == 0 → alarm
    ACTION_FREEZING_TEMP,          // Water temp ≤ 0 → emergency dump
    ACTION_WIND_SPEED,             // Wind > threshold → close shed (15-min avg to clear)
    ACTION_TYPE_COUNT
} action_type_t;

typedef struct {
    bool enabled;
    action_type_t type;
    uint8_t target_relay;       // Relay index (0-15) to control
    float threshold;            // Threshold value (e.g., 50 L/min)
    bool triggered;             // Currently triggered (runtime)
    uint32_t trigger_time_ms;   // millis() when triggered (runtime)
} action_rule_t;

// Event log constants
#define EVENT_LOG_SIZE 20
#define EVENT_MSG_LEN 64

typedef struct {
    // Water sensor data (from Wroom UART, originally from sensor node)
    float water_flow_rate;      // L/min
    float water_temperature;    // °C
    float rain_sensor;          // %
    float wind_speed;           // m/s (current reading)
    float wind_avg_15min;       // m/s (15-minute rolling average)
    float water_volume_day;     // Liters today
    float water_volume_month;   // Liters this month
    float water_volume_year;    // Liters this year

    // Relay controller data (from UART)
    float room_temperature;     // °C
    float room_humidity;        // %
    uint16_t relay_states;      // Bitmask

    // Relay names
    char relay_names[NUM_RELAYS][RELAY_NAME_MAX];

    // Schedules
    relay_schedule_t schedules[NUM_RELAYS];

    // Actions
    action_rule_t actions[MAX_ACTIONS];
    int num_actions;
    bool alarm_active;          // Global alarm flag (blocks all schedules)

    // Manual override
    uint16_t manual_override;      // Bitmask: relay under manual user control
    uint16_t cmd_pending;          // Bitmask: relay command sent, awaiting state confirm
    char suggestion_msg[80];       // Temporary suggestion shown on banner
    uint32_t suggestion_time_ms;   // millis() when suggestion was set (0 = none)

    // Connection status
    bool sensor_node_connected;
    bool uart_connected;
    uint32_t last_sensor_time;
    uint32_t last_uart_time;

    // Rain threshold (% above which = raining)
    float rain_threshold;

    // Rain analog value (0-4095, lower = more rain)
    int rain_analog;

    // Time tracking (updated from main loop)
    uint8_t current_hour;
    uint8_t current_minute;
    uint8_t current_dow;
    bool time_synced;

    // Event log (circular buffer)
    char event_log[EVENT_LOG_SIZE][EVENT_MSG_LEN];
    uint8_t event_log_head;     // Next write position
    uint8_t event_log_count;    // Total entries (max EVENT_LOG_SIZE)
} ui_data_t;

// ============================================================
// UI Object References
// ============================================================
typedef struct {
    // Main tabview
    lv_obj_t* tabview;
    lv_obj_t* tab_dashboard;
    lv_obj_t* tab_relays;
    lv_obj_t* tab_schedules;
    lv_obj_t* tab_actions;
    lv_obj_t* tab_settings;

    // Dashboard labels
    lv_obj_t* lbl_flow_rate;
    lv_obj_t* lbl_water_temp;
    lv_obj_t* lbl_rain;
    lv_obj_t* lbl_wind_speed;
    lv_obj_t* lbl_vol_day;
    lv_obj_t* lbl_vol_month;
    lv_obj_t* lbl_vol_year;
    lv_obj_t* lbl_room_temp;
    lv_obj_t* lbl_room_humid;
    lv_obj_t* lbl_sensor_status;
    lv_obj_t* lbl_uart_status;
    lv_obj_t* lbl_alarm_banner;
    lv_obj_t* lbl_clock;            // System clock display
    lv_obj_t* lbl_next_schedule;    // Next scheduled event
    lv_obj_t* lbl_last_updated;     // Sensor data age
    lv_obj_t* toast_obj;            // Toast notification container
    lv_obj_t* toast_label;          // Toast text label
    lv_obj_t* event_log_list;       // Event log list (in Settings tab)

    // Relay buttons
    lv_obj_t* relay_btns[NUM_RELAYS];
    lv_obj_t* relay_labels[NUM_RELAYS];
    lv_obj_t* relay_state_labels[NUM_RELAYS];

    // Schedule editor overlay
    lv_obj_t* sched_overlay;
    lv_obj_t* sched_day_cbs[7];        // Day checkboxes Mon-Sun
    lv_obj_t* sched_hour_roller;
    lv_obj_t* sched_min_roller;
    lv_obj_t* sched_duration_roller;
    lv_obj_t* sched_rain_cb;
    lv_obj_t* sched_repeat_cb;
    lv_obj_t* sched_repeat_interval_roller;
    lv_obj_t* sched_enable_sw;
    int sched_edit_relay;

    // Schedule list items
    lv_obj_t* sched_list;

    // Action list
    lv_obj_t* action_list;
    lv_obj_t* action_cards[MAX_ACTIONS];
    lv_obj_t* action_status_labels[MAX_ACTIONS];
    lv_obj_t* action_enable_sws[MAX_ACTIONS];
    lv_obj_t* action_desc_labels[MAX_ACTIONS];

    // Action editor overlay
    lv_obj_t* action_overlay;
    lv_obj_t* action_overlay_title;
    lv_obj_t* action_relay_roller;
    lv_obj_t* action_threshold_roller;
    int action_edit_index;

    // Keyboard for renaming
    lv_obj_t* kb;
    lv_obj_t* rename_overlay;
    lv_obj_t* rename_textarea;
    int rename_target_relay;
} ui_objects_t;

extern ui_data_t ui_data;
extern ui_objects_t ui_obj;
extern Preferences ui_prefs;

// Callback type for relay command
typedef void (*relay_cmd_cb_t)(uint8_t relay_index, uint8_t state);
extern relay_cmd_cb_t on_relay_command;

// Callback type for alarm notification to Wroom
typedef void (*alarm_notify_cb_t)(uint8_t alarm_index, uint8_t alarm_type,
                                   bool triggered, bool permanent,
                                   float sensor_value, float threshold);
extern alarm_notify_cb_t on_alarm_notify;

// ============================================================
// Functions
// ============================================================
void ui_init(relay_cmd_cb_t relay_cb, alarm_notify_cb_t alarm_cb);
void ui_update(void);
void ui_set_sensor_data(float flow, float temp, float totalLiters, bool isRaining, int rainAnalog, float windSpeed);
void ui_set_uart_data(float temp, float humidity, uint16_t relay_states);

// Schedule & Action engine (called from main loop)
void ui_process_schedules(uint8_t current_hour, uint8_t current_minute, uint8_t current_dow);
void ui_process_actions(void);
bool ui_is_raining(void);
void ui_dismiss_alarm(int action_index);

// Time-based volume auto-reset (called when time sync received)
void ui_check_volume_reset(uint8_t day, uint8_t month, uint16_t year);

// Update system time (call from main loop)
void ui_set_time(uint8_t hour, uint8_t minute, uint8_t dow, bool synced);

// Show toast notification (brief 3s popup)
void ui_show_toast(const char* msg);

// Add event to history log
void ui_log_event(const char* msg);

// Persistence
void save_schedules(void);
void save_actions(void);
