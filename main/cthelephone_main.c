/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "audio_example_file.h"
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

static const char *LOG_TAG = "dac_audio";

#if CONFIG_EXAMPLE_DAC_WRITE_ASYNC
static bool IRAM_ATTR  dac_on_convert_done_callback(dac_continuous_handle_t handle, const dac_event_data_t *event, void *user_data)
{
    QueueHandle_t que = (QueueHandle_t)user_data;
    BaseType_t need_awoke;
    /* When the queue is full, drop the oldest item */
    if (xQueueIsQueueFullFromISR(que)) {
        dac_event_data_t dummy;
        xQueueReceiveFromISR(que, &dummy, &need_awoke);
    }
    /* Send the event from callback */
    xQueueSendFromISR(que, event, &need_awoke);
    return need_awoke;
}

static void dac_write_data_asynchronously(dac_continuous_handle_t handle, QueueHandle_t que, uint8_t *data, size_t data_size)
{
    ESP_LOGI(LOG_TAG, "Audio size %d bytes, played at frequency %d Hz asynchronously", data_size, CONFIG_EXAMPLE_AUDIO_SAMPLE_RATE);
    uint32_t cnt = 1;
    while (1) {
        printf("Play count: %"PRIu32"\n", cnt++);
        dac_event_data_t evt_data;
        size_t byte_written = 0;
        /* Receive the event from callback and load the data into the DMA buffer until the whole audio loaded */
        while (byte_written < data_size) {
            xQueueReceive(que, &evt_data, portMAX_DELAY);
            size_t loaded_bytes = 0;
            ESP_ERROR_CHECK(dac_continuous_write_asynchronously(handle, evt_data.buf, evt_data.buf_size,
                                                                data + byte_written, data_size - byte_written, &loaded_bytes));
            byte_written += loaded_bytes;
        }
        /* Clear the legacy data in DMA, clear times equal to the 'dac_continuous_config_t::desc_num' */
        for (int i = 0; i < 4; i++) {
            xQueueReceive(que, &evt_data, portMAX_DELAY);
            memset(evt_data.buf, 0, evt_data.buf_size);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#else
static void dac_write_data_synchronously(dac_continuous_handle_t handle, uint8_t *data, size_t data_size)
{
    ESP_LOGI(LOG_TAG, "Audio size %d bytes, played at frequency %d Hz synchronously", data_size, CONFIG_EXAMPLE_AUDIO_SAMPLE_RATE);
    uint32_t cnt = 1;
    printf("Play count: %"PRIu32"\n", cnt++);
    ESP_ERROR_CHECK(dac_continuous_write(handle, data, data_size, NULL, -1));
}
#endif

#define DIGIT_END_TIMEOUT_MS 500

static volatile int pulse_count = 0;
static volatile bool pulse_detected = false;
static volatile bool horn_detected = false;
// Handle rotary dial pulse detection
static void IRAM_ATTR rotary_isr_handler(void* arg) {
    pulse_count++;
    pulse_detected = true;
}
// Define program states
enum program_state {
    PROGRAM_STATE_BOOT,
    PROGRAM_STATE_IDLE,
    PROGRAM_STATE_DIALING,
    PROGRAM_STATE_RESPONDING,
    PROGRAM_STATE_NULL,
};

#define ROTARY_GPIO 27
#define HORN_GPIO 22
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
        .intr_type = GPIO_INTR_DISABLE, // or GPIO_INTR_NEGEDGE depending on your wiring
    };
    gpio_config(&io_conf_horn);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(ROTARY_GPIO, rotary_isr_handler, NULL);
}

#define RED_LED_GPIO   4
#define GREEN_LED_GPIO 16
#define BLUE_LED_GPIO  17
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

void set_RGB_color(bool red, bool green, bool blue)
{
    gpio_set_level(RED_LED_GPIO, red ? 0 : 1);
    gpio_set_level(GREEN_LED_GPIO, green ? 0 : 1);
    gpio_set_level(BLUE_LED_GPIO, blue ? 0 : 1);
}

void play_audio(void)
{
    dac_continuous_handle_t dac_handle;
    dac_continuous_config_t cont_cfg = {
        .chan_mask = DAC_CHANNEL_MASK_ALL,
        .desc_num = 4,
        .buf_size = 2048,
        .freq_hz = CONFIG_EXAMPLE_AUDIO_SAMPLE_RATE,
        .offset = 0,
        .clk_src = DAC_DIGI_CLK_SRC_APLL,   // Using APLL as clock source to get a wider frequency range
        /* Assume the data in buffer is 'A B C D E F'
        * DAC_CHANNEL_MODE_SIMUL:
        *      - channel 0: A B C D E F
        *      - channel 1: A B C D E F
        * DAC_CHANNEL_MODE_ALTER:
        *      - channel 0: A C E
        *      - channel 1: B D F
        */
        .chan_mode = DAC_CHANNEL_MODE_SIMUL,
    };
    /* Allocate continuous channels */
    ESP_ERROR_CHECK(dac_continuous_new_channels(&cont_cfg, &dac_handle));
#if CONFIG_EXAMPLE_DAC_WRITE_ASYNC
    /* Create a queue to transport the interrupt event data */
    QueueHandle_t que = xQueueCreate(10, sizeof(dac_event_data_t));
    assert(que);
    dac_event_callbacks_t cbs = {
        .on_convert_done = dac_on_convert_done_callback,
        .on_stop = NULL,
    };
    /* Must register the callback if using asynchronous writing */
    ESP_ERROR_CHECK(dac_continuous_register_event_callback(dac_handle, &cbs, que));
#endif
    /* Enable the continuous channels */
    ESP_ERROR_CHECK(dac_continuous_enable(dac_handle));
    ESP_LOGI(LOG_TAG, "DAC initialized success, DAC DMA is ready");

    size_t audio_size = sizeof(audio_table);
#if CONFIG_EXAMPLE_DAC_WRITE_ASYNC
    ESP_ERROR_CHECK(dac_continuous_start_async_writing(dac_handle));
    dac_write_data_asynchronously(dac_handle, que, (uint8_t *)audio_table, audio_size);
#else
    dac_write_data_synchronously(dac_handle, (uint8_t *)audio_table, audio_size);
#endif
}

void number_counter(void)
{
    // This function would handle the logic for counting the pulses from the rotary dial
    // It could use a timer to determine when the user has finished dialing a digit
    // For example, if no new pulses are detected for DIGIT_END_TIMEOUT_MS, it could process the current pulse_count as a digit
}

void dialed_number_handler(void)
{
    // This function would handle the logic for processing the dialed number
    // For example, it could convert pulse_count to a digit and store it in a buffer
    // It could also reset pulse_count after processing
}

void app_main(void)
{
    enum program_state current_state = PROGRAM_STATE_BOOT;
    enum program_state previous_state = PROGRAM_STATE_NULL;

    ESP_LOGI(LOG_TAG, "---------------------------");
    ESP_LOGI(LOG_TAG, "|    CTHELEPHONE START    |");
    ESP_LOGI(LOG_TAG, "---------------------------");

    while (1) {
        switch (current_state)
        {
            case PROGRAM_STATE_BOOT:
                ESP_LOGI(LOG_TAG, "=> PROGRAM_STATE_BOOT");
                if (previous_state == PROGRAM_STATE_NULL)
                {
                    ESP_LOGI(LOG_TAG, "=> came from NULL state, setting up hardware");
                    setup_phoneIO();
                    setup_RGBIO();
                    set_RGB_color(true, false, false); // Red for booting
                    previous_state = current_state;
                    current_state = PROGRAM_STATE_IDLE;
                }
                vTaskDelay(pdMS_TO_TICKS(2000));
                break;
            case PROGRAM_STATE_IDLE:
                // In this state the program waits for user input
                ESP_LOGI(LOG_TAG, "=> PROGRAM_STATE_IDLE");
                set_RGB_color(true, true, true); // White for idle
                if (gpio_get_level(HORN_GPIO) == 1) {
                    ESP_LOGI(LOG_TAG, "=> horn detected, transitioning to DIALING state");
                    previous_state = current_state;
                    current_state = PROGRAM_STATE_DIALING;
                }
                break;
            case PROGRAM_STATE_DIALING:
                // In this state the program processes the dialing input
                ESP_LOGI(LOG_TAG, "=> PROGRAM_STATE_DIALING");
                set_RGB_color(false, false, true); // Blue for dialing
                // Setup a timer to detect when the user has finished and reset the pulse count
                pulse_count = 0; // Reset pulse count at the start of dialing
                pulse_detected = false; // Reset pulse detected flag
                uint8_t sixes_count = 0; // Counter for consecutive '6' digits
                while(gpio_get_level(HORN_GPIO) == 1) {
                    if (pulse_detected) {
                        ESP_LOGI(LOG_TAG, "=> pulse detected, transitioning to DIALING state");
                        // Give the dial time to rotate
                        vTaskDelay(pdMS_TO_TICKS(1500));

                        ESP_LOGI(LOG_TAG, "=> number dialed: %d", pulse_count);
                        if(pulse_count == 6) {
                            sixes_count++;
                            ESP_LOGI(LOG_TAG, "=> '6' detected, current count: %d", sixes_count);
                        } else {
                            sixes_count = 0; // Reset counter if a different digit is dialed
                        }
                        
                        pulse_detected = false;
                        pulse_count = 0; // Reset pulse count for the next digit
                        current_state = PROGRAM_STATE_DIALING;
                    }
                    if (sixes_count >= 3) {
                        ESP_LOGI(LOG_TAG, "=> three '6's detected, transitioning to RESPONDING state");
                        previous_state = current_state;
                        current_state = PROGRAM_STATE_RESPONDING;
                        break;
                    }
                    // Wait for the horn to be released before processing pulses
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                if(sixes_count < 3) {
                    previous_state = current_state;
                    current_state = PROGRAM_STATE_IDLE;
                }
                break;
            case PROGRAM_STATE_RESPONDING:
                // In this state the program plays audio in response to the dialing input
                ESP_LOGI(LOG_TAG, "=> PROGRAM_STATE_RESPONDING");
                set_RGB_color(true, false, true); // Purple for responding
                while(gpio_get_level(HORN_GPIO) == 1) {
                    // Wait for the horn to be released before playing audio
                    play_audio();
                    vTaskDelay(pdMS_TO_TICKS(1000));
                }
                previous_state = current_state;
                current_state = PROGRAM_STATE_IDLE;
                break;
            default:
                // Handle unexpected states
                ESP_LOGI(LOG_TAG, "=> ERROR: current program state is invalid");
                // set_RGB_color(true, true, true); // White for error
                break;
        }
    }
}
