package com.waterquality.app.ui.alarm

import android.graphics.Color
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import androidx.fragment.app.Fragment
import androidx.lifecycle.ViewModelProvider
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.waterquality.app.R
import com.waterquality.app.data.model.Alarm
import com.waterquality.app.data.remote.ConnectionManager
import com.waterquality.app.ui.home.HomeViewModel
import java.text.SimpleDateFormat
import java.util.*

class AlarmFragment : Fragment() {

    private lateinit var homeVM: HomeViewModel
    private lateinit var rv: RecyclerView
    private lateinit var txtTotal: TextView
    private lateinit var txtEmpty: TextView
    private lateinit var btnRefresh: Button
    private val adapter = AlarmAdapter()

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        return inflater.inflate(R.layout.fragment_alarm, container, false)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        homeVM = ViewModelProvider(requireActivity()).get(HomeViewModel::class.java)

        rv = view.findViewById(R.id.rv_alarms)
        txtTotal = view.findViewById(R.id.txt_alarm_total)
        txtEmpty = view.findViewById(R.id.txt_alarm_empty)
        btnRefresh = view.findViewById(R.id.btn_refresh_alarms)

        rv.layoutManager = LinearLayoutManager(requireContext())
        rv.adapter = adapter

        btnRefresh.setOnClickListener { refreshAlarms() }

        homeVM.currentDevice.observe(viewLifecycleOwner) { device ->
            if (device != null) refreshAlarms()
        }

        ConnectionManager.alarmList.observe(viewLifecycleOwner) { alarms ->
            adapter.submitList(alarms)
            if (alarms.isEmpty()) {
                txtEmpty.visibility = View.VISIBLE
                rv.visibility = View.GONE
                txtTotal.text = "共 0 条记录"
            } else {
                txtEmpty.visibility = View.GONE
                rv.visibility = View.VISIBLE
                txtTotal.text = "共 ${alarms.size} 条记录"
            }
        }
    }

    private fun refreshAlarms() {
        val dev = homeVM.currentDevice.value ?: return
        val deviceId = dev.rfid.ifEmpty { dev.sourceDeviceId.ifEmpty { dev.id } }
        ConnectionManager.fetchAlarms(deviceId)
    }

    inner class AlarmAdapter : RecyclerView.Adapter<AlarmAdapter.VH>() {
        private var items = listOf<Alarm>()

        fun submitList(list: List<Alarm>) {
            items = list
            notifyDataSetChanged()
        }

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH {
            val v = LayoutInflater.from(parent.context).inflate(R.layout.item_alarm, parent, false)
            return VH(v)
        }

        override fun onBindViewHolder(holder: VH, position: Int) {
            holder.bind(items[position])
        }

        override fun getItemCount() = items.size

        inner class VH(v: View) : RecyclerView.ViewHolder(v) {
            private val levelTxt: TextView = v.findViewById(R.id.txt_alarm_level)
            private val typeTxt: TextView = v.findViewById(R.id.txt_alarm_type)
            private val valueTxt: TextView = v.findViewById(R.id.txt_alarm_value)
            private val thresholdTxt: TextView = v.findViewById(R.id.txt_alarm_threshold)
            private val timeTxt: TextView = v.findViewById(R.id.txt_alarm_time)

            fun bind(a: Alarm) {
                levelTxt.text = a.alarmLevel.ifEmpty { "警告" }
                when (a.alarmLevel) {
                    "严重" -> levelTxt.setBackgroundColor(Color.parseColor("#e74c3c"))
                    "警告" -> levelTxt.setBackgroundColor(Color.parseColor("#f39c12"))
                    else -> levelTxt.setBackgroundColor(Color.parseColor("#3498db"))
                }
                typeTxt.text = a.alarmType.ifEmpty { "告警" }
                valueTxt.text = "当前值: ${a.currentValue}"
                thresholdTxt.text = "阈值: ${a.alarmThreshold}"
                try {
                    val sdf = SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss", Locale.US)
                    val date = sdf.parse(a.alarmTime)
                    val fmt = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US)
                    timeTxt.text = if (date != null) fmt.format(date) else a.alarmTime
                } catch (e: Exception) {
                    timeTxt.text = a.alarmTime
                }
            }
        }
    }
}
