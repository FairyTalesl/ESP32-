/*
 *   This file is part of DroneBridge: https://github.com/DroneBridge/ESP32
 *   Modified for WS2812 RGB LED on ESP32-C6 Super Mini
 */

#include <driver/gpio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "led_strip.h" // RGB LED Kütüphanesi Eklendi

#include "db_parameters.h"
#include "db_led_indicator.h"

static const char *TAG = "DB_LED_IND";

#define DB_STATUS_LED_TIMEOUT_MS 1000
#define RGB_LED_GPIO 8 // ESP32-C6 Super Mini üzerindeki LED pini

static TickType_t db_status_led_last_serial_mavlink_tick = 0;
static TickType_t db_status_led_last_radio_tick = 0;
static bool db_status_led_initialized = false;
static db_status_led_binding_state_t db_status_led_binding_state = DB_STATUS_LED_BINDING_NONE;

// LED Objesi ve Mevcut Renk Hafızası (Gereksiz güncellemeleri önlemek için)
static led_strip_handle_t led_strip;
static uint8_t current_r = 0, current_g = 0, current_b = 0;

static bool db_status_led_is_activity_recent(TickType_t now_tick, TickType_t last_tick) {
    if (last_tick == 0) {
        return false;
    }
    return (now_tick - last_tick) < pdMS_TO_TICKS(DB_STATUS_LED_TIMEOUT_MS);
}

void db_status_led_init() {
    // Eski GPIO kurulumu yerine RGB LED kurulumu
    led_strip_config_t strip_config = {
        .strip_gpio_num = RGB_LED_GPIO,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz RMT
    };
    
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    led_strip_clear(led_strip);
    
    db_status_led_initialized = true;
    db_status_led_last_serial_mavlink_tick = 0;
    db_status_led_last_radio_tick = 0;
}

void db_status_led_mark_serial_mavlink_rx() {
    db_status_led_last_serial_mavlink_tick = xTaskGetTickCount();
}

void db_status_led_mark_radio_rx() {
    db_status_led_last_radio_tick = xTaskGetTickCount();
}

void db_status_led_set_binding_state(db_status_led_binding_state_t state) {
    db_status_led_binding_state = state;
}

void db_status_led_process() {
    if (!db_status_led_initialized) {
        return;
    }

    TickType_t now_tick = xTaskGetTickCount();
    uint8_t target_r = 0, target_g = 0, target_b = 0;

    if (db_status_led_binding_state != DB_STATUS_LED_BINDING_NONE) {
        const uint32_t elapsed_ms = now_tick * portTICK_PERIOD_MS;
        bool flash = false;
        
        switch (db_status_led_binding_state) {
            case DB_STATUS_LED_BINDING_SEARCHING:
                flash = (elapsed_ms % 1000U) < 250U;
                if(flash) { target_b = 255; } // Mavi yanıp sönme
                break;
            case DB_STATUS_LED_BINDING_NEGOTIATING:
                flash = (elapsed_ms % 250U) < 125U;
                if(flash) { target_r = 200; target_g = 200; } // Sarı hızlı yanıp sönme
                break;
            case DB_STATUS_LED_BINDING_SUCCESS:
                target_g = 255; // Sabit Yeşil
                break;
            case DB_STATUS_LED_BINDING_FAILURE:
                flash = (elapsed_ms % 1000U) < 150U ||
                        ((elapsed_ms % 1000U) >= 300U && (elapsed_ms % 1000U) < 450U) ||
                        ((elapsed_ms % 1000U) >= 600U && (elapsed_ms % 1000U) < 750U);
                if(flash) { target_r = 255; } // Kırmızı SOS
                break;
            default:
                break;
        }
    } else {
        bool active = false;
        
        // ESP-NOW AIR veya AP-LR modundaysak, SADECE telsizden (radio) paket geldiyse yeşil yap.
        // Uçuş kartından seri portla gelen MAVLink verisi burada yeşil yakmasın!
        if (DB_PARAM_RADIO_MODE == DB_WIFI_MODE_ESPNOW_AIR || 
            DB_PARAM_RADIO_MODE == DB_WIFI_MODE_AP_LR) {
            active = db_status_led_is_activity_recent(now_tick, db_status_led_last_radio_tick);
        } 
        // Diğer modlardaysa seri porta bakabilir
        else {
            active = db_status_led_is_activity_recent(now_tick, db_status_led_last_serial_mavlink_tick);
        }

        if (active) {
            target_g = 255; // Karşı taraftan telsiz paketi akarken Yeşil kırmızı mavi
            target_r = 255;
            target_b = 255;
        } else {
            target_r = 255;  // Karşı taraf kapalıysa/beklemedeyse kırmızı
        }
    }

    // Renk değiştiyse LED'e yeni komut gönder
    if (target_r != current_r || target_g != current_g || target_b != current_b) {
        current_r = target_r;
        current_g = target_g;
        current_b = target_b;
        
        led_strip_set_pixel(led_strip, 0, current_r, current_g, current_b);
        led_strip_refresh(led_strip);
    }
}
