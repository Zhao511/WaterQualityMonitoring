package com.waterquality.app.ui.home

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import androidx.lifecycle.ViewModelProvider
import com.waterquality.app.databinding.FragmentHomeBinding

class HomeFragment : Fragment() {
    private var _binding: FragmentHomeBinding? = null
    private val binding get() = _binding!!
    private lateinit var viewModel: HomeViewModel

    override fun onCreateView(inflater: LayoutInflater, c: ViewGroup?, s: Bundle?): View {
        _binding = FragmentHomeBinding.inflate(inflater, c, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        viewModel = ViewModelProvider(requireActivity())[HomeViewModel::class.java]

        viewModel.currentDevice.observe(viewLifecycleOwner) { device ->
            device?.let { updateUI(it) }
        }

        viewModel.alarmActive.observe(viewLifecycleOwner) { active ->
            binding.alarmStatus.text = if (active) "告警中" else "告警关闭"
            binding.alarmStatus.setTextColor(
                if (active) android.graphics.Color.parseColor("#E74C3C")
                else android.graphics.Color.parseColor("#27AE60")
            )
            // 强制刷新
            binding.alarmStatus.requestLayout()
            android.util.Log.d("HOME", "alarmActive=$active status=${binding.alarmStatus.text}")
        }

        viewModel.alarmRecords.observe(viewLifecycleOwner) { records ->
            binding.alarmList.text = if (records.isEmpty()) "暂无告警记录" else records.joinToString("\n")
        }

        binding.btnManualAlarm.setOnClickListener { viewModel.triggerAlarm() }
        binding.btnStopAlarm.setOnClickListener { viewModel.stopAlarm() }
    }

    private fun updateUI(device: com.waterquality.app.data.model.Device) {
        binding.gaugeTds.value = device.data.tds
        binding.gaugeTds.caption = "TDS"
        binding.gaugeTds.unit = "mg/L"
        binding.gaugeTds.minValue = 0.0; binding.gaugeTds.maxValue = 2000.0
        binding.gaugeTds.ranges = listOf(Triple(0.0, 800.0, android.graphics.Color.parseColor("#27AE60")), Triple(800.0, 1500.0, android.graphics.Color.parseColor("#F39C12")), Triple(1500.0, 2000.0, android.graphics.Color.parseColor("#E74C3C")))

        binding.gaugePh.value = device.data.ph
        binding.gaugePh.caption = "pH"
        binding.gaugePh.unit = ""
        binding.gaugePh.minValue = 0.0; binding.gaugePh.maxValue = 14.0
        binding.gaugePh.ranges = listOf(Triple(0.0, 6.0, android.graphics.Color.parseColor("#F39C12")), Triple(6.0, 9.0, android.graphics.Color.parseColor("#27AE60")), Triple(9.0, 14.0, android.graphics.Color.parseColor("#E74C3C")))

        binding.gaugeTemp.value = device.data.temp
        binding.gaugeTemp.caption = "水温"
        binding.gaugeTemp.unit = "℃"
        binding.gaugeTemp.minValue = -20.0; binding.gaugeTemp.maxValue = 100.0
        binding.gaugeTemp.ranges = listOf(Triple(0.0, 50.0, android.graphics.Color.parseColor("#62B58F")), Triple(50.0, 100.0, android.graphics.Color.parseColor("#FC6211")))

        binding.deviceName.text = device.name
        binding.deviceStatus.text = if (device.status == "ONLINE") "在线" else "离线"
        binding.deviceStatus.setTextColor(
            if (device.status == "ONLINE") android.graphics.Color.parseColor("#27AE60")
            else android.graphics.Color.parseColor("#999999")
        )
        binding.rfidValue.text = device.rfid.ifEmpty { "未设置" }
        binding.gpsValue.text = "经度:${device.longitude} 纬度:${device.latitude}"
        binding.batteryValue.text = "${device.battery}%"
        binding.signalValue.text = "${device.signal} dBm"
        binding.powerValue.text = device.power.ifEmpty { "--" }
        binding.workStateValue.text = device.workState.ifEmpty { "--" }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }
}
