package com.waterquality.app.ui.device

import android.app.AlertDialog
import android.graphics.Color
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.*
import androidx.fragment.app.Fragment
import androidx.lifecycle.ViewModelProvider
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.waterquality.app.R
import com.waterquality.app.data.model.Device
import com.waterquality.app.data.remote.ConnectionManager
import com.waterquality.app.ui.home.HomeViewModel

class DeviceListFragment : Fragment() {

    private lateinit var homeVM: HomeViewModel
    private lateinit var rv: RecyclerView
    private lateinit var txtEmpty: TextView
    private lateinit var btnRefresh: Button
    private val adapter = DeviceAdapter()

    override fun onCreateView(inflater: LayoutInflater, c: ViewGroup?, s: Bundle?): View {
        return inflater.inflate(R.layout.fragment_device_list, c, false)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        homeVM = ViewModelProvider(requireActivity())[HomeViewModel::class.java]

        rv = view.findViewById(R.id.rv_devices)
        txtEmpty = view.findViewById(R.id.txt_device_empty)
        btnRefresh = view.findViewById(R.id.btn_refresh_devices)

        rv.layoutManager = LinearLayoutManager(requireContext())
        rv.adapter = adapter

        btnRefresh.setOnClickListener {
            // 触发重新加载
            ConnectionManager.refreshLogs()
            Toast.makeText(requireContext(), "设备列表将在下次轮询时刷新", Toast.LENGTH_SHORT).show()
        }

        homeVM.devices.observe(viewLifecycleOwner) { devices ->
            adapter.submitList(devices)
            if (devices.isEmpty()) {
                txtEmpty.visibility = View.VISIBLE
                rv.visibility = View.GONE
            } else {
                txtEmpty.visibility = View.GONE
                rv.visibility = View.VISIBLE
            }
        }
    }

    private fun showEditDialog(device: Device) {
        val dialogView = LayoutInflater.from(requireContext()).inflate(R.layout.dialog_edit_device, null)
        val editName = dialogView.findViewById<EditText>(R.id.edit_device_name)
        val editLocation = dialogView.findViewById<EditText>(R.id.edit_device_location)

        editName.setText(device.name)
        editLocation.setText(device.location)

        AlertDialog.Builder(requireContext())
            .setTitle("编辑设备信息")
            .setView(dialogView)
            .setPositiveButton("保存") { _, _ ->
                val name = editName.text.toString().trim()
                val location = editLocation.text.toString().trim()
                if (name.isEmpty()) {
                    Toast.makeText(requireContext(), "设备名称不能为空", Toast.LENGTH_SHORT).show()
                    return@setPositiveButton
                }
                val deviceId = device.rfid.ifEmpty { device.sourceDeviceId.ifEmpty { device.id } }
                ConnectionManager.updateDevice(deviceId, name, location) { success, msg ->
                    Toast.makeText(requireContext(), msg, Toast.LENGTH_SHORT).show()
                }
            }
            .setNegativeButton("取消", null)
            .show()
    }

    private fun showDeleteConfirm(device: Device) {
        val label = device.rfid.ifEmpty { device.name.ifEmpty { device.id } }
        AlertDialog.Builder(requireContext())
            .setTitle("确认删除")
            .setMessage("确认删除设备 \"$label\"？\n\n此操作将删除设备数据及其历史记录和告警记录，且不可恢复。")
            .setPositiveButton("确认删除") { _, _ ->
                val deviceId = device.rfid.ifEmpty { device.sourceDeviceId.ifEmpty { device.id } }
                ConnectionManager.deleteDevice(deviceId) { success, msg ->
                    Toast.makeText(requireContext(), msg, Toast.LENGTH_SHORT).show()
                }
            }
            .setNegativeButton("取消", null)
            .show()
    }

    inner class DeviceAdapter : RecyclerView.Adapter<DeviceAdapter.VH>() {
        private var items = listOf<Device>()

        fun submitList(list: List<Device>) {
            items = list
            notifyDataSetChanged()
        }

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH {
            val v = LayoutInflater.from(parent.context).inflate(R.layout.item_device_row, parent, false)
            return VH(v)
        }

        override fun onBindViewHolder(holder: VH, position: Int) {
            holder.bind(items[position])
        }

        override fun getItemCount() = items.size

        inner class VH(v: View) : RecyclerView.ViewHolder(v) {
            private val rfidTxt: TextView = v.findViewById(R.id.txt_dev_rfid)
            private val locTxt: TextView = v.findViewById(R.id.txt_dev_location)
            private val nameTxt: TextView = v.findViewById(R.id.txt_dev_name)
            private val statusTxt: TextView = v.findViewById(R.id.txt_dev_status)
            private val btnView: Button = v.findViewById(R.id.btn_dev_view)
            private val btnEdit: Button = v.findViewById(R.id.btn_dev_edit)
            private val btnDelete: Button = v.findViewById(R.id.btn_dev_delete)

            fun bind(d: Device) {
                rfidTxt.text = d.rfid.ifEmpty { d.name.ifEmpty { d.id.take(12) } }
                locTxt.text = d.location.ifEmpty { "-" }
                nameTxt.text = d.name.ifEmpty { "-" }
                statusTxt.text = when (d.status) {
                    "ONLINE" -> "在线"
                    "OFFLINE" -> "离线"
                    "UNKNOWN" -> "未知"
                    else -> "未知"
                }
                statusTxt.setTextColor(when (d.status) {
                    "ONLINE" -> Color.parseColor("#27ae60")
                    "OFFLINE" -> Color.parseColor("#e67e22")
                    else -> Color.parseColor("#95a5a6")
                })

                btnView.setOnClickListener { homeVM.switchDevice(d.id) }
                btnEdit.setOnClickListener { showEditDialog(d) }
                btnDelete.setOnClickListener { showDeleteConfirm(d) }
            }
        }
    }
}
