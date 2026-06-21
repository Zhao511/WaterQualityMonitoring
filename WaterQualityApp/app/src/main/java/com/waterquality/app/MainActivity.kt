package com.waterquality.app

import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import androidx.fragment.app.Fragment
import com.waterquality.app.data.local.PreferencesManager
import com.waterquality.app.data.remote.ConnectionManager
import com.waterquality.app.databinding.ActivityMainBinding
import com.waterquality.app.ui.alarm.AlarmFragment
import com.waterquality.app.ui.device.DeviceListFragment
import com.waterquality.app.ui.home.HomeFragment
import com.waterquality.app.ui.settings.SettingsFragment

class MainActivity : AppCompatActivity() {
    private lateinit var binding: ActivityMainBinding
    private var homeFragment: HomeFragment? = null
    private var alarmFragment: AlarmFragment? = null
    private var deviceFragment: DeviceListFragment? = null
    private var settingsFragment: SettingsFragment? = null
    private var activeFragment: Fragment? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        // 自动连接（仅在保存过凭证时尝试）
        val prefs = PreferencesManager(this)
        if (prefs.ak.isNotEmpty() && prefs.sk.isNotEmpty() && !ConnectionManager.isConnected) {
            ConnectionManager.autoConnect { success, msg ->
                if (!success) {
                    android.util.Log.w("MainActivity", "自动连接失败: $msg")
                }
            }
        }

        homeFragment = HomeFragment()
        alarmFragment = AlarmFragment()
        deviceFragment = DeviceListFragment()
        settingsFragment = SettingsFragment()

        if (savedInstanceState == null) {
            showFragment(homeFragment!!)
            binding.bottomNav.selectedItemId = R.id.nav_home
        }

        binding.bottomNav.setOnItemSelectedListener { item ->
            when (item.itemId) {
                R.id.nav_home -> showFragment(homeFragment!!)
                R.id.nav_alarms -> showFragment(alarmFragment!!)
                R.id.nav_devices -> showFragment(deviceFragment!!)
                R.id.nav_settings -> showFragment(settingsFragment!!)
            }
            true
        }
    }

    private fun showFragment(fragment: Fragment) {
        if (fragment == activeFragment) return
        val tx = supportFragmentManager.beginTransaction()
        if (fragment.isAdded) {
            tx.hide(activeFragment ?: fragment).show(fragment)
        } else {
            if (activeFragment != null) tx.hide(activeFragment!!)
            tx.add(R.id.fragment_container, fragment)
        }
        tx.commit()
        activeFragment = fragment
    }

    // ==================== 生命周期 ====================

    override fun onStart() {
        super.onStart()
        ConnectionManager.setForegroundState(true)
    }

    override fun onStop() {
        super.onStop()
        ConnectionManager.setForegroundState(false)
    }

    override fun onDestroy() {
        super.onDestroy()
        // 仅在真正退出时完全断开（非配置变更如屏幕旋转）
        if (isFinishing) {
            ConnectionManager.disconnect()
        }
    }
}
