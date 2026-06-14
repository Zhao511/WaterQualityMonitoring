/**
 * ============================================================
 * 华为云 IoT MQTT 客户端 — WiFi + MQTT 认证 + 属性上报 + 命令接收
 * ============================================================
 */

#include "mqtt_huawei.h"
#include <ArduinoJson.h>
#include <mbedtls/md.h>
#include <WiFiClientSecure.h>

/* ================================================================
 * 华为云 IoT MQTT TLS 根证书 (DigiCert Global Root CA)
 * 华为云使用 DigiCert 签发的服务器证书
 * ================================================================ */
static const char HUAWEI_ROOT_CA[] PROGMEM =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdWrJWZHHSjANBgkqhkiG9w0BAQUFADBh\n"
    "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"
    "d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBD\n"
    "QTAeFw0wNjExMTAwMDAwMDBaFw0zMTExMTAwMDAwMDBaMGExCzAJBgNVBAYTAlVT\n"
    "MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n"
    "b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IENBMIIBIjANBgkqhkiG\n"
    "9w0BAQEFAAOCAQ8AMIIBCgKCAQEA4jvhEXLeqKTTo1eqUKKPC3eQyaKl7hLOllsB\n"
    "CSDMAZOnTjC3U/dDxGkAV53ijSLdhwZAAIEJzs4bg7/fzTtxRuLWZscFs3YnFo97\n"
    "nh6Vfe63SKMI2tavegw5BmV/Sl0fvBf4q77uKNd0f3p4mVmFaG5cIzJLv07A6Fpt\n"
    "43C/dxC//AH2hdmoRBBYMql1GNXRor5H4idq9Joz+EkIYIvUX7Q6hL+hqkpMfT7P\n"
    "T19sdl6gSzeRntwi5m3OFBqOasv+zbMUZBfHWymeMr/y7vrTC0LUq7dBMtoM1O/4\n"
    "gdW7jVg/tRvoSSiicNoxBN33shbyTApOB6jtSj1etX+jkMOvJwIDAQABo2MwYTAO\n"
    "BgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUA95QNVbR\n"
    "TLtm8KPiGxvDl7I90VUwHwYDVR0jBBgwFoAUA95QNVbRTLtm8KPiGxvDl7I90VUw\n"
    "DQYJKoZIhvcNAQEFBQADggEBAMucN6pIExIK+t1EnE9SsPTfrgT1eXkIoyQY/Esr\n"
    "hMAtudXH/vTBH1jLuG2cenTnmCmrEbXjcKChzUyImZOMkXDiqw8cvpOp/2PV5Adg\n"
    "06O/nVsJ8dWO41P0jmP6P6fbtGbfYmbW0W5BjfIttep3Sp+dWOIrWcBAI+0tKIJF\n"
    "PnlUkiaY4IBIqDfv8NZ5YBberOgOzW6sRBc4L0na4UU+Krk2U886UAb3LujEV0ls\n"
    "YSEY1QSteDwsOoBrp+uvFRTp2InBuThs4pFsiv9kuXclVzDAGySj4dzp30d8tbQk\n"
    "CAUw7C29C79Fv1C5qfPrmAESrciIxpg0X40KPMbp1ZWVbd4=\n"
    "-----END CERTIFICATE-----\n";
static WiFiClientSecure g_wifi_client;
static PubSubClient     g_mqtt(g_wifi_client);
static mqtt_cmd_callback_t g_cmd_cb = nullptr;

static String g_topic_property;
static String g_topic_cmd;
static String g_topic_cmd_rsp_prefix;   /* "$oc/devices/{id}/sys/commands/response/" */
static String g_last_request_id;        /* 最近收到的命令 request_id */
static String g_device_id;
static String g_client_id;

static uint32_t g_wifi_retry   = 0;
static uint32_t g_mqtt_retry   = 0;
static uint32_t g_mqtt_last_connect = 0;
static char     g_mqtt_password[128] = {0};

/* ================================================================
 * 生成华为云 IoT 时间戳 (UTC, YYYYMMDDHH 格式)
 * ================================================================ */
static String generate_timestamp()
{
    time_t now;
    time(&now);
    struct tm *utc = gmtime(&now);
    char buf[16];
    snprintf(buf, sizeof(buf), "%04d%02d%02d%02d",
             utc->tm_year + 1900, utc->tm_mon + 1, utc->tm_mday, utc->tm_hour);
    return String(buf);
}

/* ================================================================
 * HMAC-SHA256 认证密码 (华为云 IoT 设备密钥认证)
 * 密码 = HMAC-SHA256(DeviceSecret, 时间戳YYYYMMDDHH)
 * ================================================================ */
static String generate_mqtt_password()
{
    String timestamp = generate_timestamp();

    /* HMAC-SHA256: key=时间戳, msg=设备密钥 (华为云规范) */
    uint8_t hmac[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, (const uint8_t *)timestamp.c_str(),
                           timestamp.length());
    mbedtls_md_hmac_update(&ctx, (const uint8_t *)HUAWEI_DEVICE_SECRET,
                           strlen(HUAWEI_DEVICE_SECRET));
    mbedtls_md_hmac_finish(&ctx, hmac);
    mbedtls_md_free(&ctx);

    /* 转十六进制字符串 */
    memset(g_mqtt_password, 0, sizeof(g_mqtt_password));
    for (int i = 0; i < 32; i++) {
        snprintf(g_mqtt_password + i * 2, 3, "%02x", hmac[i]);   /* 小写 hex，华为云要求 */
    }

    DEBUG_SERIAL.print("[MQTT] Timestamp: ");
    DEBUG_SERIAL.print(timestamp);
    DEBUG_SERIAL.print("  Password: ");
    DEBUG_SERIAL.println(g_mqtt_password);

    return String(g_mqtt_password);
}

/* ================================================================
 * WiFi 连接
 * ================================================================ */
static bool wifi_connect()
{
    if (WiFi.status() == WL_CONNECTED) return true;

    DEBUG_SERIAL.print("[WiFi] Connecting to ");
    DEBUG_SERIAL.print(WIFI_SSID);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        if (millis() - start > WIFI_TIMEOUT_MS)
        {
            DEBUG_SERIAL.println(" TIMEOUT!");
            return false;
        }
        delay(500);
        DEBUG_SERIAL.print(".");
    }

    DEBUG_SERIAL.println(" OK");
    DEBUG_SERIAL.print("[WiFi] IP: ");
    DEBUG_SERIAL.println(WiFi.localIP());

    /* WiFi 连上后同步 UTC 时间 (NTP, MQTT 认证需要 UTC 时间戳)
     * 多轮重试, 每轮切换不同 NTP 服务器池 */
    static const char* ntp_pools[] = {
        "ntp.aliyun.com", "ntp.ntsc.ac.cn",
        "pool.ntp.org", "time.google.com",
        "cn.pool.ntp.org"
    };
    const int ntp_pool_count = sizeof(ntp_pools) / sizeof(ntp_pools[0]);

    time_t now;
    bool synced = false;
    for (int round = 0; round < 3 && !synced; round++)
    {
        const char *srv0 = ntp_pools[(round * 2) % ntp_pool_count];
        const char *srv1 = ntp_pools[(round * 2 + 1) % ntp_pool_count];
        DEBUG_SERIAL.printf("[NTP] Round %d: %s, %s\n", round + 1, srv0, srv1);
        configTime(0, 0, srv0, srv1);

        for (int i = 0; i < 10; i++) {
            time(&now);
            if (now > 1000000000) {  /* 2020 年以后 */
                synced = true;
                break;
            }
            delay(1000);
        }
    }

    if (synced) {
        DEBUG_SERIAL.print("[NTP] OK: ");
        DEBUG_SERIAL.println(ctime(&now));
    } else {
        DEBUG_SERIAL.println("[NTP] WARN: All servers failed, MQTT auth may fail!");
    }
    return true;  /* 连上 WiFi 就算成功, MQTT 层有重试机制 */
}

/* ================================================================
 * MQTT 连接华为云
 * ================================================================ */
static bool mqtt_connect()
{
    if (g_mqtt.connected()) return true;

    /* 构建 Client ID: 设备ID_0_0_YYYYMMDDHH */
    g_client_id = String(HUAWEI_DEVICE_ID) + "_0_0_" + generate_timestamp();

    /* 构建 Topic */
    char buf[128];
    snprintf(buf, sizeof(buf), TOPIC_PROPERTY_REPORT, HUAWEI_DEVICE_ID);
    g_topic_property  = buf;
    snprintf(buf, sizeof(buf), TOPIC_COMMAND_RECEIVE, HUAWEI_DEVICE_ID);
    g_topic_cmd       = buf;
    /* 命令响应 topic 前缀 (request_id 在收到命令时提取) */
    snprintf(buf, sizeof(buf), "$oc/devices/%s/sys/commands/response/", HUAWEI_DEVICE_ID);
    g_topic_cmd_rsp_prefix = buf;
    g_last_request_id = "";

    /* 生成认证密码 */
    String pass = generate_mqtt_password();

    /* MQTT 连接参数 */
    g_mqtt.setServer(HUAWEI_MQTT_HOST, HUAWEI_MQTT_PORT);
    g_mqtt.setKeepAlive(MQTT_KEEPALIVE_SEC);

    DEBUG_SERIAL.print("[MQTT] Connecting to ");
    DEBUG_SERIAL.print(HUAWEI_MQTT_HOST);
    DEBUG_SERIAL.print(":");
    DEBUG_SERIAL.print(HUAWEI_MQTT_PORT);
    DEBUG_SERIAL.print("  ClientID=");
    DEBUG_SERIAL.println(g_client_id);

    if (g_mqtt.connect(g_client_id.c_str(),
                       HUAWEI_DEVICE_ID,   /* username = device ID */
                       pass.c_str()))       /* password = HMAC-SHA256 */
    {
        DEBUG_SERIAL.println("[MQTT] Connected!");

        /* 订阅命令 Topic */
        g_mqtt.subscribe(g_topic_cmd.c_str());
        DEBUG_SERIAL.print("[MQTT] Subscribed: ");
        DEBUG_SERIAL.println(g_topic_cmd);

        return true;
    }

    DEBUG_SERIAL.print("[MQTT] Failed, rc=");
    DEBUG_SERIAL.println(g_mqtt.state());
    return false;
}

/* ================================================================
 * MQTT 消息回调 (云端下发命令)
 * ================================================================ */
static void mqtt_callback(char *topic, uint8_t *payload, unsigned int length)
{
    String json;
    json.concat((char *)payload, length);

    DEBUG_SERIAL.print("[MQTT CMD] Topic: ");
    DEBUG_SERIAL.print(topic);
    DEBUG_SERIAL.print("  Payload: ");
    DEBUG_SERIAL.println(json);

    /* 从 topic 中提取 request_id: $oc/devices/{id}/sys/commands/request_id={req_id} */
    const char *req_prefix = "request_id=";
    char *req_pos = strstr(topic, req_prefix);
    if (req_pos) {
        g_last_request_id = String(req_pos + strlen(req_prefix));
    }

    if (g_cmd_cb) g_cmd_cb(json);
}

/* ================================================================
 * 公共接口
 * ================================================================ */
bool mqtt_init()
{
    /* 设置 MQTT 回调 */
    g_mqtt.setCallback(mqtt_callback);

    /* 配置 TLS：跳过证书验证（st1.iotda-device 端点证书链与内置 DigiCert 不匹配） */
    g_wifi_client.setInsecure();
    g_wifi_client.setTimeout(15);  /* TLS 握手超时 15 秒 */

    /* WiFi 连接 */
    if (!wifi_connect()) {
        DEBUG_SERIAL.println("[WiFi] Failed to connect!");
        return false;
    }

    /* MQTT 连接 */
    if (!mqtt_connect()) {
        DEBUG_SERIAL.println("[MQTT] Failed to connect!");
        return false;
    }

    return true;
}

void mqtt_loop()
{
    /* WiFi 维护 */
    if (WiFi.status() != WL_CONNECTED) {
        g_wifi_retry++;
        if (g_wifi_retry > WIFI_RETRY_MAX) {
            DEBUG_SERIAL.println("[WiFi] Max retries, restarting...");
            ESP.restart();
        }
        wifi_connect();
        if (WiFi.status() == WL_CONNECTED) {
            g_wifi_retry = 0;  /* 成功后复位计数 */
        }
    }

    /* MQTT 维护 + 处理消息 (指数退避重连: 1s→2s→4s→8s→...→60s) */
    if (!g_mqtt.connected()) {
        uint32_t now = millis();
        uint32_t backoff = (1UL << g_mqtt_retry) * 1000UL;
        if (backoff > 60000UL) backoff = 60000UL;  /* 上限 60s */
        if (now - g_mqtt_last_connect > backoff) {
            g_mqtt_last_connect = now;
            DEBUG_SERIAL.print("[MQTT] Reconnect attempt #");
            DEBUG_SERIAL.print(g_mqtt_retry + 1);
            DEBUG_SERIAL.print("  backoff=");
            DEBUG_SERIAL.print(backoff / 1000);
            DEBUG_SERIAL.println("s");
            if (mqtt_connect()) {
                g_mqtt_retry = 0;  /* 成功复位 */
            } else {
                g_mqtt_retry++;    /* 失败递增 */
            }
        }
    }
    g_mqtt.loop();
}

bool mqtt_connected()
{
    return g_mqtt.connected();
}

/* ================================================================
 * 属性上报
 * ================================================================ */
void mqtt_report_property(const String &service_json)
{
    if (!g_mqtt.connected()) {
        DEBUG_SERIAL.println("[MQTT] Not connected, can't report");
        return;
    }

    /* 华为云 IoTDA 标准格式: {"services":[{"service_id":"X","properties":{...}}]}
     * STM32 和 toCloudJson() 生成的是内层 {"service_id":"X","properties":{...}}
     * 在此统一包装 services 数组外层 */
    String wrapped = "{\"services\":[" + service_json + "]}";

    DEBUG_SERIAL.print("[MQTT PUB] ");
    DEBUG_SERIAL.print(g_topic_property);
    DEBUG_SERIAL.print("  ");
    DEBUG_SERIAL.println(wrapped);

    g_mqtt.publish(g_topic_property.c_str(), wrapped.c_str());
}

/* ================================================================
 * 命令响应
 * ================================================================ */
void mqtt_response_cmd(const String &rsp_json)
{
    if (!g_mqtt.connected()) return;

    /* 构建带 request_id 的响应 topic */
    String topic = g_topic_cmd_rsp_prefix + "request_id=" + g_last_request_id;

    DEBUG_SERIAL.print("[MQTT RSP] ");
    DEBUG_SERIAL.print(topic);
    DEBUG_SERIAL.print("  ");
    DEBUG_SERIAL.println(rsp_json);

    g_mqtt.publish(topic.c_str(), rsp_json.c_str());
}

/* ================================================================
 * 注册命令回调
 * ================================================================ */
void mqtt_on_command(mqtt_cmd_callback_t cb)
{
    g_cmd_cb = cb;
}
