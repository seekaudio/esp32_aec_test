# esp32_aec_test

这个是基于ESP32-S3-BOX-3开发板上运行回声消除aec测试程序，用来测试esp32-s3自带的aec回声消除性能，以及也可以测试seekaudio_aec的回声消除性能。通过文件seekaudio_defines.h的宏来控制测试哪个aec。seekaudio_aec的库接口文件是seekaudio_aec.h

USE_SEEKAUDIO_AEC  打开宏可以用来测试seekaudio_aec回声消除技术，关闭该宏则测试afe aec技术性能。

## 编译环境要求

- ESP-IDF 版本：v5.3
- Flash 大小：8MB（如使用其他容量请修改 sdkconfig.defaults）

## 编译步骤

### 1. 打开 ESP-IDF 终端
Windows 用户使用 ESP-IDF CMD 或 ESP-IDF PowerShell。

### 2. 进入项目目录

idf.py set-target esp32s3

idf.py build

esptool.py -p COM3 -b 115200 write_flash 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/aec_test.bin 0x410000 fs.img

idf.py -p COM3 monitor 


### 下面指令可以获取aec后的处理效果文件

esptool.py -p COM3 -b 115200 read_flash 0x410000 0x900000 storage_dump.bin
python -m littlefs extract --block-size 4096  storage_dump.bin output_dir/

### 镜像制作

python -m littlefs create --block-size 4096 --block-count 2304 fs_files/ fs.img

工程中的测试音频样本来源于微软发起的国际挑战赛（https://github.com/microsoft/AEC-Challenge ）数据集的2KevRJB38USSRuK0JPvLlA_doubletalk_mic.wav和2KevRJB38USSRuK0JPvLlA_doubletalk_lpb.wav文件,
我们可以从数据集中拿到更多的真实语音对讲样本来测试。
