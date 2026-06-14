package com.waterquality.app.util

import android.content.Context
import android.widget.Toast

fun Context.toast(msg: String, duration: Int = Toast.LENGTH_SHORT) {
    Toast.makeText(this, msg, duration).show()
}

fun Double.format(digits: Int): String = String.format("%.${digits}f", this)
