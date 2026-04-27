// SPDX-License-Identifier: Apache-2.0

package com.fuvr.quest

import android.app.NativeActivity

class MainActivity : NativeActivity() {
    companion object {
        init {
            System.loadLibrary("fuvr_quest")
        }
    }
}
