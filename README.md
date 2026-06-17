# WaterQualityMonitoring 智慧水质监测系统

基于 STM32 + ESP32 + 华为云 IoT + Web/Android 的全栈式水质物联网监测系统，实现 **终端采集 → LoRa 传输 → 云端存储 → 多端展示** 的完整链路。

## 系统架构

```
STM32F103C8T6          ESP32 网关            华为云 IoTDA          前端应用
┌────────────┐ LoRa   ┌──────────┐ MQTT/TLS ┌──────────┐ SDK    ┌───────────┐
│ pH/TDS/温度 │─115200─▶│ 透明转发  │──8883───▶│ 设备影子  │◀──────│ Web 前端   │
│ RFID/GPS   │ LORA  │ WiFi重连 │         │ 规则引擎  │ SDK   │           │
│ RGB LED    │◀──01─│ 自动重启 │◀────────│ 属性存储  │◀──────│ Android   │
│ IWDG看门狗 │       │          │         │          │       │ (Kotlin)  │
└────────────┘       └──────────┘         └──────────┘       └───────────┘
```

## 目录结构

```
├── WaterQuality_Terminal/       # STM32 终端固件 (Keil MDK)
│   ├── Core/Src/                # 主程序 + IoT 协议栈
│   ├── Core/Inc/                # 物模型头文件 + SPL 配置
│   ├── Drivers/                 # 传感器/通信驱动
│   ├── Libraries/CMSIS/         # ARM CMSIS 核心文件 (V3.5.0)
│   ├── Libraries/STM32F10x_StdPeriph_Driver/  # ST 官方 SPL V3.5.0
│   ├── FreeRTOS/                # FreeRTOS 配置
│   └── FreeRTOS-Kernel/         # FreeRTOS 内核 V10.x
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
└── docs/                        # 项目文档
    └── 问题汇总与解决方案.md       # 33 项问题诊断与修复记录
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
- **RTOS**：FreeRTOS V10.x, 抢占式调度, 6 任务, 优先级继承 (configUSE_MUTEXES=1)
- **库**：ST 官方 STM32F10x_StdPeriph_Lib_V3.5.0（非精简版）
- **传感器**：pH(PA0), TDS电导率(PA1), NTC温度(PA3)
- **ADC**：12位单次转换, SWSTART 软件触发, 采样率 55.5 周期, 互斥锁保护
- **外设**：GPS(USART2, 9600-8N1), LoRa(USART3, 115200-8N1), RFID(RC522/SPI2), RGB LED(PB6-8)
- **IWDG 看门狗**：~4s 超时 (LSI 40kHz/预分频256/重装载625), 任务心跳监控, HardFault 诊断 (LED 闪烁 + 串口字符)
- **LoRa 通信**：正点原子 ATK-LORA-01, 115200-8N1 透传, JSON + `\n` 分帧, FreeRTOS 延迟替代忙等
- **时钟**：HSE(8MHz) + PLL(×9) → 72MHz, HSI 回退 (HSI/2 + PLL×18 → 72MHz)

### 任务调度

| 任务 | 优先级 | 栈 | 周期 | 功能 |
|------|:---:|:---:|------|------|
| Watchdog | 4 | 128W | 1s | 心跳检查 + 喂狗 + 状态打印 |
| Sensor | 3 | 384W | 1s | 传感器采集 + 校准验证 + 告警判定 + LED |
| GPS | 2 | 256W | 200ms | NMEA 解析 + 坐标转换 |
| IoT | 2 | 512W | 1s | LoRa 收发 + 4 服务上报 + 命令处理 |
| RFID | 1 | 128W | 500ms | RC522 刷卡轮询 |
| LED | 1 | 128W | 事件驱动 | RGB LED 颜色控制 |

### 心跳超时

| 任务 | 超时 | 说明 |
|------|------|------|
| Sensor | 3000ms | 3 次采集周期 |
| GPS | 1000ms | 5 次轮询周期 |
| IoT | 5000ms | 含 LoRa 通信延迟 |
| RFID | 3000ms | 含 SPI 通信延迟 |
| LED | 10000ms | 事件驱动, 长超时 |

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
| 终端 | C, FreeRTOS V10.x, STM32F10x SPL V3.5.0 |
| 网关 | C++ (Arduino), PubSubClient, ArduinoJson |
| 后端 | Node.js, Express, IoTDA SDK V3 |
| Web 前端 | Bootstrap 4, jQuery, FusionCharts, QRCode.js |
| Android | Kotlin, IoTDA SDK V3, Custom GaugeView |
| 通信 | LoRa 115200-8N1, MQTT/TLS 8883, HMAC-SHA256 |

## 关键设计决策

- **不用精简版 SPL**：裁减过的外设库可能存在寄存器级 BUG（如 `ADC_ResetCalibration` 写 CONT 而非 RSTCAL），使用 ST 官方 V3.5.0
- **驱动忙等必须 yield**：`Lora_DelayMs` 使用 `vTaskDelay` 替代 `while(NOP)`，避免低优先级任务饿死
- **临界区最小化**：`Debug_Printf` 的 `taskENTER_CRITICAL` 只保护 `vsnprintf`，UART 发送在临界区外
- **互斥锁保护 ADC**：`xADCMutex` 保护通道配置+转换的原子性，防止 vSensorTask 与 vIOTTask 竞争 SQR3
- **RSSI 静态缓存**：启动时查询一次 RSSI，避免每 5 秒切换 LoRa 模式导致数据丢失
- **ISR 缓冲区溢出保护**：溢出时丢弃新字节而非清空缓冲区，保护已积累的完整帧
