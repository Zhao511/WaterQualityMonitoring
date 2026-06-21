package com.waterquality.app.ui.home

import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.TextView
import androidx.fragment.app.Fragment
import androidx.lifecycle.ViewModelProvider
import com.waterquality.app.R
import com.waterquality.app.data.model.Device
import com.waterquality.app.ui.widgets.GaugeView

class HomeFragment : Fragment() {

    private lateinit var viewModel: HomeViewModel
    private lateinit var statusLight: View
    private lateinit var deviceName: TextView
    private lateinit var deviceStatus: TextView
    private lateinit var gaugeTds: GaugeView
    private lateinit var gaugePh: GaugeView
    private lateinit var gaugeTemp: GaugeView
    private lateinit var rfidValue: TextView
    private lateinit var gpsValue: TextView
    private lateinit var batteryValue: TextView
    private lateinit var signalValue: TextView
    private lateinit var powerValue: TextView
    private lateinit var workStateValue: TextView
    private lateinit var gpsStatusValue: TextView
    private lateinit var lastReportValue: TextView
    private lateinit var alarmStatus: TextView
    private lateinit var alarmList: TextView
    private lateinit var btnManualAlarm: Button
    private lateinit var btnStopAlarm: Button

    override fun onCreateView(inflater: LayoutInflater, c: ViewGroup?, s: Bundle?): View {
        return inflater.inflate(R.layout.fragment_home, c, false)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        viewModel = ViewModelProvider(requireActivity())[HomeViewModel::class.java]

        // Bind views
        statusLight = view.findViewById(R.id.status_light)
        deviceName = view.findViewById(R.id.device_name)
        deviceStatus = view.findViewById(R.id.device_status)
        gaugeTds = view.findViewById(R.id.gauge_tds)
        gaugePh = view.findViewById(R.id.gauge_ph)
        gaugeTemp = view.findViewById(R.id.gauge_temp)
        rfidValue = view.findViewById(R.id.rfid_value)
        gpsValue = view.findViewById(R.id.gps_value)
        batteryValue = view.findViewById(R.id.battery_value)
        signalValue = view.findViewById(R.id.signal_value)
        powerValue = view.findViewById(R.id.power_value)
        workStateValue = view.findViewById(R.id.work_state_value)
        gpsStatusValue = view.findViewById(R.id.gps_status_value)
        lastReportValue = view.findViewById(R.id.last_report_value)
        alarmStatus = view.findViewById(R.id.alarm_status)
        alarmList = view.findViewById(R.id.alarm_list)
        btnManualAlarm = view.findViewById(R.id.btn_manual_alarm)
        btnStopAlarm = view.findViewById(R.id.btn_stop_alarm)

        // Init gauges
        initGauge(gaugeTds, 0.0, 2000.0)
        initGauge(gaugePh, 0.0, 14.0)
        initGauge(gaugeTemp, -20.0, 100.0)

        viewModel.currentDevice.observe(viewLifecycleOwner) { device ->
            device?.let { updateUI(it) }
        }

        viewModel.alarmActive.observe(viewLifecycleOwner) { active ->
            alarmStatus.text = if (active) "告警中" else "告警关闭"
            alarmStatus.setTextColor(
                if (active) Color.parseColor("#E74C3C")
                else Color.parseColor("#27AE60")
            )
        }

        viewModel.alarmRecords.observe(viewLifecycleOwner) { records ->
            alarmList.text = if (records.isEmpty()) "暂无告警记录" else records.joinToString("\n")
        }

        btnManualAlarm.setOnClickListener { viewModel.triggerAlarm() }
        btnStopAlarm.setOnClickListener { viewModel.stopAlarm() }
    }

    private fun initGauge(gauge: GaugeView, min: Double, max: Double) {
        gauge.minValue = min
        gauge.maxValue = max
    }

    private fun updateUI(device: Device) {
        // Gauges
        gaugeTds.value = device.data.tds
        gaugeTds.caption = "TDS"
        gaugeTds.unit = "mg/L"
        gaugeTds.ranges = listOf(
            Triple(0.0, 800.0, Color.parseColor("#27AE60")),
            Triple(800.0, 1500.0, Color.parseColor("#F39C12")),
            Triple(1500.0, 2000.0, Color.parseColor("#E74C3C"))
        )

        gaugePh.value = device.data.ph
        gaugePh.caption = "pH"
        gaugePh.unit = ""
        gaugePh.ranges = listOf(
            Triple(0.0, 6.0, Color.parseColor("#F39C12")),
            Triple(6.0, 9.0, Color.parseColor("#27AE60")),
            Triple(9.0, 14.0, Color.parseColor("#E74C3C"))
        )

        gaugeTemp.value = device.data.temp
        gaugeTemp.caption = "水温"
        gaugeTemp.unit = "℃"
        gaugeTemp.ranges = listOf(
            Triple(0.0, 50.0, Color.parseColor("#62B58F")),
            Triple(50.0, 100.0, Color.parseColor("#FC6211"))
        )

        // Status LED
        val shape = statusLight.background as? GradientDrawable
        when (device.status) {
            "ONLINE" -> {
                shape?.setColor(Color.parseColor("#27AE60")) // 绿色
                deviceStatus.text = "在线"
                deviceStatus.setTextColor(Color.parseColor("#27AE60"))
            }
            "OFFLINE" -> {
                shape?.setColor(Color.parseColor("#E67E22")) // 橙色
                deviceStatus.text = "离线"
                deviceStatus.setTextColor(Color.parseColor("#999999"))
            }
            else -> {
                shape?.setColor(Color.parseColor("#95A5A6")) // 灰色 - UNKNOWN
                deviceStatus.text = "未知"
                deviceStatus.setTextColor(Color.parseColor("#999999"))
            }
        }

        deviceName.text = device.name
        rfidValue.text = device.rfid.ifEmpty { "未设置" }
        gpsValue.text = "经度:${device.longitude} 纬度:${device.latitude}"
        batteryValue.text = "${device.battery}%"
        signalValue.text = "${device.signal} dBm"
        powerValue.text = device.power.ifEmpty { "--" }
        workStateValue.text = device.workState.ifEmpty { "--" }

        // New fields
        gpsStatusValue.text = device.gpsStatus.ifEmpty { "未知" }
        gpsStatusValue.setTextColor(
            if (device.gpsStatus.contains("有效") || device.gpsStatus.contains("定位"))
                Color.parseColor("#27AE60") else Color.parseColor("#95A5A6")
        )

        lastReportValue.text = if (device.lastReport.isNotEmpty()) {
            try {
                val sdf = java.text.SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss", java.util.Locale.US)
                val date = sdf.parse(device.lastReport)
                val fmt = java.text.SimpleDateFormat("MM-dd HH:mm", java.util.Locale.US)
                if (date != null) fmt.format(date) else device.lastReport
            } catch (e: Exception) {
                device.lastReport
            }
        } else {
            "--"
        }
    }
}
