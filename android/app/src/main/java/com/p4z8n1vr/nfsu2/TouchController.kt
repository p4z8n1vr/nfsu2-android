package com.p4z8n1vr.nfsu2

import android.view.MotionEvent
import android.view.View

internal class TouchController(
    private val overlay: View
) {
    private data class Control(
        val id: Int,
        val digitalMask: Int = 0,
        val analogIndex: Int = -1
    )

    private companion object {
        const val PORT = 0

        const val DPAD_UP = 0x0001
        const val DPAD_DOWN = 0x0002
        const val DPAD_LEFT = 0x0004
        const val DPAD_RIGHT = 0x0008
        const val START = 0x0010
        const val BACK = 0x0020

        const val ANALOG_A = 0
        const val ANALOG_B = 1
        const val ANALOG_X = 2
        const val ANALOG_Y = 3
        const val ANALOG_BLACK = 4
        const val ANALOG_WHITE = 5
        const val ANALOG_LT = 6
        const val ANALOG_RT = 7
    }

    private val controls = listOf(
        Control(R.id.controller_up, DPAD_UP),
        Control(R.id.controller_down, DPAD_DOWN),
        Control(R.id.controller_left, DPAD_LEFT),
        Control(R.id.controller_right, DPAD_RIGHT),
        Control(R.id.controller_start, START),
        Control(R.id.controller_back, BACK),
        Control(R.id.controller_a, analogIndex = ANALOG_A),
        Control(R.id.controller_b, analogIndex = ANALOG_B),
        Control(R.id.controller_x, analogIndex = ANALOG_X),
        Control(R.id.controller_y, analogIndex = ANALOG_Y),
        Control(R.id.controller_black, analogIndex = ANALOG_BLACK),
        Control(R.id.controller_white, analogIndex = ANALOG_WHITE),
        Control(R.id.controller_lt, analogIndex = ANALOG_LT),
        Control(R.id.controller_rt, analogIndex = ANALOG_RT)
    )

    private val pressed = mutableSetOf<Int>()

    init {
        controls.forEach { control ->
            val view = overlay.findViewById<View>(control.id)
            val pointers = mutableSetOf<Int>()

            view.setOnTouchListener { button, event ->
                val wasPressed = pointers.isNotEmpty()

                when (event.actionMasked) {
                    MotionEvent.ACTION_DOWN,
                    MotionEvent.ACTION_POINTER_DOWN -> {
                        pointers.add(
                            event.getPointerId(event.actionIndex)
                        )
                    }

                    MotionEvent.ACTION_UP,
                    MotionEvent.ACTION_POINTER_UP -> {
                        pointers.remove(
                            event.getPointerId(event.actionIndex)
                        )
                    }

                    MotionEvent.ACTION_CANCEL -> {
                        pointers.clear()
                    }
                }

                val isPressed = pointers.isNotEmpty()

                if (wasPressed != isPressed) {
                    if (isPressed) {
                        pressed.add(control.id)
                    } else {
                        pressed.remove(control.id)
                    }

                    button.isPressed = isPressed
                    publish(connected = true)
                }

                if (event.actionMasked == MotionEvent.ACTION_UP) {
                    button.performClick()
                }

                true
            }
        }

        // Advertise Xbox controller port 0 before game boot.
        publish(connected = true)
    }

    private fun publish(connected: Boolean) {
        var buttons = 0
        var analog = 0L

        if (connected) {
            controls.forEach { control ->
                if (control.id !in pressed) {
                    return@forEach
                }

                buttons = buttons or control.digitalMask

                if (control.analogIndex >= 0) {
                    analog = analog or (
                        0xFFL shl (control.analogIndex * 8)
                    )
                }
            }
        }

        NativeBridge.updatePad(
            port = PORT,
            connected = connected,
            buttons = buttons,
            analog = analog,
            lx = 0,
            ly = 0,
            rx = 0,
            ry = 0
        )
    }

    fun disconnect() {
        pressed.clear()
        publish(connected = false)
    }
}
