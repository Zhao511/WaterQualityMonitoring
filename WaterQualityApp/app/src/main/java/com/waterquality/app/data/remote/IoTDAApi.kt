package com.waterquality.app.data.remote

import com.huaweicloud.sdk.core.auth.BasicCredentials
import com.huaweicloud.sdk.core.http.HttpConfig
import com.huaweicloud.sdk.iotda.v5.IoTDAClient
import com.huaweicloud.sdk.iotda.v5.model.*

class IoTDAApi {
    private var endpoint: String = ""
    private var client: IoTDAClient? = null
    private var projectId: String = ""
    private var productId: String = ""
    var isConnected: Boolean = false
        private set

    fun configure(ak: String, sk: String, projectId: String, productId: String = "", endpoint: String = "") {
        this.projectId = projectId
        this.productId = productId
        this.endpoint = endpoint.ifEmpty { "" }
        android.util.Log.d("IOTDA", "configure: ak=$ak projectId=$projectId productId=$productId endpoint=${this.endpoint}")

        val credentials = BasicCredentials()
            .withAk(ak)
            .withSk(sk)
            .withProjectId(projectId)
        try {
            credentials.processDerivedAuthParams("iotdm", "cn-south-1")
            credentials.withDerivedPredicate { true }
            android.util.Log.d("IOTDA", "credentials configured OK")
        } catch (e: Exception) {
            android.util.Log.e("IOTDA", "credentials failed: ${e.message}", e)
            throw e
        }

        val httpConfig = HttpConfig.getDefaultHttpConfig()
        httpConfig.withIgnoreSSLVerification(true)

        try {
            client = IoTDAClient.newBuilder()
                .withCredential(credentials)
                .withEndpoint(endpoint)
                .withHttpConfig(httpConfig)
                .build()
            android.util.Log.d("IOTDA", "client built OK, isConnected=true")
        } catch (e: Exception) {
            android.util.Log.e("IOTDA", "client build failed: ${e.message}", e)
            throw e
        }
    }

    fun verify(): Result<Int> {
        return try {
            val count = listDevices().size
            if (count == 0) {
                Result.failure(Exception("未找到设备，请检查 product_id 或权限"))
            } else {
                isConnected = true
                Result.success(count)
            }
        } catch (e: Exception) {
            isConnected = false
            client = null
            Result.failure(e)
        }
    }

    fun disconnect() { client = null; isConnected = false }

    fun listDevices(): List<QueryDeviceSimplify> {
        val c = client ?: throw IllegalStateException("请先配置 AK/SK")
        val req = ListDevicesRequest()
        if (productId.isNotEmpty()) req.productId = productId
        val res = c.listDevices(req)
        return res.devices ?: emptyList()
    }

    fun getDeviceShadow(deviceId: String): List<DeviceShadowData> {
        val c = client ?: return emptyList()
        return try {
            val req = ShowDeviceShadowRequest()
            req.deviceId = deviceId
            val res = c.showDeviceShadow(req)
            res.shadow ?: emptyList()
        } catch (e: Exception) { emptyList() }
    }

    fun createCommand(deviceId: String, serviceId: String, commandName: String, paras: Map<String, Any>): Boolean {
        val c = client ?: return false
        return try {
            val body = DeviceCommandRequest()
            body.serviceId = serviceId
            body.commandName = commandName
            body.paras = paras
            val req = CreateCommandRequest()
            req.deviceId = deviceId
            req.body = body
            c.createCommand(req)
            true
        } catch (e: Exception) { false }
    }
}
