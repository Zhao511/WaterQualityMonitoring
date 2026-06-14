# ESP32 LoRa 网关 — 华为云 IoT 水情监测

## 架构

```
STM32F103 ←──LoRa(E220 9600)──→ ESP32 ←──WiFi/MQTT──→ 华为云IoT
 (采集+报警)                    (协议转换)              (物模型)
```

## 目录结构

```
esp32_gateway/
├── platformio.ini              # PlatformIO 项目配置
├── README.md
├── include/
│   ├── config.h                # WiFi/MQTT/LoRa/设备ID 配置 (需要修改!)
│   ├── thing_model.h           # 4 服务物模型结构体 (STM32 同步)
│   ├── lora_uart.h             # LoRa UART 驱动
│   └── mqtt_huawei.h           # 华为云 MQTT 客户端
└── src/
    ├── main.cpp                # 主程序 (收发转发)
    ├── lora_uart.cpp           # LoRa JSON 帧收发
    └── mqtt_huawei.cpp         # WiFi + MQTT 认证 + 属性上报
```

## 使用步骤

### 1. 安装 PlatformIO (VS Code)

搜索安装 "PlatformIO IDE" 扩展，或命令行:
```bash
pip install platformio
```

### 2. 修改配置

编辑 `include/config.h`，修改以下参数:

```c
/* WiFi */
#define WIFI_SSID         "你的WiFi名"
#define WIFI_PASSWORD     "你的WiFi密码"

/* 华为云 IoT (从控制台 → 设备详情 获取) */
#define HUAWEI_DEVICE_ID      "设备ID"
#define HUAWEI_PRODUCT_ID     "产品ID"
#define HUAWEI_DEVICE_SECRET  "设备密钥"

/* MQTT 接入点 (根据注册区域) */
#define HUAWEI_MQTT_HOST  "iot-mqtts.cn-north-4.myhuaweicloud.com"
```

### 3. 烧录 & 运行

```bash
cd esp32_gateway
# 编译
platformio run
# 烧录 + 串口监视
platformio run --target upload --target monitor
```

或用 VS Code 左下角按钮: ✓ 编译 → 烧录 → 监视

### 4. 硬件接线

```
ESP32           LoRa E220
GPIO16 (RX2) ─── TX
GPIO17 (TX2) ─── RX
3.3V        ─── VCC
GND         ─── GND
GPIO18      ─── AUX  (可选)
GPIO19      ─── M0   (可选)
GPIO21      ─── M1   (可选)
```

### 5. 验证

串口监视 (115200) 应看到:
```
========================================
 ESP32 LoRa Gateway for Huawei Cloud IoT
 STM32 <--LoRa--> ESP32 <--MQTT--> Cloud
========================================
[LoRa] UART initialized, 9600-8N1, transparent mode
[WiFi] Connecting to xxx... OK
[WiFi] IP: 192.168.1.100
[NTP] Time syncing...
[NTP] OK: Wed Jun 11 14:30:00 2026
[MQTT] Connected!
[MQTT] Subscribed: /v1.0/devices/xxx/down/commands
[SYS] Gateway ready.
[LoRa RX] {"service":"Water_status","data":{...}}
[FWD] LoRa→Cloud  service=Water_status  len=xxx
[MQTT PUB] /v1.0/devices/xxx/up/properties {"service_id":"Water_status",...}
```

## 数据流

| 方向 | 来源 | 目标 | 说明 |
|------|------|------|------|
| 上行 | STM32 LoRa → | ESP32 → MQTT 属性上报 | 传感器数据/告警/GPS/设备状态 |
| 下行 | 华为云 MQTT → | ESP32 → STM32 LoRa | set_threshold/led_control 等 12 条命令 |
| 响应 | STM32 LoRa → | ESP32 → MQTT 命令响应 | 命令执行结果回云端 |

## 依赖库

| 库 | 用途 |
|----|------|
| `knolleary/PubSubClient` | MQTT 客户端 |
| `bblanchon/ArduinoJson` | JSON 序列化/反序列化 |
| `WiFi.h` (ESP32 内置) | WiFi 连接 |
| `mbedtls/md.h` (ESP32 内置) | HMAC-SHA256 设备认证 |
