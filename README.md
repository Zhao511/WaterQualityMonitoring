# WaterQualityMonitoring 智慧水质监测系统

基于 STM32 + ESP32 + 华为云 IoT + Web/Android 的全栈式水质物联网监测系统，实现 **终端采集 → LoRa 传输 → 云端存储 → 多端展示** 的完整链路。

## 系统架构

```
STM32F103C8T6          ESP32 网关            华为云 IoTDA          前端应用
┌────────────┐ LoRa   ┌──────────┐ MQTT/TLS ┌──────────┐ SDK    ┌───────────┐
│ pH/TDS/温度 │──9600─▶│ 透明转发  │──8883───▶│ 设备影子  │◀──────│ Web 前端   │
│ RFID/GPS   │ ATK-  │ HMAC认证 │         │ 命令路由  │ HTTP  │ (Bootstrap)│
│ RGB LED    │ LORA  │ WiFi重连 │         │ 规则引擎  │ SDK   │           │
│ IWDG看门狗 │◀──01──│ 自动重启 │◀────────│ 属性存储  │◀──────│ Android   │
└────────────┘       └──────────┘         └──────────┘       │ (Kotlin)  │
                                                              └───────────┘
```

## 目录结构

```
├── WaterQuality_Terminal/       # STM32 终端固件 (Keil MDK)
│   ├── Core/Src/                # 主程序 + IoT 协议栈
│   ├── Core/Inc/                # 物模型头文件
│   ├── Drivers/                 # 传感器/通信驱动
│   ├── FreeRTOS/                # FreeRTOS 配置
│   └── FreeRTOS-Kernel/         # FreeRTOS 内核
│
├── esp32_gateway/               # ESP32 网关固件 (PlatformIO)
│   ├── src/                     # LoRa收发 + MQTT客户端
│   └── include/                 # 物模型 + 配置
│
├── WaterQualityMonitoring_web/  # Web 管理平台 (Node.js + Bootstrap)
│   ├── server.js                # 后端: Express + IoTDA SDK
│   └── js/app.js                # 前端: FusionCharts 仪表盘
│
├── WaterQualityApp/             # Android 监控 App (Kotlin)
│   └── app/src/main/java/       # 直连 IoTDA SDK
│
└── secrets/                     # 凭证文件模板 (已排除上传)
```

## 物模型 (4 服务)

| 服务 | 属性 | 命令 |
|------|------|------|
| **DeviceStatus** | online, battery, signal, power, work_state, last_report | set_work_mode, remote_reboot, ota_upgrade, set_lora_param |
| **Water_status** | tds, pH, temp, rfid, gps | set_report_interval, set_threshold, calibrate_sensor, led_control, request_immediate_report |
| **Alarm** | alarm_id, alarm_type, device_id, rfid, current_value, threshold, alarm_level, alarm_time, status | set_alarm_mode, clear_alarm, mute_alarm |
| **gps** | longitude, latitude, gps_status | set_gps_mode, request_location, set_geo_fence, sync_time |

## 终端特性 (STM32)

- **MCU**：STM32F103C8T6, 72MHz, 20KB SRAM
- **RTOS**：FreeRTOS V10.x, 抢占式调度, 6 任务
- **传感器**：pH(PA0), TDS电导率(PA1), NTC温度(PA3)
- **外设**：GPS(USART2), LoRa(USART3), RFID(RC522/SPI2), RGB LED(PB6-8)
- **IWDG 看门狗**：~4s 超时, 任务心跳监控, 异常自动复位
- **LoRa 通信**：正点原子 ATK-LORA-01, 9600-8N1 透传, JSON + `\n` 分帧

### 任务调度

| 任务 | 优先级 | 周期 | 功能 |
|------|:---:|------|------|
| Watchdog | 4 | 1s | 心跳检查 + 喂狗 |
| Sensor | 3 | 1s | 传感器采集 + 告警判定 + LED |
| GPS | 2 | 200ms | NMEA 解析 + 坐标转换 |
| IoT | 2 | 1s | LoRa 收发 + 4 服务上报 + 命令处理 |
| RFID | 1 | 500ms | RC522 刷卡轮询 |
| LED | 1 | 事件驱动 | RGB LED 颜色控制 |

## 快速开始

### 1. STM32 终端

1. 安装 Keil MDK-ARM V5 + STM32F1xx DFP 包
2. 打开 `WaterQuality_Terminal/water_quality_monitor.uvprojx`
3. 编译烧录到 STM32F103C8T6

### 2. ESP32 网关

1. 安装 PlatformIO (VS Code 扩展)
2. 修改 `esp32_gateway/include/config.h` 中的 WiFi 和华为云凭证
3. 编译上传到 ESP32

```bash
cd esp32_gateway
pio run -t upload
```

### 3. Web 管理平台

1. 安装 Node.js
2. 修改 `server.js` 中的 AK/SK/ProjectID
3. 启动服务

```bash
cd WaterQualityMonitoring_web
npm install
node server.js
# 浏览器打开 http://localhost:3000
```

### 4. Android App

1. 安装 Android Studio
2. 打开 `WaterQualityApp/` 项目
3. 在设置页面配置 AK/SK/ProjectID
4. 编译运行

## 凭证配置

所有敏感凭证已替换为占位符，需修改以下文件：

| 文件 | 占位符 | 说明 |
|------|--------|------|
| `esp32_gateway/include/config.h` | `YOUR_WIFI_SSID` 等 | WiFi + 华为云设备凭证 |
| `server.js` | `YOUR_PRODUCT_ID` 等 | 华为云产品 ID + 端点 |
| `IoTDAApi.kt` | `YOUR_INSTANCE_ID` 等 | 华为云端点 + 产品 ID |

`secrets/` 目录存放凭证模板文件，已通过 `.gitignore` 排除上传。

## 数据流

```
传感器 ──ADC──▶ STM32 ──LoRa JSON──▶ ESP32 ──MQTT──▶ 华为云 IoTDA
                                                       │
                                    ┌──────────────────┼──────────────────┐
                                    ▼                  ▼                  ▼
                              Web 仪表盘         Android App        server.js 缓存
                           (3s 轮询 REST)     (5s 轮询 SDK)    (5s 同步 devices.json)
```

## 技术栈

| 层级 | 技术 |
|------|------|
| 终端 | C, FreeRTOS, STM32 StdPeriph |
| 网关 | C++ (Arduino), PubSubClient, ArduinoJson |
| 后端 | Node.js, Express, IoTDA SDK V3 |
| Web 前端 | Bootstrap 4, jQuery, FusionCharts, QRCode.js |
| Android | Kotlin, IoTDA SDK V3, Custom GaugeView |
| 通信 | LoRa 9600-8N1, MQTT/TLS 8883, HMAC-SHA256 |

## License

MIT
