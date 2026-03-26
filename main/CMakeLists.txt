################################################################################
# main 组件：应用层 Demo
# 静态库 libseekaudio_aec.a 和头文件 seekaudio_aec.h 已预先编译好，
# 存放在项目根目录的 libs/seekaudio_aec/ 下。
################################################################################

idf_component_register(
    SRCS
        "aectest.cpp"
        "seekaudio_afe_aec.cpp"
        "wave_file.cpp"
    INCLUDE_DIRS
        "."
        # 添加预编译库的头文件目录
        "${CMAKE_CURRENT_SOURCE_DIR}/../libs/seekaudio_aec/include"
    # 仍然需要这些公开依赖，因为它们提供的头文件被 seekaudio_afe_aec.cpp 直接或间接包含
    REQUIRES
        espressif__gmf_ai_audio
        espressif__gmf_core
        espressif__esp-sr
    PRIV_REQUIRES
        esp_timer
        esp-dsp
        esp_driver_uart
        joltwallet__littlefs
        freertos
        log
)

# 链接预编译的静态库
# 注意：必须使用绝对路径或相对于 CMake 当前源目录的路径
target_link_libraries(__idf_main PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../libs/seekaudio_aec/lib/libseekaudio_aec.a"
    "${CMAKE_BINARY_DIR}/esp-idf/espressif__esp-dsp/libespressif__esp-dsp.a"
)

# 编译选项（与原来保持一致）
target_compile_options(__idf_main PRIVATE
    -Wno-error=format
    -Wno-error=unused-variable
    -Wno-error=implicit-function-declaration
    -Wno-error=unused-function
    -Wno-error=misleading-indentation
    -Wno-error=return-type
)
