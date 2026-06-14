/**
 * ============================================================
 * 智慧水质监测系统 — FreeRTOS + 华为云 IoT 物模型
 * ============================================================
 * 架构: STM32F103 ←LoRa→ ESP32网关 ←MQTT→ 华为云IoT
 * MCU:  STM32F103C8T6  |  RTOS: FreeRTOS
 * SRAM: 20KB           |  Flash: 64KB
 * ============================================================
 * 任务架构 (6 个任务):
 *
 *   vWatchdogTask(Prio 4, Stack 128W) — IWDG 看门狗监控 + 心跳检查
 *   vSensorTask  (Prio 3, Stack 256W) — 传感器 1s 采集 + 告警判定
 *   vGPSTask     (Prio 2, Stack 256W) — GPS 200ms 轮询 + NMEA→Decimal
 *   vIOTTask     (Prio 2, Stack 512W) — LoRa 收发 + 4 服务上报 + 命令处理
 *   vRFIDTask    (Prio 1, Stack 128W) — RFID 500ms 扫描
 *   vLEDTask     (Prio 1, Stack 128W) — 水质 & 命令 LED 指示
 * ============================================================
 */

#include "stm32f10x.h"

/* FreeRTOS */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* 驱动层 */
#include "led_rgb.h"
#include "sensor_ph.h"
#include "sensor_turbidity.h"
#include "sensor_temp.h"
#include "sensor_tds.h"
#include "adc_common.h"
#include "lora.h"
#include "gps.h"
#include "rc522.h"
#include "usart_debug.h"
#include "watchdog.h"

/* IoT 物模型 */
#include "iot_model.h"
#include "iot_json.h"
#include "iot_service.h"

#include <stdio.h>
#include <string.h>

/* ================================================================
 * 全局物模型实例 (iot_service.h 中 extern 声明)
 * ================================================================ */
WaterStatus  g_water_status;
DeviceStatus g_device_status;
GPS          g_gps_data;
uint32_t     g_report_interval_sec = IOT_DEFAULT_REPORT_INTERVAL;

/* ================================================================
 * 旧版数据结构 (保留用于队列兼容, 后续逐步迁移)
 * ================================================================ */
typedef struct {
    float ph;
    float turbidity;
    float temperature;
    float tds;
} SensorData;

typedef struct {
    uint8_t tag_id[5];
    uint8_t tag_valid;
} RFIDData;

/* ================================================================
 * 温度合法性 (保留用于 TDS 补偿)
 * ================================================================ */
#define TEMP_MIN_VALID       0.0f
#define TEMP_MAX_VALID       60.0f
#define TEMP_DEFAULT         25.0f

/* ================================================================
 * FreeRTOS 队列 & 互斥量
 * ================================================================ */
static QueueHandle_t xSensorQueue  = NULL;
static QueueHandle_t xGPSQueue     = NULL;
static QueueHandle_t xRFIDQueue    = NULL;
static QueueHandle_t xLEDQueue     = NULL;
static QueueHandle_t xAlarmQueue   = NULL;   /* 告警事件队列 */
static SemaphoreHandle_t xDebugMutex = NULL;
static SemaphoreHandle_t xWaterMutex = NULL;  /* 保护 g_water_status 并发访问 */
static TaskHandle_t xWatchdogTaskHandle = NULL;

/* 任务运行计数器 (周期性日志输出, 避免刷屏) */
static uint32_t g_sensor_cycle = 0;
static uint32_t g_gps_cycle    = 0;
static uint32_t g_iot_cycle    = 0;
static uint32_t g_rfid_cycle   = 0;
static uint32_t g_led_cycle    = 0;

/* 即时上报信号 */
static TaskHandle_t xSensorTaskHandle = NULL;
TaskHandle_t xIOTTaskHandle = NULL;
#define IOT_IMMEDIATE_REPORT_BIT   0x01

/* ================================================================
 * 函数声明
 * ================================================================ */
static LED_Color CheckWaterQuality_LED(const WaterStatus *ws);
static uint8_t  ReadRFIDTag(uint8_t *tag_id);

/* 任务函数 */
static void vSensorTask(void *pvParameters);
static void vGPSTask(void *pvParameters);
static void vRFIDTask(void *pvParameters);
static void vIOTTask(void *pvParameters);
static void vLEDTask(void *pvParameters);
static void vWatchdogTask(void *pvParameters);

/* 传输层外部函数 (iot_transport.c) */
extern void IOT_Report_DeviceStatus(const DeviceStatus *s);
extern void IOT_Report_WaterStatus(const WaterStatus *s);
extern void IOT_Report_Alarm(const Alarm *a);
extern void IOT_Report_GPS(const GPS *g);
extern void IOT_Process_Incoming(void);
extern void IOT_Collect_All_Sensors(WaterStatus *ws);

/* ================================================================
 * LED 颜色判定 (基于物模型阈值)
 * ================================================================ */
static LED_Color CheckWaterQuality_LED(const WaterStatus *ws)
{
    uint8_t fail_count = 0;

    if (ws->ph   < ws->ph_min   || ws->ph   > ws->ph_max)   fail_count++;
    if (ws->tds  > ws->tds_max)                              fail_count++;
    if (ws->temp < ws->temp_min || ws->temp > ws->temp_max)  fail_count++;

    if (fail_count == 0)      return LED_COLOR_GREEN;
    else if (fail_count == 1) return LED_COLOR_YELLOW;
    else                      return LED_COLOR_RED;
}

/* ================================================================
 * RFID 读卡辅助
 * ================================================================ */
static uint8_t ReadRFIDTag(uint8_t *tag_id)
{
    uint8_t status;
    uint8_t tag_type[2];

    status = RC522_Request(PICC_REQA, tag_type);
    if (status != MI_OK) return 0;

    status = RC522_SelectTag(tag_id);
    return (status == MI_OK) ? 1 : 0;
}

/* ================================================================
 * 任务 1 — 传感器采集 (Prio 3, 1s 周期)
 * ================================================================ */
static void vSensorTask(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(1000);

    SensorData sensor_data;

    for (;;)
    {
        /* 读取所有传感器 + 校准 */
        sensor_data.ph          = IOT_Apply_Calibration("ph", PH_Sensor_Read());
        sensor_data.turbidity   = 0;  /* 无浊度传感器, 固定为 0 */
        sensor_data.temperature = IOT_Apply_Calibration("temp", Temp_Sensor_Read());

        /* 温度合理性检查 */
        float temp = sensor_data.temperature;
        if (temp < TEMP_MIN_VALID || temp > TEMP_MAX_VALID)
            temp = TEMP_DEFAULT;
        sensor_data.tds = IOT_Apply_Calibration("tds", TDS_Sensor_Read(temp));

        /* 更新全局物模型 WaterStatus (加锁保护) */
        if (xSemaphoreTake(xWaterMutex, pdMS_TO_TICKS(50)) == pdPASS)
        {
            g_water_status.ph        = sensor_data.ph;
            g_water_status.tds       = sensor_data.tds;
            g_water_status.temp      = sensor_data.temperature;
            g_water_status.turbidity = sensor_data.turbidity;
            xSemaphoreGive(xWaterMutex);
        }

        /* 写入传感器队列 (供 IOT 任务消费) */
        xQueueOverwrite(xSensorQueue, &sensor_data);

        /* 水质评估 → LED */
        LED_Color color = CheckWaterQuality_LED(&g_water_status);
        xQueueOverwrite(xLEDQueue, &color);

        /* 告警检测 (基于物模型阈值) */
        Alarm alarm;
        if (IOT_Alarm_Check(&g_water_status, g_water_status.rfid, &alarm) != 0)
        {
            xQueueSend(xAlarmQueue, &alarm, 0);  /* 非阻塞送告警 */
        }

        /* 调试输出 */
        if (xSemaphoreTake(xDebugMutex, pdMS_TO_TICKS(100)) == pdPASS)
        {
            Debug_Printf("PH:%.2f TDS:%.1f Temp:%.1f Turb:%.1f\r\n",
                         sensor_data.ph, sensor_data.tds,
                         sensor_data.temperature, sensor_data.turbidity);
            xSemaphoreGive(xDebugMutex);
        }

        /* 心跳 + 周期日志 (每 10 轮 ≈ 10s 输出摘要) */
        g_sensor_cycle++;
        WDG_Heartbeat(HEARTBEAT_SENSOR);

        if ((g_sensor_cycle % 10) == 0) {
            if (xSemaphoreTake(xDebugMutex, pdMS_TO_TICKS(100)) == pdPASS) {
                Debug_Printf("[Sensor] cycle=%lu | PH=%.2f TDS=%.1f Temp=%.1f Turb=%.1f\r\n",
                             g_sensor_cycle, sensor_data.ph, sensor_data.tds,
                             sensor_data.temperature, sensor_data.turbidity);
                xSemaphoreGive(xDebugMutex);
            }
        }

        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

/* ================================================================
 * 任务 2 — GPS 解析 (Prio 2, 200ms 周期)
 * ================================================================ */
static void vGPSTask(void *pvParameters)
{
    GPS_Data raw_gps;

    for (;;)
    {
        GPS_ProcessData();
        GPS_GetData(&raw_gps);

        if (raw_gps.fix > 0)
        {
            xQueueOverwrite(xGPSQueue, &raw_gps);

            /* 转换为物模型 GPS (decimal degrees) */
            IOT_GPS_GetDecimal(&g_gps_data);

            if (xSemaphoreTake(xDebugMutex, pdMS_TO_TICKS(100)) == pdPASS)
            {
                Debug_Printf("GPS: %.6f,%.6f Fix=%d Sat=%d\r\n",
                             g_gps_data.latitude, g_gps_data.longitude,
                             raw_gps.fix, raw_gps.satellites);
                xSemaphoreGive(xDebugMutex);
            }
        }

        /* 心跳 + 周期日志 (每 25 轮 ≈ 5s 输出摘要) */
        g_gps_cycle++;
        WDG_Heartbeat(HEARTBEAT_GPS);

        if ((g_gps_cycle % 25) == 0) {
            if (xSemaphoreTake(xDebugMutex, pdMS_TO_TICKS(100)) == pdPASS) {
                Debug_Printf("[GPS] cycle=%lu | Fix=%d Sat=%d\r\n",
                             g_gps_cycle, raw_gps.fix, raw_gps.satellites);
                xSemaphoreGive(xDebugMutex);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/* ================================================================
 * 任务 3 — RFID 扫描 (Prio 1, 500ms 周期)
 * ================================================================ */
static void vRFIDTask(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(500);

    RFIDData rfid = {0};

    for (;;)
    {
        rfid.tag_valid = ReadRFIDTag(rfid.tag_id);

        if (rfid.tag_valid)
        {
            xQueueOverwrite(xRFIDQueue, &rfid);

            /* 更新物模型 RFID */
            snprintf(g_water_status.rfid, IOT_RFID_LEN,
                     "%02X%02X%02X%02X%02X",
                     rfid.tag_id[0], rfid.tag_id[1],
                     rfid.tag_id[2], rfid.tag_id[3],
                     rfid.tag_id[4]);

            if (xSemaphoreTake(xDebugMutex, pdMS_TO_TICKS(100)) == pdPASS)
            {
                Debug_Printf("RFID: %s\r\n", g_water_status.rfid);
                xSemaphoreGive(xDebugMutex);
            }
        }

        /* 心跳 + 周期日志 (每 10 轮 ≈ 5s 输出摘要) */
        g_rfid_cycle++;
        WDG_Heartbeat(HEARTBEAT_RFID);

        if ((g_rfid_cycle % 10) == 0) {
            if (xSemaphoreTake(xDebugMutex, pdMS_TO_TICKS(100)) == pdPASS) {
                Debug_Printf("[RFID] cycle=%lu | scanning...\r\n", g_rfid_cycle);
                xSemaphoreGive(xDebugMutex);
            }
        }

        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

/* ================================================================
 * 任务 4 — IoT 通信 (Prio 2, 可调上报周期)
 *   - 消费 LoRa RX → 命令解析分发
 *   - 定时上报 4 服务属性 (WaterStatus/DeviceStatus/GPS/Alarm)
 * ================================================================ */
static void vIOTTask(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    uint32_t   tick_count    = 0;
    uint32_t   last_device_status = 0;
    uint32_t   last_gps_report    = 0;

    SensorData sensor_data;
    RFIDData   rfid_data;
    GPS_Data   raw_gps;
    Alarm      alarm;

    /* tick_count 为秒计数器, 直接与秒值比较 */
    tick_count = 1;  /* 从 1 开始, 避免首发空数据 */

    for (;;)
    {
        /* --- LoRa 下行命令处理 (每次循环都检查) --- */
        IOT_Process_Incoming();

        /* --- 检查即时上报信号 --- */
        uint32_t notify;
        if (xTaskNotifyWait(0, 0, &notify, 0) == pdTRUE)
        {
            if (notify & IOT_IMMEDIATE_REPORT_BIT)
            {
                /* request_immediate_report: 全量采集 + 立即上报 */
                IOT_Collect_All_Sensors(&g_water_status);
                IOT_Report_WaterStatus(&g_water_status);
            }
        }

        /* --- 非阻塞消费各数据队列 (更新物模型) --- */
        xQueueReceive(xSensorQueue, &sensor_data, 0);
        xQueueReceive(xGPSQueue,    &raw_gps,     0);
        xQueueReceive(xRFIDQueue,   &rfid_data,   0);

        /* --- 定时: Water_status 上报 (可调间隔, 默认 5s) --- */
        if ((tick_count % g_report_interval_sec) == 0)
        {
            /* 更新 GPS 文本 (点位地理位置) */
            if (xSemaphoreTake(xWaterMutex, pdMS_TO_TICKS(50)) == pdPASS)
            {
                snprintf(g_water_status.gps, IOT_GPS_STR_LEN,
                         "%.6f,%.6f", g_gps_data.longitude, g_gps_data.latitude);
                IOT_Report_WaterStatus(&g_water_status);
                xSemaphoreGive(xWaterMutex);
            }
        }

        /* --- 定时: DeviceStatus 上报 (60s) --- */
        if (tick_count - last_device_status >= IOT_DEVICE_STATUS_INTERVAL)
        {
            IOT_DeviceStatus_Update(&g_device_status);
            IOT_Report_DeviceStatus(&g_device_status);
            last_device_status = tick_count;
        }

        /* --- 定时: GPS 上报 (30s) --- */
        if (tick_count - last_gps_report >= IOT_GPS_REPORT_INTERVAL)
        {
            IOT_Report_GPS(&g_gps_data);
            last_gps_report = tick_count;
        }

        /* --- Alarm 即时上报 (检查队列) --- */
        while (xQueueReceive(xAlarmQueue, &alarm, 0) == pdPASS)
        {
            IOT_Report_Alarm(&alarm);
        }

        /* 心跳 + 周期日志 (每 5 轮 ≈ 5s 输出摘要) */
        g_iot_cycle++;
        WDG_Heartbeat(HEARTBEAT_IOT);

        if ((g_iot_cycle % 5) == 0) {
            if (xSemaphoreTake(xDebugMutex, pdMS_TO_TICKS(100)) == pdPASS) {
                Debug_Printf("[IoT] cycle=%lu | interval=%lus | RSSI=%d\r\n",
                             g_iot_cycle, g_report_interval_sec,
                             IOT_Get_LoRa_RSSI());
                xSemaphoreGive(xDebugMutex);
            }
        }

        tick_count++;
        vTaskDelay(pdMS_TO_TICKS(1000));  /* 1s 主循环 */
    }
}

/* ================================================================
 * 任务 5 — LED 状态指示 (Prio 1, 阻塞等待)
 * ================================================================ */
static void vLEDTask(void *pvParameters)
{
    LED_Color color;
    LED_Color last_color = LED_COLOR_OFF;
    static const char *color_names[] = {
        "OFF", "RED", "GREEN", "BLUE",
        "YELLOW", "CYAN", "MAGENTA", "WHITE"
    };

    for (;;)
    {
        if (xQueueReceive(xLEDQueue, &color, portMAX_DELAY) == pdPASS)
        {
            LED_RGB_SetColor(color);
            g_led_cycle++;
            WDG_Heartbeat(HEARTBEAT_LED);

            /* LED 颜色变化时输出日志 */
            if (color != last_color) {
                if (xSemaphoreTake(xDebugMutex, pdMS_TO_TICKS(100)) == pdPASS) {
                    Debug_Printf("[LED] -> %s (cycle=%lu)\r\n",
                                 color_names[color], g_led_cycle);
                    xSemaphoreGive(xDebugMutex);
                }
                last_color = color;
            }
        }
    }
}

/* ================================================================
 * 任务 6 — 看门狗监控 (Prio 4, 1s 周期)
 *   - 检查全部应用任务心跳 → 正常则喂狗, 异常则停止喂狗
 *   - 每 10s 输出心跳状态摘要
 * ================================================================ */
static void vWatchdogTask(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(1000);
    uint32_t cycle = 0;

    for (;;)
    {
        WDG_CheckAll();   /* 检查心跳 → 正常则喂狗, 异常则等待 IWDG 复位 */
        cycle++;

        /* 每 10 轮 (~10s) 输出看门狗状态摘要 */
        if ((cycle % 10) == 0) {
            if (xSemaphoreTake(xDebugMutex, pdMS_TO_TICKS(100)) == pdPASS) {
                WDG_PrintStatus();
                xSemaphoreGive(xDebugMutex);
            }
        }

        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

/* ================================================================
 * main() — 系统初始化 & 调度器启动
 * ================================================================ */

/* FreeRTOS 栈溢出钩子 (configCHECK_FOR_STACK_OVERFLOW=1 时必须实现) */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    Debug_Printf("\r\n[FATAL] Stack overflow in task: %s\r\n", pcTaskName);
    /* 死循环 — 看门狗将在 ~4s 后复位系统 */
    taskDISABLE_INTERRUPTS();
    for (;;) { __NOP(); }
}

int main(void)
{
    /* ---- 硬件初始化 ---- */
    LED_RGB_Init();
    ADC_Common_Init();
    PH_Sensor_Init();
    Turbidity_Sensor_Init();
    Temp_Sensor_Init();
    TDS_Sensor_Init();
    GPS_Init();
    LoRa_Init();
    RC522_Init();
    Debug_USART_Init();

    /* 启动指示：蓝灯 */
    LED_RGB_SetColor(LED_COLOR_BLUE);
    Debug_Printf("\r\n========================================\r\n");
    Debug_Printf(" Water Quality Monitor - FreeRTOS v2.0\r\n");
    Debug_Printf(" STM32F103C8T6 | 72MHz | IoT 4-Service\r\n");
    Debug_Printf("========================================\r\n");

    for (volatile uint32_t i = 0; i < 2000000; i++) { __NOP(); }

    /* ---- IoT 物模型初始化 ---- */
    IOT_DeviceStatus_Init(&g_device_status);
    IOT_Threshold_Init(&g_water_status);
    IOT_Alarm_Init();
    IOT_Calibration_Init();
    memset(&g_gps_data, 0, sizeof(g_gps_data));

    Debug_Printf("IoT Model: 4 services initialized\r\n");

    /* ---- 打印任务配置表 ---- */
    WDG_PrintTaskTable();

    /* ---- 创建 FreeRTOS 队列 ---- */
    xSensorQueue = xQueueCreate(1, sizeof(SensorData));
    xGPSQueue    = xQueueCreate(1, sizeof(GPS_Data));
    xRFIDQueue   = xQueueCreate(1, sizeof(RFIDData));
    xLEDQueue    = xQueueCreate(1, sizeof(LED_Color));
    xAlarmQueue  = xQueueCreate(4, sizeof(Alarm));     /* 告警缓冲 */
    xDebugMutex  = xSemaphoreCreateMutex();
    xWaterMutex  = xSemaphoreCreateMutex();            /* 保护 g_water_status */

    /* 验证内核对象 */
    if (xSensorQueue == NULL || xGPSQueue    == NULL ||
        xRFIDQueue   == NULL || xLEDQueue    == NULL ||
        xAlarmQueue  == NULL || xDebugMutex  == NULL || xWaterMutex == NULL)
    {
        LED_RGB_SetColor(LED_COLOR_RED);
        Debug_Printf("FATAL: Failed to create RTOS objects!\r\n");
        for (;;) { __NOP(); }
    }

    /* ---- 初始化 IWDG 看门狗 ---- */
    WDG_Init();
    WDG_DebugFreeze();      /* 调试断点时冻结 IWDG, 避免误复位 */
    Debug_Printf("[WDG] IWDG started, timeout ~4s (LSI ~40kHz)\r\n\r\n");

    /* ---- 创建任务 (Prio: Watchdog > Sensor > IOT/GPS > RFID/LED) ---- */
    BaseType_t ret;

    ret = xTaskCreate(vWatchdogTask, "Watchdog", 128, NULL, 4, &xWatchdogTaskHandle);
    if (ret != pdPASS) { LED_RGB_SetColor(LED_COLOR_RED); for (;;); }

    ret = xTaskCreate(vSensorTask, "Sensor", 256, NULL, 3, &xSensorTaskHandle);
    if (ret != pdPASS) { LED_RGB_SetColor(LED_COLOR_RED); for (;;); }

    ret = xTaskCreate(vGPSTask,   "GPS",    256, NULL, 2, NULL);
    if (ret != pdPASS) { LED_RGB_SetColor(LED_COLOR_RED); for (;;); }

    ret = xTaskCreate(vIOTTask,   "IoT",    512, NULL, 2, &xIOTTaskHandle);
    if (ret != pdPASS) { LED_RGB_SetColor(LED_COLOR_RED); for (;;); }

    ret = xTaskCreate(vRFIDTask,  "RFID",   128, NULL, 1, NULL);
    if (ret != pdPASS) { LED_RGB_SetColor(LED_COLOR_RED); for (;;); }

    ret = xTaskCreate(vLEDTask,   "LED",    128, NULL, 1, NULL);
    if (ret != pdPASS) { LED_RGB_SetColor(LED_COLOR_RED); for (;;); }

    Debug_Printf("All tasks created. Starting scheduler...\r\n\r\n");

    /* ---- 启动调度器 ---- */
    vTaskStartScheduler();

    LED_RGB_SetColor(LED_COLOR_RED);
    Debug_Printf("FATAL: Scheduler failed to start!\r\n");
    for (;;) { __NOP(); }
}
