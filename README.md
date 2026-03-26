# esp32_aec_test

## 目录结构

```
aec_test/
├── CMakeLists.txt          # 项目根配置
├── partitions.csv          # 自定义分区表
├── sdkconfig.defaults      # 默认 SDK 配置
└── main/
    ├── aectest.c           # 主程序
    ├── CMakeLists.txt      # 组件编译配置
    └── idf_component.yml  # 第三方组件依赖声明
```

## 编译环境要求

- ESP-IDF 版本：v5.3
- Flash 大小：8MB（如使用其他容量请修改 sdkconfig.defaults）

## 编译步骤

### 1. 打开 ESP-IDF 终端
Windows 用户使用 ESP-IDF CMD 或 ESP-IDF PowerShell。

### 2. 进入项目目录
```bash
cd D:/esptest/aec_test
```

### 3. 拉取第三方组件（首次编译必须执行）
```bash
idf.py update-dependencies
```
执行完成后会生成 managed_components/ 文件夹，内含：
- espressif__gmf_ai_audio/
- joltwallet__littlefs/

### 4. 设置目标芯片（以 ESP32-S3 为例）
```bash
idf.py set-target esp32s3
```
根据你的实际芯片替换，支持：esp32 / esp32s2 / esp32s3 / esp32c3 / esp32c6

### 5. 编译
```bash
idf.py build
```

### 6. 烧录
```bash
idf.py -p COM端口号 flash
```
例如：idf.py -p COM3 flash

### 7. 查看日志
```bash
idf.py -p COM3 monitor
```

### 8. 编译+烧录+监视 一步完成
```bash
idf.py -p COM3 flash monitor
```

## 常见问题

### 组件下载失败（网络问题）
```bash
# 设置代理后重试
set HTTPS_PROXY=http://127.0.0.1:你的代理端口
idf.py update-dependencies
```

### 重新全量编译
```bash
idf.py fullclean
idf.py build
```

### Flash 大小不匹配
修改 sdkconfig.defaults 中的 CONFIG_ESPTOOLPY_FLASHSIZE，
可选值：2MB / 4MB / 8MB / 16MB
```

## LittleFS 文件准备

运行前需要提前将 PCM 文件烧录到 storage 分区：
```bash
# 安装 littlefs 打包工具
pip install littlefs-python

# 打包文件系统镜像（near.pcm 和 far.pcm 放在 fs_files/ 目录下）
python -m littlefs create --block-size 4096 --block-count 2560 fs_files/ fs.img

# 烧录到 storage 分区（偏移量 0x310000）
esptool.py -p COM3 write_flash 0x310000 fs.img

esptool.py -p COM3 -b 115200 write_flash 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/aec_test.bin 0x310000 fs.img

idf.py -p COM3 monitor

esptool.py -p COM3 -b 115200 read_flash 0x310000 0xA00000 storage_dump.bin

python -m littlefs extract --block-size 4096  storage_dump.bin output_dir/


# 读 flash
esptool.py -p COM3 -b 115200 read_flash 0x410000 0x900000 storage_dump.bin

# 制作镜像
python -m littlefs create --block-size 4096 --block-count 2304 fs_files/ fs.img

esptool.py -p COM3 -b 115200 write_flash 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/aec_test.bin 0x410000 fs.img
idf.py -p COM3 monitor
idf.py set-target esp32s3