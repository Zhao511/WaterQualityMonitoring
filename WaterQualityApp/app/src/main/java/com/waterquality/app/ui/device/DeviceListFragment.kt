package com.waterquality.app.ui.device

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ArrayAdapter
import androidx.fragment.app.Fragment
import androidx.lifecycle.ViewModelProvider
import com.waterquality.app.databinding.FragmentDeviceListBinding
import com.waterquality.app.ui.home.HomeViewModel

class DeviceListFragment : Fragment() {
    private var _binding: FragmentDeviceListBinding? = null
    private val binding get() = _binding!!
    private lateinit var homeVM: HomeViewModel

    override fun onCreateView(inflater: LayoutInflater, c: ViewGroup?, s: Bundle?): View {
        _binding = FragmentDeviceListBinding.inflate(inflater, c, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        homeVM = ViewModelProvider(requireActivity())[HomeViewModel::class.java]

        homeVM.devices.observe(viewLifecycleOwner) { devices ->
            val names = devices.map { "${it.name} [${if (it.status == "ONLINE") "在线" else "离线"}]" }
            binding.deviceList.adapter = ArrayAdapter(requireContext(), android.R.layout.simple_list_item_1, names)
            binding.deviceList.setOnItemClickListener { _, _, pos, _ ->
                devices.getOrNull(pos)?.let { homeVM.switchDevice(it.id) }
            }
        }

        binding.btnRefresh.setOnClickListener {
            homeVM.switchDevice(homeVM.currentDevice.value?.id ?: "")
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }
}
