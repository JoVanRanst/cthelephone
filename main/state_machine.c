#include "audio_player.h"
#include "state_machine.h"
#include "cthelephone_definitions.h"

#include <inttypes.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"

static const char *LOG_TAG = "dac_audio_STATE_MACHINE";
//---------------------------------------------------------------------------------------------
// DEFINITIONS AND VARIABLES
//---------------------------------------------------------------------------------------------
// Program states
enum program_state {
    PROGRAM_STATE_BOOT,
    PROGRAM_STATE_IDLE,
    PROGRAM_STATE_RINGING,
    PROGRAM_STATE_DIALING,
    PROGRAM_STATE_RESPONDING,
    PROGRAM_STATE_NULL,
};
enum songs {
    SONG_DIALING,
    SONG_RINGING,
    SONG_BUSY,
    SONG_RESPONSE,
};

static enum program_state current_state = PROGRAM_STATE_BOOT;
static enum program_state previous_state = PROGRAM_STATE_NULL;

//---------------------------------------------------------------------------------------------
// INTERRUPT FUNCTIONS
//---------------------------------------------------------------------------------------------
static volatile int  pulse_count    = 0;
static volatile bool pulse_detected = false;
static volatile bool horn_picked_up = false;
// Handle rotary dial pulse detection
static void IRAM_ATTR rotary_isr_handler(void* arg) {
    pulse_count++;
    pulse_detected = true;
}
// Handle horn pulse detection
static void IRAM_ATTR horn_isr_handler(void* arg) {
    if(gpio_get_level(HORN_GPIO) == 1) {
        horn_picked_up = true;
    } else {
        horn_picked_up = false;
    }
}

//---------------------------------------------------------------------------------------------
// HELPER FUNCTIONS
//---------------------------------------------------------------------------------------------
// RGB control function
void set_RGB_color(bool red, bool green, bool blue)
{
    gpio_set_level(RED_LED_GPIO, red ? 0 : 1);
    gpio_set_level(GREEN_LED_GPIO, green ? 0 : 1);
    gpio_set_level(BLUE_LED_GPIO, blue ? 0 : 1);
}
void call_id_led(bool on) {
    gpio_set_level(CALL_ID_GPIO, on ? 0 : 1);
}
// Handles all things switching states
void update_state(enum program_state new_state) {
    previous_state = current_state;
    current_state = new_state;
}

//---------------------------------------------------------------------------------------------
// STATES
//---------------------------------------------------------------------------------------------
// The boot state is to setup anything that only
// needs to, or can be, setup once
void state_boot() {
    ESP_LOGI(LOG_TAG, "=> PROGRAM_STATE_BOOT");
    set_RGB_color(true, false, false); // Red for booting
    if (previous_state == PROGRAM_STATE_NULL) {
        ESP_LOGI(LOG_TAG, "=> came from NULL state, setting up hardware");
        // Setup hardware
        gpio_install_isr_service(0);
        gpio_isr_handler_add(ROTARY_GPIO, rotary_isr_handler, NULL);
        gpio_isr_handler_add(HORN_GPIO, horn_isr_handler, NULL);
        call_id_led(false); // Ensure CALL ID LED is off in idle state

        update_state(PROGRAM_STATE_IDLE);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void state_idle() {
    static uint16_t idle_time_sec= 480;
    static uint16_t idle_timer_counter = 0; // Counter for idle timer
    // In this state the program waits for user input
    ESP_LOGI(LOG_TAG, "=> PROGRAM_STATE_IDLE");
    set_RGB_color(true, true, true); // White for idle
    call_id_led(false); // Ensure CALL ID LED is off in idle state
    play_silence();
    if (horn_picked_up) {
        ESP_LOGI(LOG_TAG, "=> horn detected, transitioning to DIALING state");

        update_state(PROGRAM_STATE_DIALING);
    } else {
        // If nothing happens for a certain amount of time, RINg for attention
        idle_timer_counter++;
        if (idle_timer_counter >= (idle_time_sec*10)) {
            ESP_LOGI(LOG_TAG, "=> idle timer expired, transitioning to RINGING state");

            update_state(PROGRAM_STATE_RINGING);
            idle_timer_counter = 0;
        }
    }
    vTaskDelay(pdMS_TO_TICKS(100)); // Adjust the delay as needed to control how often the state is checked
}

void state_ringing() {
    ESP_LOGI(LOG_TAG, "=> PROGRAM_STATE_RINGING");
    set_RGB_color(true, true, false); // Yellow for ringing

    play_ringing();
    while (1) {
        if (horn_picked_up) {
            ESP_LOGI(LOG_TAG, "=> horn detected, transitioning to DIALING state");
            play_silence();
            update_state(PROGRAM_STATE_DIALING);
            return;
        }
        if (audio_playback_finished()) {
            play_silence();
            update_state(PROGRAM_STATE_IDLE);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void state_dialing() {
    // In this state the program processes the dialing input
    ESP_LOGI(LOG_TAG, "=> PROGRAM_STATE_DIALING");
    set_RGB_color(false, false, true); // Blue for dialing
    // Setup a timer to detect when the user has finished and reset the pulse count
    pulse_count = 0; // Reset pulse count at the start of dialing
    pulse_detected = false; // Reset pulse detected flag
    uint8_t sixes_count = 0; // Counter for consecutive '6' digits
    // Start playing dialing tone and wait for the user to finish dialing
    play_busy();
    call_id_led(true); // Turn on CALL ID LED to indicate response is playing
    while(horn_picked_up) {
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
        }
        if (sixes_count >= 3) {
            ESP_LOGI(LOG_TAG, "=> three '6's detected, transitioning to RESPONDING state");
            for (int i = 0; i < 2; i++) {
                play_beep(); // Play the Beep 2 times
                while (1) {
                    if (!horn_picked_up) {
                        ESP_LOGI(LOG_TAG, "=> horn put down, ending reply early");
                        play_silence();
                        update_state(PROGRAM_STATE_IDLE);
                        return;
                    }
                    if (audio_playback_finished()) {
                        play_silence();
                        break;
                    }
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
            }
            update_state(PROGRAM_STATE_RESPONDING);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // Adjust the delay as needed to control how often the state is checked
    }
    if(sixes_count < 3) {
        update_state(PROGRAM_STATE_IDLE);
    }
}

void state_response() {
    ESP_LOGI(LOG_TAG, "=> PROGRAM_STATE_RESPONDING");
    set_RGB_color(true, false, true); // Purple for responding

    play_reply();
    while (1) {
        if (!horn_picked_up) {
            ESP_LOGI(LOG_TAG, "=> horn put down, ending reply early");
            play_silence();
            update_state(PROGRAM_STATE_IDLE);
            return;
        }
        if (audio_playback_finished()) {
            play_silence();
            update_state(PROGRAM_STATE_IDLE);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

//---------------------------------------------------------------------------------------------
// MAIN STATE MACHINE FUNCTION
//---------------------------------------------------------------------------------------------
void state_machine_run()
{
    while (1) {
        switch (current_state)
        {
            case PROGRAM_STATE_BOOT:
                state_boot();
                break;
            case PROGRAM_STATE_IDLE:
                state_idle();
                break;
            case PROGRAM_STATE_RINGING:
                state_ringing();
                break;
            case PROGRAM_STATE_DIALING:
                state_dialing();
                break;
            case PROGRAM_STATE_RESPONDING:
                state_response();
                break;
            default:
                // Handle unexpected states
                ESP_LOGI(LOG_TAG, "=> ERROR: current program state is invalid");
                update_state(PROGRAM_STATE_BOOT);

                // set_RGB_color(true, true, true); // White for error
                break;
        }
    }
}