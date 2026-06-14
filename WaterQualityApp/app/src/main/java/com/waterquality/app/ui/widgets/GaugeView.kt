package com.waterquality.app.ui.widgets

import android.content.Context
import android.graphics.*
import android.util.AttributeSet
import android.view.View
import kotlin.math.cos
import kotlin.math.min
import kotlin.math.sin

class GaugeView @JvmOverloads constructor(
    context: Context, attrs: AttributeSet? = null, defStyleAttr: Int = 0
) : View(context, attrs, defStyleAttr) {

    var value: Double = 0.0
        set(v) { field = v; invalidate() }
    var minValue: Double = 0.0
    var maxValue: Double = 100.0
    var caption: String = ""
    var unit: String = ""
    var ranges: List<Triple<Double, Double, Int>> = emptyList()

    private val arcPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeCap = Paint.Cap.ROUND }
    private val needlePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.RED; strokeWidth = 4f }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER }
    private val rect = RectF()
    private val bgPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE; color = Color.parseColor("#E0E0E0"); strokeWidth = 20f; strokeCap = Paint.Cap.ROUND
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val w = width.toFloat()
        val h = height.toFloat()
        val cx = w / 2
        val cy = h * 0.6f
        val radius = min(w, h) * 0.35f
        val strokeW = radius * 0.18f

        arcPaint.strokeWidth = strokeW
        bgPaint.strokeWidth = strokeW
        rect.set(cx - radius, cy - radius, cx + radius, cy + radius)

        // Background arc
        canvas.drawArc(rect, 135f, 270f, false, bgPaint)

        // Color arcs
        if (ranges.isEmpty()) {
            arcPaint.color = Color.parseColor("#27AE60")
            canvas.drawArc(rect, 135f, 270f, false, arcPaint)
        } else {
            for (r in ranges) {
                val pct1 = (r.first - minValue) / (maxValue - minValue)
                val pct2 = (r.second - minValue) / (maxValue - minValue)
                arcPaint.color = r.third
                canvas.drawArc(rect, 135f + (pct1 * 270).toFloat(), ((pct2 - pct1) * 270).toFloat(), false, arcPaint)
            }
        }

        // Needle
        val angle = 135f + ((value - minValue) / (maxValue - minValue) * 270).toFloat().coerceIn(135f, 405f)
        val rad = Math.toRadians(angle.toDouble())
        val nx = cx + (radius * 0.7f * cos(rad)).toFloat()
        val ny = cy + (radius * 0.7f * sin(rad)).toFloat()
        canvas.drawLine(cx, cy, nx, ny, needlePaint)
        canvas.drawCircle(cx, cy, strokeW * 0.4f, Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.RED })

        // Caption
        textPaint.color = Color.parseColor("#666666")
        textPaint.textSize = radius * 0.18f
        canvas.drawText(caption, cx, cy + radius * 0.7f, textPaint)

        // Value
        textPaint.color = Color.parseColor("#333333")
        textPaint.textSize = radius * 0.35f
        textPaint.typeface = Typeface.DEFAULT_BOLD
        canvas.drawText(String.format("%.1f", value), cx, cy + radius * 1.0f, textPaint)

        // Unit
        textPaint.textSize = radius * 0.16f
        textPaint.typeface = Typeface.DEFAULT
        textPaint.color = Color.parseColor("#999999")
        canvas.drawText(unit, cx, cy + radius * 1.25f, textPaint)
    }
}
