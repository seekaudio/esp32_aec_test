#ifndef __AEC_DEFINES__
#define __AEC_DEFINES__


// ==================== AEC Backend Selection ====================
// Set USE_SEEKAUDIO_AEC to 1 to test the SeekAudio AEC wrapper,
// or 0 to use the built-in Espressif AFE AEC.
#ifndef USE_SEEKAUDIO_AEC
#define USE_SEEKAUDIO_AEC  1
#endif

// This is a parameter setting for the SeekAudioAEC_Process function, 
// indicating the overall echo path delay from the speaker to the microphone.
// It only work for AEC_AI_MODEL_MAIN
#define SEEKAUDIO_AEC_LATENCY  9

#if USE_SEEKAUDIO_AEC
// 0 -> AEC_AI_MODEL_BASE (faster, lighter)
// 1 -> AEC_AI_MODEL_MAIN (better quality)
#define SEEKAUDIO_AEC_MODEL  1

// ===============================================================
#else
#define USE_AFE_TYPE_SR 1       //set USE_AFE_TYPE_SR to 0 to use AFE_TYPE_VC
#endif

#if USE_SEEKAUDIO_AEC
#include "seekaudio_afe_aec.h"
// Alias types and functions to a common name so run_aec() stays clean
typedef seekaudio_aec_handle_t      aec_handle_unified_t;
#define AEC_CREATE(fmt, flen, type, mode)   seekaudio_aec_create((fmt), (flen), (type), (mode))
#define AEC_PROCESS(h, in, out)             seekaudio_aec_process((h), (in), (out))
#define AEC_CHUNKSIZE(h)                    seekaudio_aec_get_chunksize(h)
#define AEC_DESTROY(h)                      seekaudio_aec_destroy(h)
#if SEEKAUDIO_AEC_MODEL
#define AEC_EFFECTIVE_MODE   AFE_MODE_HIGH_PERF   // -> AEC_AI_MODEL_MAIN
#define AEC_BACKEND_STR      "SeekAudio / MAIN"
#else
#define AEC_EFFECTIVE_MODE   AFE_MODE_LOW_COST    // -> AEC_AI_MODEL_BASE
#define AEC_BACKEND_STR      "SeekAudio / BASE"
#endif
#else
#include "esp_afe_aec.h"
typedef afe_aec_handle_t            aec_handle_unified_t;
#define AEC_CREATE(fmt, flen, type, mode)   afe_aec_create((fmt), (flen), (type), (mode))
#define AEC_PROCESS(h, in, out)             afe_aec_process((h), (in), (out))
#define AEC_CHUNKSIZE(h)                    afe_aec_get_chunksize(h)
#define AEC_DESTROY(h)                      afe_aec_destroy(h)
#define AEC_EFFECTIVE_MODE   AFE_MODE_HIGH_PERF
#if USE_AFE_TYPE_SR
#define AEC_BACKEND_STR      "Espressif AFE AEC for Speech recognition scenarios"
#else
#define AEC_BACKEND_STR      "Espressif AFE AEC VC for Voice communication scenarios"
#endif
#endif

// ==================== Log Switch ====================
#ifndef AEC_DEMO_ENABLE_INFO_LOG
#define AEC_DEMO_ENABLE_INFO_LOG 1
#endif

#if AEC_DEMO_ENABLE_INFO_LOG
#define AEC_LOGI(tag, ...) ESP_LOGI(tag, __VA_ARGS__)
#define AEC_LOGW(tag, ...) ESP_LOGW(tag, __VA_ARGS__)
#else
#define AEC_LOGI(tag, ...) do {} while (0)
#define AEC_LOGW(tag, ...) do {} while (0)
#endif
#define AEC_LOGE(tag, ...) ESP_LOGE(tag, __VA_ARGS__)
// ====================================================

// Audio config
#define NEAR_END_FILE    "/littlefs/near.wav"
#define FAR_END_FILE     "/littlefs/far.wav"
#define OUTPUT_FILE      "/littlefs/output.wav"

// AEC config
#define AEC_INPUT_FORMAT "MR"
#define AEC_FILTER_LEN   4

// UART config
#define UART_PORT        UART_NUM_0
#define UART_BAUD        115200

// Transfer protocol
#define DATA_HEADER      0xAA55
#define DATA_FOOTER      0x55AA

// UART send chunk size (bytes)
#define UART_CHUNK_BYTES 2048


#endif