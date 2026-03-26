#ifndef _SEEKAUDIO_AFE_AEC_H_
#define _SEEKAUDIO_AFE_AEC_H_

/**
 * @file seekaudio_afe_aec.h
 * @brief Wrapper of SeekAudio AEC algorithm, providing an interface consistent
 *        with esp_afe_aec.h so that aectest can switch between the two
 *        implementations by changing only the include and the function-name prefix.
 *
 * Mapping table:
 *  afe_aec_create()        -> seekaudio_aec_create()
 *  afe_aec_process()       -> seekaudio_aec_process()
 *  afe_aec_get_chunksize() -> seekaudio_aec_get_chunksize()
 *  afe_aec_destroy()       -> seekaudio_aec_destroy()
 */

#include "seekaudio_aec.h"
#include "esp_afe_config.h"   // reuse afe_type_t / afe_mode_t from the project
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Constants                                                           */
/* ------------------------------------------------------------------ */

/** Maximum samples per 10 ms frame across all supported sample rates.
 *  16000 Hz -> 160 samples, 8000 Hz -> 80 samples.
 *  Stack buffers in seekaudio_aec_process() are sized by this constant
 *  to avoid VLA (Variable Length Array) usage. */
#define SEEKAUDIO_AEC_MAX_FRAME_SAMPLES  160

/* ------------------------------------------------------------------ */
/*  Handle                                                              */
/* ------------------------------------------------------------------ */

/**
 * @brief Internal handle that mirrors afe_aec_handle_t.
 *
 * Keeps all state needed so that the caller never has to touch
 * the underlying SeekAudio API directly.
 */
typedef struct {
    AECHandle        *handle;       /**< Raw SeekAudio AEC instance          */
    int               sample_rate;  /**< 8000 or 16000 Hz                    */
    int               frame_samples;/**< Samples per 10 ms frame             */
    AEC_MODEL_E       model_level;  /**< Model level derived from afe_mode_t */
    afe_pcm_config_t  pcm_config;   /**< Parsed channel layout (M/R indices) */
} seekaudio_aec_handle_t;

/* ------------------------------------------------------------------ */
/*  API                                                                 */
/* ------------------------------------------------------------------ */

/**
 * @brief Create and initialise a SeekAudio AEC instance.
 *
 * The signature is intentionally identical to afe_aec_create() so that
 * aectest.cpp can substitute it with a one-line change.
 *
 * @param input_format  Channel layout string, same convention as afe_aec_create().
 *                      e.g. "MR" = mic first, reference second (interleaved).
 *                      Currently only 1 mic + 1 reference channel is supported.
 * @param filter_length Unused (kept for API compatibility; SeekAudio manages its
 *                      own internal filter). Pass the same value you use for
 *                      afe_aec_create().
 * @param type          AFE type. AFE_TYPE_VC_8K selects 8 kHz; all other values
 *                      select 16 kHz.
 * @param mode          AFE mode.
 *                        AFE_MODE_LOW_COST  -> AEC_AI_MODEL_BASE
 *                        AFE_MODE_HIGH_PERF -> AEC_AI_MODEL_MAIN
 *
 * @return Pointer to a new seekaudio_aec_handle_t, or NULL on failure.
 */
seekaudio_aec_handle_t *seekaudio_aec_create(const char  *input_format,
                                              int          filter_length,
                                              afe_type_t   type,
                                              afe_mode_t   mode);

/**
 * @brief Run echo cancellation on one frame.
 *
 * The function accepts interleaved MR (or whatever layout was specified in
 * input_format) audio, splits it into near-end and far-end internally, feeds
 * the far-end to SeekAudioAEC_buffer_farend(), then calls
 * SeekAudioAEC_Process() to produce the cleaned near-end output.
 *
 * One call processes exactly seekaudio_aec_get_chunksize() samples per channel.
 *
 * @param handle   Instance returned by seekaudio_aec_create().
 * @param indata   Interleaved input: frame_samples * channel_num int16 samples.
 *                 Memory does NOT need special alignment (unlike afe_aec_process).
 * @param outdata  Output buffer for the cleaned mono near-end signal.
 *                 Must hold at least frame_samples int16 samples.
 *                 16-byte alignment is recommended but not strictly required here.
 *
 * @return Number of output samples written (== seekaudio_aec_get_chunksize()),
 *         or 0 on error. Matches the size_t return of afe_aec_process().
 */
size_t seekaudio_aec_process(seekaudio_aec_handle_t *handle,
                              const int16_t          *indata,
                              int16_t                *outdata);

/**
 * @brief Return the number of samples per processing frame (one channel).
 *
 * SeekAudio AEC operates on fixed 10 ms frames, so this returns:
 *   - 160 for 16000 Hz
 *   -  80 for  8000 Hz
 *
 * This matches the contract of afe_aec_get_chunksize().
 *
 * @param handle  Instance returned by seekaudio_aec_create().
 * @return        Samples per frame (single channel).
 */
int seekaudio_aec_get_chunksize(seekaudio_aec_handle_t *handle);

/**
 * @brief Destroy the AEC instance and free all associated resources.
 *
 * Mirrors afe_aec_destroy().
 *
 * @param handle  Instance returned by seekaudio_aec_create(). May be NULL.
 */
void seekaudio_aec_destroy(seekaudio_aec_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* _SEEKAUDIO_AFE_AEC_H_ */
