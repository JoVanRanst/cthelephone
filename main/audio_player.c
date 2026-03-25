#include "audio_file.h"
#include "audio_player.h"
#include "cthelephone_definitions.h"
#include "driver/dac_continuous.h"

#include <inttypes.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/dac_continuous.h"
#include "esp_check.h"

// Audio definitions
// Tentickles_audio_table
// DialTone_audio_table
// PhoneRing_audio_table
// busy_tone_be_audio_table
// dial_tone_be_audio_table
// rotary_ring_be_audio_table

static const char *LOG_TAG = "dac_audio_PLAYER";
//---------------------------------------------------------------------------------------------
// AUDIO PLAYER SYNC CONFIGURATION
//---------------------------------------------------------------------------------------------
static bool currently_playing = false;
static dac_continuous_handle_t dac_handle;

#ifdef ASYNC_AUDIO
    static const size_t dummy_table_size = 2048;
    static uint8_t dummy_table[2048] = {0}; // A dummy table used to clear the legacy data in DMA when playing audio asynchronously

    volatile bool  IRAM_ATTR audio_done = false; 
    static bool    IRAM_ATTR audio_loop = false;
    static uint8_t IRAM_ATTR *audio_data = dummy_table;;
    static size_t  IRAM_ATTR audio_size = dummy_table_size;
    static size_t  IRAM_ATTR byte_written = 0;
    static size_t  IRAM_ATTR loaded_bytes = 0;
    static QueueHandle_t IRAM_ATTR interrupt_que = NULL;

    static bool IRAM_ATTR dac_on_convert_done_callback(dac_continuous_handle_t handle, const dac_event_data_t *event, void *user_data)
    {
        QueueHandle_t que = (QueueHandle_t)user_data;
        BaseType_t need_awoke;
        /* When the queue is full, drop the oldest item */
        if (xQueueIsQueueFullFromISR(que)) {
            dac_event_data_t dummy;
            xQueueReceiveFromISR(que, &dummy, &need_awoke);
        }
        loaded_bytes = 0;
        ESP_ERROR_CHECK(dac_continuous_write_asynchronously(handle, event->buf, event->buf_size,
                                                            audio_data + byte_written, audio_size - byte_written,
                                                            &loaded_bytes));
        byte_written += loaded_bytes;
        if (byte_written >= audio_size) {
            if (audio_loop) {
                byte_written = 0;
            } else {
                audio_done = true;
                reset_player();
            }
        }
        
        xQueueSendFromISR(que, event, &need_awoke);
        return need_awoke;
    }
    void IRAM_ATTR dac_write_data(uint8_t *data, size_t size, bool loop) {
        // Setup all parameters for the callback to write the data out
        audio_data = data;
        audio_size = size;
        audio_loop = loop;
        audio_done = false;
        byte_written = 0;
        loaded_bytes = 0;
    }
    void setup_audio_player(void) {
        ESP_LOGI(LOG_TAG, "DAC audio example start");
        ESP_LOGI(LOG_TAG, "--------------------------------------");

        dac_continuous_config_t cont_cfg = {
            .chan_mask = DAC_CHANNEL_MASK_ALL,
            .desc_num = 4,
            .buf_size = 2048,
            .freq_hz = CONFIG_AUDIO_SAMPLE_RATE,
            .offset = 0,
            .clk_src = DAC_DIGI_CLK_SRC_APLL,   // Using APLL as clock source to get a wider frequency range
            .chan_mode = DAC_CHANNEL_MODE_SIMUL,
        };
        /* Allocate continuous channels */
        ESP_ERROR_CHECK(dac_continuous_new_channels(&cont_cfg, &dac_handle));
        /* Create a queue to transport the interrupt event data */
        interrupt_que = xQueueCreate(10, sizeof(dac_event_data_t));
        assert(interrupt_que);
        dac_event_callbacks_t cbs = {
            .on_convert_done = dac_on_convert_done_callback,
            .on_stop = NULL,
        };
        /* Must register the callback if using asynchronous writing */
        ESP_ERROR_CHECK(dac_continuous_register_event_callback(dac_handle, &cbs, interrupt_que));
        // Enable the DAC channels before starting to write data
        ESP_ERROR_CHECK(dac_continuous_enable(dac_handle));
        // Startup the dac
        ESP_ERROR_CHECK(dac_continuous_start_async_writing(dac_handle));
        // Loadup the DMA with dummy data to clear it
        reset_player();
    }
    void reset_player() {
        if(currently_playing == true) {
            ESP_ERROR_CHECK(dac_continuous_disable(dac_handle));
            vTaskDelay(pdMS_TO_TICKS(100)); // Make sure the release has happend
        }
        currently_playing = true;
        ESP_ERROR_CHECK(dac_continuous_enable(dac_handle));
    }
#else
    static void dac_write_data(uint8_t *data, size_t data_size) {
        dac_continuous_write(dac_handle, data, data_size, NULL, 500);
    }
    void setup_audio_player(void)
    {
        ESP_LOGI(LOG_TAG, "DAC audio example start");
        ESP_LOGI(LOG_TAG, "--------------------------------------");

        dac_continuous_config_t cont_cfg = {
            .chan_mask = DAC_CHANNEL_MASK_ALL,
            .desc_num = 4,
            .buf_size = 2048,
            .freq_hz = CONFIG_AUDIO_SAMPLE_RATE,
            .offset = 0,
            .clk_src = DAC_DIGI_CLK_SRC_APLL,   // Using APLL as clock source to get a wider frequency range
            .chan_mode = DAC_CHANNEL_MODE_SIMUL,
        };
        /* Allocate continuous channels */
        ESP_ERROR_CHECK(dac_continuous_new_channels(&cont_cfg, &dac_handle));
        ESP_ERROR_CHECK(dac_continuous_enable(dac_handle));

        /* Flag that the channel has not been enabled yet */
        currently_playing = false;
    }
#endif


// const size_t segment_size_ms = 500; // ms
// const size_t segment_size_bytes = (CONFIG_AUDIO_SAMPLE_RATE * segment_size_ms) / 1000; // Calculate how many bytes correspond to the segment size in ms
// bool play_part_of_sound(uint8_t *data, size_t data_size, uint8_t *segment_pointer) {
//     // Calculate the offset for the current segment and determine how many bytes to write
//     bool is_last_segment = false;
//     size_t bytes_to_write = 0;
//     size_t offset = *segment_pointer * segment_size_bytes;
//     if ((offset + segment_size_bytes) >= data_size) {
//         bytes_to_write = data_size - offset; // Write only the remaining bytes if the segment exceeds the data size
//         *segment_pointer = 0; // Reset segment number for the next play
//         is_last_segment = true;
//     } else {
//         bytes_to_write = segment_size_bytes; // Write a full segment
//         (*segment_pointer)++; // Move to the next segment for the next play
//         is_last_segment = false;
//     }
//     ESP_LOGI(LOG_TAG, "%d bytes, %d offset", bytes_to_write, offset);

//     // Write the calculated segment to the DAC
//     dac_write_data((uint8_t *)data + offset, bytes_to_write);

//     return is_last_segment;
// }

bool play_sound_for_ms(uint8_t *data, size_t data_size, size_t *data_offset_pointer, size_t play_time_ms) {
    // Play the sound in segments until the entire data is played
    bool is_last_segment = false;
    size_t bytes_to_write = 0;
    size_t segment_size_bytes = (CONFIG_AUDIO_SAMPLE_RATE * play_time_ms) / 1000; // Calculate how many bytes correspond to the play time in ms
    if ((*data_offset_pointer + segment_size_bytes) >= data_size) {
        bytes_to_write = data_size - *data_offset_pointer -1; // Write only the remaining bytes if the segment exceeds the data size
        is_last_segment = true;
    } else {
        bytes_to_write = segment_size_bytes; // Write a full segment
        is_last_segment = false;
    }
    // ESP_LOGI(LOG_TAG, "%d bytes, %d offset", bytes_to_write, *data_offset_pointer);
    // Write the calculated segment to the DAC
    dac_write_data(data + *data_offset_pointer, bytes_to_write);
    // ESP_LOGI(LOG_TAG, "Played segment of sound, is last segment: %d", is_last_segment);

    if(is_last_segment) {
        *data_offset_pointer = 0; // Reset offset for the next play
        ESP_ERROR_CHECK(dac_continuous_disable(dac_handle));
        vTaskDelay(pdMS_TO_TICKS(100)); // Make sure the release has happend
        ESP_ERROR_CHECK(dac_continuous_enable(dac_handle));
    } else {
        *data_offset_pointer += bytes_to_write;
    }
    return is_last_segment;
}

static size_t segment_size_ms = 200; // ms
bool play_dialing() {
    ESP_LOGI(LOG_TAG, "Playing DIALING sound");
    ESP_LOGI(LOG_TAG, "Audio size %d bytes, played at frequency %d Hz asynchronously", sizeof(beep_audio_table), CONFIG_AUDIO_SAMPLE_RATE);
    static size_t segment_pointer = 0;

    return play_sound_for_ms((uint8_t *)beep_audio_table, sizeof(beep_audio_table), &segment_pointer, segment_size_ms);
}
bool play_ringing() {
    ESP_LOGI(LOG_TAG, "Playing RINGING sound");
    ESP_LOGI(LOG_TAG, "Audio size %d bytes, played at frequency %d Hz asynchronously", sizeof(ring_audio_table), CONFIG_AUDIO_SAMPLE_RATE);
    static size_t segment_pointer = 0;

    return play_sound_for_ms((uint8_t *)ring_audio_table, sizeof(ring_audio_table), &segment_pointer, segment_size_ms);
}
bool play_busy() {
    ESP_LOGI(LOG_TAG, "Playing BUSY sound");
    ESP_LOGI(LOG_TAG, "Audio size %d bytes, played at frequency %d Hz asynchronously", sizeof(beep_audio_table), CONFIG_AUDIO_SAMPLE_RATE);
    static size_t segment_pointer = 0;

    return play_sound_for_ms((uint8_t *)beep_audio_table, sizeof(beep_audio_table), &segment_pointer, segment_size_ms);
}
bool play_reply() {
    ESP_LOGI(LOG_TAG, "Playing REPLY sound");
    ESP_LOGI(LOG_TAG, "Audio size %d bytes, played at frequency %d Hz asynchronously", sizeof(Tentickles_audio_table), CONFIG_AUDIO_SAMPLE_RATE);
    static size_t segment_pointer = 0;

    return play_sound_for_ms((uint8_t *)Tentickles_audio_table, sizeof(Tentickles_audio_table), &segment_pointer, segment_size_ms);
}