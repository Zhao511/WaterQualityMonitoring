package com.waterquality.app

import android.app.Application

class WaterQualityApp : Application() {
    companion object {
        lateinit var instance: WaterQualityApp
            private set
    }

    override fun onCreate() {
        super.onCreate()
        instance = this
    }
}
