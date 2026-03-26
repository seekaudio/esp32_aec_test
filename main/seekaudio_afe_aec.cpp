#include "seekaudio_afe_aec.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdlib.h>
#include "seekaudio_defines.h"

static const char *TAG = "SEEKAUDIO_AEC";


#define DUMP_FILE      "/littlefs/aectest.dump"
static FILE* dumpFile = NULL;
static void log_callback(const void* dump_data, const int dump_size, void* user_data)
{
    if (!dumpFile)
        dumpFile = fopen(DUMP_FILE, "wb");
    if (dumpFile)
        fwrite(dump_data, 1, dump_size, dumpFile);
}

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                    */
/* ------------------------------------------------------------------ */

/**
 * Map afe_mode_t to the SeekAudio model level.
 *   AFE_MODE_LOW_COST  -> AEC_AI_MODEL_BASE  (faster, lighter)
 *   AFE_MODE_HIGH_PERF -> AEC_AI_MODEL_MAIN  (better quality)
 */
static AEC_MODEL_E map_mode(afe_mode_t mode)
{
    return (mode == AFE_MODE_HIGH_PERF) ? AEC_AI_MODEL_MAIN : AEC_AI_MODEL_BASE;
}

/**
 * Derive sample rate from afe_type_t.
 * AFE_TYPE_VC_8K -> 8000 Hz, everything else -> 16000 Hz.
 */
static int map_sample_rate(afe_type_t type)
{
    return (type == AFE_TYPE_VC_8K) ? 8000 : 16000;
}

/* ------------------------------------------------------------------ */
/*  seekaudio_aec_create                                                */
/* ------------------------------------------------------------------ */

seekaudio_aec_handle_t *seekaudio_aec_create(const char  *input_format,
                                              int          filter_length,
                                              afe_type_t   type,
                                              afe_mode_t   mode)
{
    int ret = 0;
    (void)filter_length; /* not used by SeekAudio; kept for API compatibility */

    /* --- parse input_format to get mic / ref channel indices --- */
    afe_pcm_config_t pcm_cfg = {0};
    if (!afe_parse_input_format(input_format, &pcm_cfg)) {
        ESP_LOGE(TAG, "Invalid input_format: %s", input_format ? input_format : "(null)");
        return NULL;
    }
    if (pcm_cfg.mic_num < 1 || pcm_cfg.ref_num < 1) {
        ESP_LOGE(TAG, "input_format must contain at least one M and one R channel");
        return NULL;
    }
    if (pcm_cfg.mic_num > 1 || pcm_cfg.ref_num > 1) {
        ESP_LOGW(TAG, "SeekAudio AEC supports only 1 mic + 1 ref; "
                      "only the first M and R channels will be used");
    }

    /* --- derive parameters --- */
    int sample_rate   = map_sample_rate(type);
    AEC_MODEL_E level = map_mode(mode);
    /* 10 ms frame, fixed by SeekAudio AEC */
    int frame_samples = sample_rate / 100;

    /* --- allocate wrapper handle --- */
    seekaudio_aec_handle_t *h = (seekaudio_aec_handle_t *)
        heap_caps_calloc(1, sizeof(seekaudio_aec_handle_t), MALLOC_CAP_INTERNAL);
    if (!h) {
        ESP_LOGE(TAG, "OOM: handle allocation failed");
        return NULL;
    }

    /* --- create and initialise SeekAudio instance --- */
    h->handle = SeekAudioAEC_Create();
    if (!h->handle) {
        ESP_LOGE(TAG, "SeekAudioAEC_Create() failed");
        free(h);
        return NULL;
    }

#if 0
    ret=SeekAudioAEC_Set_Log_Callback(h->handle, log_callback, NULL);
    if (ret != 0) {
        ESP_LOGE(TAG, "SeekAudioAEC_Set_Log_Callback() failed (ret=%d),this function must be called before SeekAudioAEC_Init", ret);
    }
#endif

    ret = SeekAudioAEC_Init(h->handle, sample_rate, level);
    if (ret != 0) {
        ESP_LOGE(TAG, "SeekAudioAEC_Init() failed (ret=%d)", ret);
        SeekAudioAEC_Free(h->handle);
        free(h);
        return NULL;
    }

    /* --- fill handle fields --- */
    h->sample_rate   = sample_rate;
    h->frame_samples = frame_samples;
    h->model_level   = level;
    memcpy(&h->pcm_config, &pcm_cfg, sizeof(pcm_cfg));

    ESP_LOGI(TAG, "Created: sample_rate=%d Hz, frame=%d samples (10 ms), "
                  "model=%s, mic_ch=%d, ref_ch=%d",
             sample_rate, frame_samples,
             (level == AEC_AI_MODEL_MAIN) ? "MAIN" : "BASE",
             pcm_cfg.mic_ids[0], pcm_cfg.ref_ids[0]);

    return h;
}

/* ------------------------------------------------------------------ */
/*  seekaudio_aec_process                                               */
/* ------------------------------------------------------------------ */

size_t seekaudio_aec_process(seekaudio_aec_handle_t *handle,
                              const int16_t          *indata,
                              int16_t                *outdata)
{
    if (!handle || !indata || !outdata) {
        ESP_LOGE(TAG, "seekaudio_aec_process: NULL argument");
        return 0;
    }

    int   n           = handle->frame_samples;
    int   total_ch    = handle->pcm_config.total_ch_num;
    int   mic_ch      = handle->pcm_config.mic_ids[0];  /* first mic channel  */
    int   ref_ch      = handle->pcm_config.ref_ids[0];  /* first ref channel  */

    /*
     * De-interleave: extract mic (near-end) and ref (far-end) from the
     * interleaved multi-channel input buffer.
     *
     * Input layout (interleaved, total_ch_num channels):
     *   [ch0_s0, ch1_s0, ..., chN_s0,  ch0_s1, ch1_s1, ..., chN_s1, ...]
     *
     * We use stack buffers for the extracted mono signals to avoid heap
     * allocation on every frame. 10 ms @ 16 kHz = 160 samples = 320 bytes,
     * well within safe stack limits on ESP32-S3.
     */
    int16_t near_buf[SEEKAUDIO_AEC_MAX_FRAME_SAMPLES];
    int16_t far_buf[SEEKAUDIO_AEC_MAX_FRAME_SAMPLES];

    for (int s = 0; s < n; s++) {
        near_buf[s] = indata[s * total_ch + mic_ch];
        far_buf[s]  = indata[s * total_ch + ref_ch];
    }

    /* Feed far-end reference first */
    int ret = SeekAudioAEC_buffer_farend(handle->handle, far_buf, n);
    if (ret != 0) {
        ESP_LOGE(TAG, "SeekAudioAEC_buffer_farend() failed (ret=%d)", ret);
        return 0;
    }

    /* Process near-end: output goes directly into caller's buffer */
    ret = SeekAudioAEC_Process(handle->handle, near_buf, outdata, n, SEEKAUDIO_AEC_LATENCY);
    if (ret < 0) {
        ESP_LOGE(TAG, "SeekAudioAEC_Process() failed (ret=%d)", ret);
        return 0;
    }

    return (size_t)n;
}

/* ------------------------------------------------------------------ */
/*  seekaudio_aec_get_chunksize                                         */
/* ------------------------------------------------------------------ */

int seekaudio_aec_get_chunksize(seekaudio_aec_handle_t *handle)
{
    if (!handle) return 0;
    return handle->frame_samples;  /* sample_rate / 100 = 10 ms */
}

/* ------------------------------------------------------------------ */
/*  seekaudio_aec_destroy                                               */
/* ------------------------------------------------------------------ */

void seekaudio_aec_destroy(seekaudio_aec_handle_t *handle)
{
    if (!handle) return;
    if (handle->handle) {
        SeekAudioAEC_Free(handle->handle);
        handle->handle = NULL;
    }
    /* Free the pcm_config arrays allocated by afe_parse_input_format() */
    if (handle->pcm_config.mic_ids) {
        free(handle->pcm_config.mic_ids);
        handle->pcm_config.mic_ids = NULL;
    }
    if (handle->pcm_config.ref_ids) {
        free(handle->pcm_config.ref_ids);
        handle->pcm_config.ref_ids = NULL;
    }
    free(handle);
}
