/*
 * Copyright 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.google.android.games.paddleboat.gamecontrollersample;

import android.os.Build.VERSION;
import android.os.Build.VERSION_CODES;
import android.os.Bundle;
import android.view.View;
import android.view.WindowManager.LayoutParams;
import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.core.view.WindowInsetsControllerCompat;
import com.google.androidgamesdk.GameActivity;

// A minimal extension of GameActivity. For this sample, it is only used to invoke
// a workaround for loading the runtime shared library on old Android versions
public class PaddleboatDemoActivity extends GameActivity {
  // Load our native library:
  static {
    // Load the STL first to workaround issues on old Android versions:
    // "if your app targets a version of Android earlier than
    // Android 4.3 (Android API level 18),
    // and you use libc++_shared.so, you must load the shared library before any other
    // library that depends on it."
    // See https://developer.android.com/ndk/guides/cpp-support#shared_runtimes
    System.loadLibrary("c++_shared");

    // Load the 'paddleboat_demo' library:
    System.loadLibrary("paddleboat_demo");
  }

  private void hideSystemUI() {
    // This will put the game behind any cutouts and waterfalls on devices which have
    // them, so the corresponding insets will be non-zero.
    if (VERSION.SDK_INT >= VERSION_CODES.P) {
      getWindow().getAttributes().layoutInDisplayCutoutMode =
          LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
    }
    // From API 30 onwards, this is the recommended way to hide the system UI, rather than
    // using View.setSystemUiVisibility.
    View decorView = getWindow().getDecorView();
    WindowInsetsControllerCompat controller =
        new WindowInsetsControllerCompat(getWindow(), decorView);
    controller.hide(WindowInsetsCompat.Type.systemBars());
    controller.hide(WindowInsetsCompat.Type.displayCutout());
    controller.setSystemBarsBehavior(
        WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
  }

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    // When true, the app will fit inside any system UI windows.
    // When false, we render behind any system UI windows.
    WindowCompat.setDecorFitsSystemWindows(getWindow(), false);
    hideSystemUI();
    super.onCreate(savedInstanceState);
  }
}
