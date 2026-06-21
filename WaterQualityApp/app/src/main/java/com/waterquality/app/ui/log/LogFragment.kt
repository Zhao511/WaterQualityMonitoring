package com.waterquality.app.ui.log

import android.app.AlertDialog
import android.content.Intent
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
import com.waterquality.app.data.remote.ConnectionManager
import com.waterquality.app.ui.home.HomeViewModel
import com.waterquality.app.util.LogEntry
import com.waterquality.app.util.LogManager
import java.text.SimpleDateFormat
import java.util.*

class LogFragment : Fragment() {

    private lateinit var homeVM: HomeViewModel
    private lateinit var rv: RecyclerView
    private lateinit var txtCount: TextView
    private lateinit var txtEmpty: TextView
    private val adapter = LogAdapter()
    private var currentFilter: List<String>? = null // null = 全部

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        return inflater.inflate(R.layout.fragment_log, container, false)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        homeVM = ViewModelProvider(requireActivity()).get(HomeViewModel::class.java)

        rv = view.findViewById(R.id.rv_logs)
        txtCount = view.findViewById(R.id.txt_log_count)
        txtEmpty = view.findViewById(R.id.txt_log_empty)

        rv.layoutManager = LinearLayoutManager(requireContext())
        rv.adapter = adapter

        // 类型筛选按钮
        val filterButtons = mapOf(
            null to view.findViewById<Button>(R.id.btn_log_all),
            "info" to view.findViewById<Button>(R.id.btn_log_info),
            "success" to view.findViewById<Button>(R.id.btn_log_success),
            "warning" to view.findViewById<Button>(R.id.btn_log_warning),
            "error" to view.findViewById<Button>(R.id.btn_log_error)
        )
        filterButtons.forEach { (type, btn) ->
            btn.setOnClickListener { setFilter(type, filterButtons) }
        }
        setFilter(null, filterButtons) // 默认选中全部

        // 导出/清空
        view.findViewById<Button>(R.id.btn_export_log).setOnClickListener {
            val csv = LogManager.exportCsv()
            val intent = Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
                addCategory(Intent.CATEGORY_OPENABLE)
                type = "text/csv"
                putExtra(Intent.EXTRA_TITLE, "log_${System.currentTimeMillis()}.csv")
            }
            startActivityForResult(intent, 2001)
        }

        view.findViewById<Button>(R.id.btn_clear_log).setOnClickListener {
            AlertDialog.Builder(requireContext())
                .setTitle("清空日志")
                .setMessage("确认清空所有日志记录？此操作不可恢复。")
                .setPositiveButton("确认") { _, _ ->
                    ConnectionManager.clearLogs()
                    reloadLogs()
                    Toast.makeText(requireContext(), "日志已清空", Toast.LENGTH_SHORT).show()
                }
                .setNegativeButton("取消", null)
                .show()
        }

        // 观察日志变化
        ConnectionManager.logs.observe(viewLifecycleOwner) { logs ->
            val filtered = if (currentFilter == null) logs
            else logs.filter { it.type in currentFilter!! }
            adapter.submitList(filtered)
            txtCount.text = "共 ${filtered.size}/${logs.size} 条"
            if (filtered.isEmpty()) {
                txtEmpty.visibility = View.VISIBLE
            } else {
                txtEmpty.visibility = View.GONE
            }
        }

        ConnectionManager.refreshLogs()
    }

    private fun setFilter(type: String?, buttons: Map<String?, Button>) {
        currentFilter = if (type == null) null else listOf(type)
        buttons.forEach { (t, btn) ->
            btn.setBackgroundColor(if (t == type) Color.parseColor("#2980b9") else Color.parseColor("#bdc3c7"))
        }
        reloadLogs()
    }

    private fun reloadLogs() {
        ConnectionManager.refreshLogs()
    }

    inner class LogAdapter : RecyclerView.Adapter<LogAdapter.VH>() {
        private var items = listOf<LogEntry>()

        fun submitList(list: List<LogEntry>) {
            items = list
            notifyDataSetChanged()
        }

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH {
            val v = LayoutInflater.from(parent.context).inflate(R.layout.item_log, parent, false)
            return VH(v)
        }

        override fun onBindViewHolder(holder: VH, position: Int) {
            holder.bind(items[position])
        }

        override fun getItemCount() = items.size

        inner class VH(v: View) : RecyclerView.ViewHolder(v) {
            private val typeTxt: TextView = v.findViewById(R.id.txt_log_type)
            private val timeTxt: TextView = v.findViewById(R.id.txt_log_time)
            private val deviceTxt: TextView = v.findViewById(R.id.txt_log_device)
            private val msgTxt: TextView = v.findViewById(R.id.txt_log_msg)

            fun bind(e: LogEntry) {
                typeTxt.text = when (e.type) {
                    "info" -> "信息"
                    "success" -> "成功"
                    "warning" -> "警告"
                    "error" -> "错误"
                    else -> e.type
                }
                when (e.type) {
                    "info" -> typeTxt.setBackgroundColor(Color.parseColor("#3498db"))
                    "success" -> typeTxt.setBackgroundColor(Color.parseColor("#2ecc71"))
                    "warning" -> typeTxt.setBackgroundColor(Color.parseColor("#f39c12"))
                    "error" -> typeTxt.setBackgroundColor(Color.parseColor("#e74c3c"))
                    else -> typeTxt.setBackgroundColor(Color.parseColor("#95a5a6"))
                }
                val sdf = SimpleDateFormat("HH:mm:ss", Locale.US)
                timeTxt.text = sdf.format(Date(e.time))
                deviceTxt.text = e.deviceId.take(16)
                msgTxt.text = e.message
            }
        }
    }
}
