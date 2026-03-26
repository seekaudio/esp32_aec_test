#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_littlefs.h"
#include "esp_timer.h"
#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "wave_file.h"
#include "seekaudio_defines.h"

static const char* TAG = "AEC_DEMO";

// ==================== UART ====================

static void init_uart(void)
{
    uart_config_t cfg = {
        .baud_rate  = UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config(UART_PORT, &cfg);
    uart_set_pin(UART_PORT, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_PORT, 2048, 0, 0, NULL, 0);
    AEC_LOGI(TAG, "UART init: %d baud", UART_BAUD);
}

// Send output file via UART in chunks
static esp_err_t uart_send_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        AEC_LOGE(TAG, "UART send: cannot open %s", path);
        return ESP_FAIL;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint16_t header = DATA_HEADER;
    uint32_t dlen   = (uint32_t)fsize;
    uint16_t footer = DATA_FOOTER;

    uart_write_bytes(UART_PORT, &header, 2);
    uart_write_bytes(UART_PORT, &dlen,   4);

    uint8_t *chunk = (uint8_t *)malloc(UART_CHUNK_BYTES);
    if (!chunk) {
        AEC_LOGE(TAG, "UART send: OOM");
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    size_t total_sent = 0;
    while (total_sent < (size_t)fsize) {
        size_t to_read = UART_CHUNK_BYTES;
        if (total_sent + to_read > (size_t)fsize) {
            to_read = fsize - total_sent;
        }
        size_t rd = fread(chunk, 1, to_read, f);
        if (rd == 0) break;
        uart_write_bytes(UART_PORT, chunk, rd);
        uart_wait_tx_done(UART_PORT, portMAX_DELAY);
        total_sent += rd;
    }

    free(chunk);
    fclose(f);

    uart_write_bytes(UART_PORT, &footer, 2);
    uart_wait_tx_done(UART_PORT, portMAX_DELAY);

    AEC_LOGI(TAG, "UART: sent %zu bytes", total_sent);
    return ESP_OK;
}

// ==================== LittleFS ====================

static esp_err_t init_littlefs(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path              = "/littlefs",
        .partition_label        = "storage",
        .format_if_mount_failed = true,
        .dont_mount             = false,
    };
    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        AEC_LOGE(TAG, "LittleFS mount failed: %s", esp_err_to_name(ret));
        return ret;
    }
    size_t total = 0, used = 0;
    esp_littlefs_info(conf.partition_label, &total, &used);
    AEC_LOGI(TAG, "LittleFS: total=%zu used=%zu", total, used);
    return ESP_OK;
}

// ==================== WAV file reading ====================

/**
 * @brief 校验WAV文件格式，仅允许：
 *        - 单通道（mono）
 *        - 采样率 8000 Hz 或 16000 Hz
 *        - 16-bit PCM
 *
 * @return ESP_OK 校验通过，ESP_FAIL 不符合要求
 */
static esp_err_t validate_wav(WavReader &reader, const char *path)
{
    if (reader.Channels() != 1) {
        AEC_LOGE(TAG, "%s: only mono (1ch) supported, got %u ch", path, reader.Channels());
        return ESP_FAIL;
    }

    uint32_t sr = reader.SampleRate();
    if (sr != 8000 && sr != 16000) {
        AEC_LOGE(TAG, "%s: only 8000/16000 Hz supported, got %lu Hz", path, sr);
        return ESP_FAIL;
    }

    if (reader.BitsPerSample() != 16) {
        AEC_LOGE(TAG, "%s: only 16-bit PCM supported, got %u bit", path, reader.BitsPerSample());
        return ESP_FAIL;
    }

    AEC_LOGI(TAG, "%s: %lu Hz, %u ch, %u bit, %lu samples (%.2f s)",
             path, sr, reader.Channels(), reader.BitsPerSample(),
             reader.Length(), (float)reader.Length() / sr);
    return ESP_OK;
}

/**
 * @brief 读取WAV文件音频数据到PSRAM
 *        调用前须先 Open()，函数内部完成校验、读取，失败时自动 Close()
 */
static esp_err_t read_wav_to_psram(const char *path, int16_t **buf,
                                    size_t *samples, uint32_t *sample_rate)
{
    WavReader reader;

    if (!reader.Open(path)) {
        AEC_LOGE(TAG, "Cannot open WAV: %s", path);
        return ESP_FAIL;
    }

    if (validate_wav(reader, path) != ESP_OK) {
        reader.Close();
        return ESP_FAIL;
    }

    *sample_rate = reader.SampleRate();
    size_t n     = reader.Length();

    *buf = (int16_t *)heap_caps_malloc(n * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!*buf) {
        AEC_LOGE(TAG, "PSRAM alloc failed for %s (%zu bytes)", path, n * sizeof(int16_t));
        reader.Close();
        return ESP_ERR_NO_MEM;
    }

    int rd = reader.Read(*buf, (int)n);
    reader.Close();

    if (rd <= 0 || (size_t)rd != n) {
        AEC_LOGW(TAG, "%s: expected %zu samples, got %d", path, n, rd);
        // 仍继续，以实际读到的为准
    }

    *samples = (size_t)rd;
    AEC_LOGI(TAG, "Loaded %s to PSRAM: %zu samples", path, *samples);
    return ESP_OK;
}

// ==================== WAV file output ====================

static esp_err_t write_wav_from_psram(const char *path, const int16_t *buf,
                                       size_t samples, uint32_t sample_rate)
{
    WavWriter writer;
    if (!writer.Open(path, (int)sample_rate, 1, 16, FormatTag_PCM)) {
        AEC_LOGE(TAG, "Cannot create WAV: %s", path);
        return ESP_FAIL;
    }
    // WavWriter::Write(short*, int) expects non-const; cast is safe (no modification)
    writer.Write((short *)buf, (int)samples);
    writer.Close();
    AEC_LOGI(TAG, "Saved %s: %zu samples @ %lu Hz", path, samples, sample_rate);
    return ESP_OK;
}

// ==================== AEC processing ====================

static esp_err_t run_aec(const int16_t *near, const int16_t *far,
                          size_t mono_samples, uint32_t sample_rate,
                          int16_t **out_buf, size_t *out_samples)
{
    
 #if  USE_AFE_TYPE_SR
    afe_type_t aec_type = (sample_rate == 8000) ? AFE_TYPE_VC_8K : AFE_TYPE_SR;
#else
    afe_type_t aec_type = (sample_rate == 8000) ? AFE_TYPE_VC_8K : AFE_TYPE_VC;
#endif

    AEC_LOGI(TAG, "AEC backend     : %s", AEC_BACKEND_STR);

    aec_handle_unified_t *aec = AEC_CREATE(AEC_INPUT_FORMAT, AEC_FILTER_LEN,
                                            aec_type, AEC_EFFECTIVE_MODE);
    if (!aec) {
        AEC_LOGE(TAG, "AEC create failed");
        return ESP_FAIL;
    }

    int frame_samples = AEC_CHUNKSIZE(aec);
    AEC_LOGI(TAG, "AEC frame size: %d samples (%.1f ms)",
             frame_samples, (float)frame_samples / sample_rate * 1000.0f);

    size_t total_frames = mono_samples / frame_samples;
    size_t proc_samples = total_frames * frame_samples;
    float  audio_sec    = (float)proc_samples / sample_rate;

    AEC_LOGI(TAG, "Processing: %zu frames / %zu samples / %.2f s",
             total_frames, proc_samples, audio_sec);

    int16_t *interleaved = (int16_t *)heap_caps_aligned_alloc(
        16, frame_samples * 2 * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    int16_t *frame_out = (int16_t *)heap_caps_aligned_alloc(
        16, frame_samples * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    *out_buf = (int16_t *)heap_caps_malloc(proc_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM);

    if (!interleaved || !frame_out || !*out_buf) {
        AEC_LOGE(TAG, "OOM: buffers (interleaved=%p frame_out=%p out=%p)",
                 interleaved, frame_out, *out_buf);
        if (interleaved) free(interleaved);
        if (frame_out)   free(frame_out);
        if (*out_buf)  { heap_caps_free(*out_buf); *out_buf = NULL; }
        AEC_DESTROY(aec);
        return ESP_ERR_NO_MEM;
    }

    int64_t delay_us = 0;
    int64_t t_start  = esp_timer_get_time();

    for (size_t f = 0; f < total_frames; f++) {
        const int16_t *near_ptr = near + f * frame_samples;
        const int16_t *far_ptr  = far  + f * frame_samples;

        // Build interleaved MR frame (both backends accept this format;
        // seekaudio_afe_aec wrapper de-interleaves internally)
        for (int s = 0; s < frame_samples; s++) {
            interleaved[s * 2 + 0] = near_ptr[s];
            interleaved[s * 2 + 1] = far_ptr[s];
        }

        AEC_PROCESS(aec, interleaved, frame_out);

        memcpy(*out_buf + f * frame_samples,
               frame_out, frame_samples * sizeof(int16_t));

        if (f % 100 == 0) {
            int64_t d0 = esp_timer_get_time();
            vTaskDelay(1);
            delay_us += esp_timer_get_time() - d0;
        }
    }

    float elapsed = (esp_timer_get_time() - t_start - delay_us) / 1e6f;
    float rtf     = audio_sec / elapsed;
    float kbps    = (proc_samples * sizeof(int16_t) * 8) / (elapsed * 1000.0f);

    AEC_LOGI(TAG, "-------- AEC Performance --------");
    AEC_LOGI(TAG, "Backend         : %s", AEC_BACKEND_STR);
    AEC_LOGI(TAG, "Sample rate     : %lu Hz", sample_rate);
    AEC_LOGI(TAG, "Audio duration  : %.2f s", audio_sec);
    AEC_LOGI(TAG, "Process time    : %.2f s", elapsed);
    AEC_LOGI(TAG, "Real-time factor: %.2fx", rtf);
    AEC_LOGI(TAG, "Throughput      : %.0f samples/s  (%.2f kbps)",
             proc_samples / elapsed, kbps);
    AEC_LOGI(TAG, "---------------------------------");

    *out_samples = proc_samples;

    free(interleaved);
    free(frame_out);
    AEC_DESTROY(aec);
    return ESP_OK;
}

// ==================== app_main ====================

extern "C" void app_main(void)
{
    AEC_LOGI(TAG, "AEC Test Demo Start");
    AEC_LOGI(TAG, "Free PSRAM: %zu bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    AEC_LOGI(TAG, "Free RAM  : %zu bytes", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    int16_t *near   = NULL;
    int16_t *far    = NULL;
    int16_t *output = NULL;
    size_t   near_n = 0, far_n = 0, out_n = 0;
    uint32_t near_sr = 0, far_sr = 0;

    init_uart();

    if (init_littlefs() != ESP_OK) {
        AEC_LOGE(TAG, "LittleFS init failed");
        goto cleanup;
    }

    // Load WAV files into PSRAM (format validation included)
    if (read_wav_to_psram(NEAR_END_FILE, &near, &near_n, &near_sr) != ESP_OK) {
        goto cleanup;
    }
    if (read_wav_to_psram(FAR_END_FILE, &far, &far_n, &far_sr) != ESP_OK) {
        goto cleanup;
    }

    // near/far must have the same sample rate
    if (near_sr != far_sr) {
        AEC_LOGE(TAG, "Sample rate mismatch: near=%lu Hz, far=%lu Hz", near_sr, far_sr);
        goto cleanup;
    }

    {
        size_t mono_samples = (near_n < far_n) ? near_n : far_n;
        if (near_n != far_n) {
            AEC_LOGW(TAG, "Length mismatch near=%zu far=%zu, using %zu",
                     near_n, far_n, mono_samples);
        }

        // Run AEC
        if (run_aec(near, far, mono_samples, near_sr, &output, &out_n) != ESP_OK) {
            AEC_LOGE(TAG, "AEC processing failed");
            goto cleanup;
        }
    }

    // Save output to LittleFS as WAV
    if (write_wav_from_psram(OUTPUT_FILE, output, out_n, near_sr) != ESP_OK) {
        AEC_LOGE(TAG, "Failed to save output");
        goto cleanup;
    }

    // Send output file via UART
    AEC_LOGI(TAG, "Sending output.wav via UART...");
    uart_send_file(OUTPUT_FILE);
    AEC_LOGI(TAG, "UART transfer complete");

cleanup:
    if (near)   heap_caps_free(near);
    if (far)    heap_caps_free(far);
    if (output) heap_caps_free(output);
    esp_vfs_littlefs_unregister("storage");
    AEC_LOGI(TAG, "AEC Test Demo End");
}
