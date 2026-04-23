#include "audio_file.h"
#include "audio_player.h"
#include "cthelephone_definitions.h"
#include "driver/dac_continuous.h"

#include <inttypes.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/dac_continuous.h"
#include "esp_check.h"

static const char *LOG_TAG = "dac_audio_PLAYER";

// Silence buffer for muting output
#define SILENCE_BUFFER_SIZE 2048
static uint8_t silence_buffer[SILENCE_BUFFER_SIZE] = {0};
//---------------------------------------------------------------------------------------------
// AUDIO PLAYER SYNC CONFIGURATION
//---------------------------------------------------------------------------------------------
typedef struct {
    uint8_t *data;
    size_t size;
} dac_audio_stream_t;

static dac_audio_stream_t current_stream;
// Track playback progress for polling
static volatile size_t playback_bytes_written = 0;
static volatile size_t playback_total_size = 0;
static SemaphoreHandle_t stream_mutex;

void dac_set_audio_stream(uint8_t *new_data, size_t new_size) {
    xSemaphoreTake(stream_mutex, portMAX_DELAY);
    current_stream.data = new_data;
    current_stream.size = new_size;
    playback_bytes_written = 0;
    playback_total_size = new_size;
    xSemaphoreGive(stream_mutex);
}

// Polling function: returns true if the current sound has finished playing
bool audio_playback_finished(void) {
    // If silence is playing, always return true
    if (current_stream.data == silence_buffer) return true;
    return playback_bytes_written >= playback_total_size;
}

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

static void dac_playback_task(void *param)
{
    struct {
        dac_continuous_handle_t handle;
        QueueHandle_t que;
    } *ctx = param;
    while (1) {
        // Always get the current stream before starting playback
        xSemaphoreTake(stream_mutex, portMAX_DELAY);
        uint8_t *data = current_stream.data;
        size_t data_size = current_stream.size;
        xSemaphoreGive(stream_mutex);

        ESP_LOGI(LOG_TAG, "Audio size %d bytes, played at frequency %d Hz asynchronously", data_size, CONFIG_AUDIO_SAMPLE_RATE);
        dac_event_data_t evt_data;
        size_t byte_written = 0;
        while (byte_written < data_size) {
            // Check for stream change before each buffer fill
            xSemaphoreTake(stream_mutex, portMAX_DELAY);
            uint8_t *cur_data = current_stream.data;
            size_t cur_size = current_stream.size;
            xSemaphoreGive(stream_mutex);
            if (cur_data != data || cur_size != data_size) {
                // Stream changed, restart playback from new buffer
                data = cur_data;
                data_size = cur_size;
                byte_written = 0;
                ESP_LOGI(LOG_TAG, "Stream changed, restarting playback");
                continue;
            }
            xQueueReceive(ctx->que, &evt_data, portMAX_DELAY);
            size_t loaded_bytes = 0;
            ESP_ERROR_CHECK(dac_continuous_write_asynchronously(ctx->handle, evt_data.buf, evt_data.buf_size,
                                                                data + byte_written, data_size - byte_written, &loaded_bytes));
            byte_written += loaded_bytes;
            // Track progress for polling
            playback_bytes_written = byte_written;
        }
        // ...existing code for clearing DMA and delay...
        for (int i = 0; i < 4; i++) {
            xQueueReceive(ctx->que, &evt_data, portMAX_DELAY);
            memset(evt_data.buf, 0, evt_data.buf_size);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void setup_audio_player(void)
{
    ESP_LOGI(LOG_TAG, "DAC async audio setup");

    dac_continuous_handle_t dac_handle;
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
    QueueHandle_t que = xQueueCreate(10, sizeof(dac_event_data_t));
    assert(que);
    dac_event_callbacks_t cbs = {
        .on_convert_done = dac_on_convert_done_callback,
        .on_stop = NULL,
    };
    /* Must register the callback if using asynchronous writing */
    ESP_ERROR_CHECK(dac_continuous_register_event_callback(dac_handle, &cbs, que));
    /* Enable the continuous channels */
    ESP_ERROR_CHECK(dac_continuous_enable(dac_handle));
    ESP_LOGI(LOG_TAG, "DAC initialized success, DAC DMA is ready");

    // Create mutex and set initial stream
    stream_mutex = xSemaphoreCreateMutex();
    assert(stream_mutex);
    current_stream.data = (uint8_t *)silence_buffer; // Start with silence
    current_stream.size = SILENCE_BUFFER_SIZE;

    ESP_ERROR_CHECK(dac_continuous_start_async_writing(dac_handle));

    // Start playback task
    static struct {
        dac_continuous_handle_t handle;
        QueueHandle_t que;
    } playback_ctx;
    playback_ctx.handle = dac_handle;
    playback_ctx.que = que;
    xTaskCreate(dac_playback_task, "dac_playback_task", 4096, &playback_ctx, 5, NULL);
}

void play_dialing() {
    ESP_LOGI(LOG_TAG, "Playing DIALING sound");
    ESP_LOGI(LOG_TAG, "Audio size %d bytes, played at frequency %d Hz asynchronously", sizeof(dialtone_opti_audio_table), CONFIG_AUDIO_SAMPLE_RATE);
    dac_set_audio_stream((uint8_t *)dialtone_opti_audio_table, sizeof(dialtone_opti_audio_table));
}
void play_ringing() {
    ESP_LOGI(LOG_TAG, "Playing RINGING sound");
    ESP_LOGI(LOG_TAG, "Audio size %d bytes, played at frequency %d Hz asynchronously", sizeof(ring_opti_audio_table), CONFIG_AUDIO_SAMPLE_RATE);
    dac_set_audio_stream((uint8_t *)ring_opti_audio_table, sizeof(ring_opti_audio_table));
}
void play_busy() {
    ESP_LOGI(LOG_TAG, "Playing BUSY sound");
    ESP_LOGI(LOG_TAG, "Audio size %d bytes, played at frequency %d Hz asynchronously", sizeof(dialtone_opti_audio_table), CONFIG_AUDIO_SAMPLE_RATE);
    dac_set_audio_stream((uint8_t *)dialtone_opti_audio_table, sizeof(dialtone_opti_audio_table));
}
void play_reply() {
    ESP_LOGI(LOG_TAG, "Playing REPLY sound");
    ESP_LOGI(LOG_TAG, "Audio size %d bytes, played at frequency %d Hz asynchronously", sizeof(response_opti_audio_table), CONFIG_AUDIO_SAMPLE_RATE);
    dac_set_audio_stream((uint8_t *)response_opti_audio_table, sizeof(response_opti_audio_table));
}

void play_silence() {
    ESP_LOGI(LOG_TAG, "Playing SILENCE (muted)");
    dac_set_audio_stream(silence_buffer, SILENCE_BUFFER_SIZE);
}