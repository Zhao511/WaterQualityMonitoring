package com.waterquality.app.ui.widgets

import android.content.Context
import android.graphics.*
import android.util.AttributeSet
import android.view.View
import kotlin.math.max
import kotlin.math.min

class LineChartView @JvmOverloads constructor(
    context: Context, attrs: AttributeSet? = null, defStyleAttr: Int = 0
) : View(context, attrs, defStyleAttr) {

    private val dataPoints = mutableListOf<Pair<Long, Double>>()
    private val linePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#2196F3"); strokeWidth = 3f; style = Paint.Style.STROKE }
    private val gridPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#E0E0E0"); strokeWidth = 1f }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.GRAY; textSize = 26f; textAlign = Paint.Align.RIGHT }
    private val labelPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.DKGRAY; textSize = 32f; textAlign = Paint.Align.CENTER }
    private val dotPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#2196F3"); style = Paint.Style.FILL }

    var caption: String = ""

    fun setData(data: List<Pair<Long, Double>>) {
        dataPoints.clear()
        dataPoints.addAll(data)
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val w = width.toFloat()
        val h = height.toFloat()
        val padLeft = 100f; val padRight = 20f; val padTop = 60f; val padBottom = 60f

        if (dataPoints.isEmpty() || w <= padLeft + padRight || h <= padTop + padBottom) return

        val vals = dataPoints.map { it.second }
        val vMin = vals.min() * 0.9
        val vMax = vals.max() * 1.1
        val vRange = if (vMax == vMin) 1.0 else vMax - vMin

        // Grid
        val chartW = w - padLeft - padRight
        val chartH = h - padTop - padBottom
        for (i in 0..4) {
            val y = padTop + chartH * i / 4
            canvas.drawLine(padLeft, y, w - padRight, y, gridPaint)
            val label = String.format("%.1f", vMax - vRange * i / 4)
            canvas.drawText(label, padLeft - 10, y + 10, textPaint)
        }

        // Line
        if (dataPoints.size < 2) return
        val path = Path()
        val tRange = max(1L, dataPoints.last().first - dataPoints.first().first)
        dataPoints.forEachIndexed { i, (t, v) ->
            val x = padLeft + if (tRange == 0L) chartW * i / dataPoints.size else chartW * ((t - dataPoints.first().first).toFloat() / tRange)
            val y = padTop + chartH * (1 - (v - vMin) / vRange).toFloat()
            if (i == 0) path.moveTo(x, y) else path.lineTo(x, y)
            canvas.drawCircle(x, y, 6f, dotPaint)
        }
        canvas.drawPath(path, linePaint)

        // Caption
        canvas.drawText(caption, w / 2, 40f, labelPaint)
    }
}
