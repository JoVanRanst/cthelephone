/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "audio_player.h"
#include "cthelephone_definitions.h"
#include "state_machine.h"

#include "driver/dac_continuous.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include <inttypes.h>
#include <string.h>


static const char *LOG_TAG = "dac_audio_MAIN";
//---------------------------------------------------------------------------------------------
// SETUP FUNCTIONS
//---------------------------------------------------------------------------------------------
// Setup GPIO for rotary dial and horn button
void setup_phoneIO(void)
{
    gpio_config_t io_conf_rotary = {
        .pin_bit_mask = 1ULL << ROTARY_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_POSEDGE, // or GPIO_INTR_NEGEDGE depending on your wiring
    };
    gpio_config(&io_conf_rotary);
    gpio_config_t io_conf_horn = {
        .pin_bit_mask = 1ULL << HORN_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE, // or GPIO_INTR_NEGEDGE depending on your wiring
    };
    gpio_config(&io_conf_horn);
}
void setup_RGBIO(void)
{
    gpio_config_t io_conf_red = {
        .pin_bit_mask = 1ULL << RED_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf_red);

    gpio_config_t io_conf_green = {
        .pin_bit_mask = 1ULL << GREEN_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf_green);

    gpio_config_t io_conf_blue = {
        .pin_bit_mask = 1ULL << BLUE_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf_blue);
}

//---------------------------------------------------------------------------------------------
// MAIN APPLICATION
//---------------------------------------------------------------------------------------------
void app_main(void)
{
    ESP_LOGI(LOG_TAG, "---------------------------");
    ESP_LOGI(LOG_TAG, "|    CTHELEPHONE START    |");
    ESP_LOGI(LOG_TAG, "---------------------------");

    // Setup hardware
    setup_phoneIO();
    setup_RGBIO();
    setup_audio_player();

    // Start up the state machine
    state_machine_run();

    ESP_LOGI(LOG_TAG, "-------------------------");
    ESP_LOGI(LOG_TAG, "|    CTHELEPHONE END    |");
    ESP_LOGI(LOG_TAG, "-------------------------");
}
