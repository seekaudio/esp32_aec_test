#ifndef SEEK_AUDIO_AEC_H
#define SEEK_AUDIO_AEC_H 1

/**
 * 这是一个通过轻量级AI模型推理和传统NLMS（PBFDAF）算法相结合实现回音消除的双引擎架构AEC。
 * 通过AI引擎赋能后，它不但具备比WebRTC AEC3更好的回音消除性能，而且运算量远远低于WebRTC AEC3。
 * 在低功耗移动设备，尤其是ESP32-S3这种嵌入式芯片运行时，它的运算速度是WebRTC AEC3的10倍以上。
 * 接口功能既可以满足语音通话的常用需求，也可以适用于语音识别和语音打断的应用场景。
 */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SEEKAUDIO_E
# if defined(WIN32)
#   define SEEKAUDIO_E __declspec(dllexport)
# elif defined(__GNUC__)
#  define SEEKAUDIO_E __attribute__ ((visibility ("default")))
# else
#  define SEEKAUDIO_E
# endif
#else
#  define SEEKAUDIO_E
#endif

    typedef void AECHandle;   // AEC 句柄
    typedef void (*log_callback_t)(const void* dump_data, const int dump_size, void* user_data);

    enum AEC_MODEL_E
    {
        AEC_AI_MODEL_BASE = 0,  // AI模型的base级别，运算速度极快，回音消除效果可以和WebRTC AEC3相媲美，远超WebRTC AECM，可以免费商用
        AEC_AI_MODEL_MAIN = 1,  // AI模型的main级别，运算速度极快，回音消除效果超过WebRTC AEC3。需要授权才能商用；未授权时，每次处理10分钟后输出静音数据
        AEC_AI_MODEL_HIGH = 2,  // 保留选项，在研发中，敬请期待
    };

    /**
     * 创建并返回AEC对象
     */
    SEEKAUDIO_E AECHandle* SeekAudioAEC_Create();

    /**
     * 释放AEC对象
     */
    SEEKAUDIO_E void SeekAudioAEC_Free(AECHandle* AEC_inst);

    /**
     * 注册日志回调函数，必须在SeekAudioAEC_Init之前调用
     *
     * 输入:
     *      - AEC_inst  : AEC对象
     *      - callback  : 回调函数指针
     *      - user_data : 用户数据指针（可为NULL），该指针会在回调时返回给用户
     * 返回值:
     *      0  - 成功
     *      -1 - 失败
     */
    SEEKAUDIO_E int SeekAudioAEC_Set_Log_Callback(AECHandle* AEC_inst, const log_callback_t callback, void* user_data);

    /**
     * 初始化AEC对象
     *
     * 输入:
     *      - AEC_inst      : AEC对象
     *      - sampleRate    : 采样率，支持16000和8000
     *      - modelLevel    : AEC AI模型级别
     *
     * 返回值:
     *      0  - 成功
     *      -1 - 失败
     */
    SEEKAUDIO_E int SeekAudioAEC_Init(AECHandle* AEC_inst, int sampleRate, AEC_MODEL_E modelLevel);

    /**
     * 设置AI引擎分配给双讲（double-talk）功能的性能比例，仅在modelLevel为AEC_AI_MODEL_MAIN时生效。
     * 该函数可在SeekAudioAEC_Init之后调用，也可以在运行时调用，但必须与SeekAudioAEC_Process处于同一线程。
     *
     * 输入:
     *      - AEC_inst      : AEC对象
     *      - powerLevel    : 百分比（0-100），默认值为0。例如powerLevel=35表示AI引擎输出35%的性能给双讲功能。
     *                        数值越大，双讲性能越好，但对回音消除效果会产生轻微的负面影响。
     *
     * 返回值: 无
     */
    SEEKAUDIO_E void SeekAudioAEC_Set_AI_Engine_Power_For_DoubleTalk(AECHandle* AEC_inst, int powerLevel);

    /**
     * 设置AI引擎分配给回音消除功能的性能比例，仅在modelLevel为AEC_AI_MODEL_MAIN时生效。
     * 该函数可在SeekAudioAEC_Init之后调用，也可以在运行时调用，但必须与SeekAudioAEC_Process处于同一线程。
     * 大多数情况下不建议调用此函数，因为默认值已具备良好的回音抑制效果。只有出现回音时才建议调用。
     *
     * 输入:
     *      - AEC_inst      : AEC对象
     *      - powerLevel    : 百分比（0-100），默认值为0。例如powerLevel=35表示AI引擎输出35%的性能给回音消除功能。
     *                        数值越大，回音抑制效果越好。
     *
     * 返回值: 无
     */
    SEEKAUDIO_E void SeekAudioAEC_Set_AI_Engine_Power_For_EchoCancel(AECHandle* AEC_inst, int powerLevel);

    /**
     * 执行回音消除处理。输入的近端数据（麦克风采集）长度必须为10ms。
     * 当modelLevel为AEC_AI_MODEL_MAIN时，可根据返回值实现精准的语音打断。
     *
     * 输入:
     *      - AEC_inst          : AEC对象
     *      - nearend           : 近端音频数据，长度必须为10ms
     *      - nrOfSamples       : 采样点数，采样率16000时为160，8000时为80
     *      - delayForMainModel : 喇叭到麦克风的整体回声路径延迟（仅当modelLevel为AEC_AI_MODEL_MAIN时需认真设置）。
     *                            嵌入式环境下需准确设置；非嵌入式环境或modelLevel为BASE时可设为0。
     *                            如何获取delayForMainModel值？可先运行BASE模型，通过该函数的返回值获得。
     *
     * 输出:
     *      - outframe          : 处理后的输出音频数据，长度为10ms
     *
     * 返回值:
     *      当modelLevel为AEC_AI_MODEL_MAIN时:
     *          0 - 非双讲状态
     *          1 - 双讲状态
     *      当modelLevel为AEC_AI_MODEL_BASE时:
     *          返回延迟值（喇叭到麦克风的整体回声路径时长）。初始返回0，从有回声出现开始会快速检测到并持续输出稳定的延迟值。
     *      < 0 - 失败
     */
    SEEKAUDIO_E int SeekAudioAEC_Process(AECHandle* AEC_inst, const short* nearend, short* outframe, int nrOfSamples, int delayForMainModel);

    /**
     * 缓存远端音频数据（参考信号）。输入数据长度必须为10ms。
     *
     * 输入:
     *      - AEC_inst      : AEC对象
     *      - farend        : 远端音频数据，长度必须为10ms
     *      - nrOfSamples   : 采样点数，采样率16000时为160，8000时为80
     *
     * 返回值:
     *      0  - 成功
     *      -1 - 失败
     */
    SEEKAUDIO_E int SeekAudioAEC_buffer_farend(AECHandle* AEC_inst, const short* farend, int nrOfSamples);

#ifdef __cplusplus
}
#endif

#endif /* SEEK_AUDIO_AEC_H */