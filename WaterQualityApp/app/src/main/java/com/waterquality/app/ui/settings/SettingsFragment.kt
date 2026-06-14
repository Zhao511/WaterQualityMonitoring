package com.waterquality.app.ui.settings

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.SeekBar
import android.widget.Toast
import androidx.fragment.app.Fragment
import androidx.lifecycle.ViewModelProvider
import com.waterquality.app.databinding.FragmentSettingsBinding
import com.waterquality.app.data.model.Thresholds
import com.waterquality.app.ui.home.HomeViewModel

class SettingsFragment : Fragment() {
    private var _binding: FragmentSettingsBinding? = null
    private val binding get() = _binding!!
    private lateinit var homeVM: HomeViewModel

    override fun onCreateView(inflater: LayoutInflater, c: ViewGroup?, s: Bundle?): View {
        _binding = FragmentSettingsBinding.inflate(inflater, c, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        homeVM = ViewModelProvider(requireActivity())[HomeViewModel::class.java]

        val prefs = com.waterquality.app.data.local.PreferencesManager(requireContext())
        binding.inputAk.setText(prefs.ak)
        binding.inputSk.setText(prefs.sk)
        binding.inputProjectId.setText(prefs.projectId)
        binding.switchAutoMode.isChecked = prefs.isAutoMode
        binding.labelMode.text = if (prefs.isAutoMode) "自动模式" else "手动模式"

        // 初始状态
        if (homeVM.isConnected) showConnected()

        // 编辑输入框时清除已连接状态
        homeVM.connecting.observe(viewLifecycleOwner) { connecting ->
            if (connecting) {
                binding.labelConnStatus.text = "连接中..."
                binding.labelConnStatus.setTextColor(android.graphics.Color.parseColor("#F39C12"))
            }
        }
        homeVM.connectionError.observe(viewLifecycleOwner) { error ->
            error?.let {
                binding.labelConnStatus.text = "连接失败"
                binding.labelConnStatus.setTextColor(android.graphics.Color.parseColor("#E74C3C"))
                Toast.makeText(requireContext(), it, Toast.LENGTH_LONG).show()
            }
        }

        binding.btnConnect.setOnClickListener {
            val ak = binding.inputAk.text.toString().trim()
            val sk = binding.inputSk.text.toString().trim()
            val pid = binding.inputProjectId.text.toString().trim()
            android.util.Log.d("SETTINGS", "AK len=${ak.length} SK len=${sk.length} PID len=${pid.length} akPref=${prefs.ak.length}")
            if (ak.isEmpty() || sk.isEmpty() || pid.isEmpty()) {
                Toast.makeText(requireContext(), "请填写完整 AK(长${ak.length}) SK(长${sk.length}) PID(长${pid.length})", Toast.LENGTH_LONG).show()
                return@setOnClickListener
            }
            binding.btnConnect.isEnabled = false
            binding.labelConnStatus.text = "连接中..."
            homeVM.connect(ak, sk, pid) { success, msg ->
                binding.btnConnect.isEnabled = true
                if (success) {
                    showConnected()
                } else {
                    binding.labelConnStatus.text = "连接失败"
                    binding.labelConnStatus.setTextColor(android.graphics.Color.parseColor("#E74C3C"))
                }
                Toast.makeText(requireContext(), msg, Toast.LENGTH_SHORT).show()
            }
        }

        binding.btnDisconnect.setOnClickListener {
            homeVM.disconnect()
            binding.labelConnStatus.text = "未连接"
            binding.labelConnStatus.setTextColor(android.graphics.Color.parseColor("#E74C3C"))
            binding.btnConnect.visibility = android.view.View.VISIBLE
            binding.btnDisconnect.visibility = android.view.View.GONE
            Toast.makeText(requireContext(), "已断开连接", Toast.LENGTH_SHORT).show()
        }

        homeVM.currentDevice.observe(viewLifecycleOwner) { dev ->
            if (dev == null) return@observe
            binding.seekTemp.progress = dev.thresholds.Temp_threshold.toInt()
            binding.sliderPh.values = listOf(dev.thresholds.Ph_min.toFloat(), dev.thresholds.Ph_max.toFloat())
            binding.seekTds.progress = dev.thresholds.Tds_threshold.toInt()
            updateLabels(dev.thresholds)
        }

        val listener = object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(sb: SeekBar?, v: Int, fromUser: Boolean) {
                dev?.let { updateLabels(it.thresholds) }
            }
            override fun onStartTrackingTouch(sb: SeekBar?) {}
            override fun onStopTrackingTouch(sb: SeekBar?) {
                dev?.let { d ->
                    val t = Thresholds(
                        Temp_threshold = binding.seekTemp.progress.toDouble(),
                        Ph_min = binding.sliderPh.values[0].toDouble(),
                        Ph_max = binding.sliderPh.values[1].toDouble(),
                        Tds_threshold = binding.seekTds.progress.toDouble()
                    )
                    homeVM.updateThreshold(d.id, t)
                    updateLabels(t)
                }
            }
        }

        binding.seekTemp.setOnSeekBarChangeListener(listener)
        binding.seekTds.setOnSeekBarChangeListener(listener)

        binding.sliderPh.addOnChangeListener { _, _, _ ->
            dev?.let { d ->
                val t = Thresholds(
                    Temp_threshold = d.thresholds.Temp_threshold,
                    Ph_min = binding.sliderPh.values[0].toDouble(),
                    Ph_max = binding.sliderPh.values[1].toDouble(),
                    Tds_threshold = d.thresholds.Tds_threshold
                )
                homeVM.updateThreshold(d.id, t)
                updateLabels(t)
            }
        }

        binding.switchAutoMode.setOnCheckedChangeListener { _, checked ->
            homeVM.setAutoMode(checked)
            binding.labelMode.text = if (checked) "自动模式" else "手动模式"
        }
    }

    private fun showConnected() {
        binding.labelConnStatus.text = "已连接"
        binding.labelConnStatus.setTextColor(android.graphics.Color.parseColor("#27AE60"))
        binding.btnConnect.visibility = android.view.View.GONE
        binding.btnDisconnect.visibility = android.view.View.VISIBLE
    }

    private val dev get() = homeVM.currentDevice.value

    private fun updateLabels(t: Thresholds) {
        binding.labelTemp.text = "水温阈值: ${t.Temp_threshold.toInt()}℃"
        binding.labelPh.text = "pH范围: ${t.Ph_min} - ${t.Ph_max}"
        binding.labelTds.text = "TDS阈值: ${t.Tds_threshold.toInt()} mg/L"
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }
}
