/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.google.androidgamesdk.gametextinput.test;

import android.os.Bundle;
import android.text.InputType;
import android.view.inputmethod.EditorInfo;
import android.widget.Button;
import android.widget.TextView;
import androidx.appcompat.app.AppCompatActivity;
import com.google.androidgamesdk.gametextinput.test.R;

// import com.google.androidgamesdk.gametextinput.InputConnection;

public class MainActivity extends AppCompatActivity {
  InputEnabledTextView inputEnabledTextView;
  TextView displayedText;

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    setContentView(R.layout.activity_main);

    inputEnabledTextView = (InputEnabledTextView) findViewById(R.id.input_enabled_text_view);
    assert (inputEnabledTextView != null);

    displayedText = (TextView) findViewById(R.id.displayed_text);
    assert (displayedText != null);

    inputEnabledTextView.createInputConnection(InputType.TYPE_CLASS_TEXT, this);
  }

  public void setDisplayedText(String text) {
    displayedText.setText(text);
  }

  public String getTestString() {
    return "abc";
  }

  public void enableSoftKeyboard() {
    inputEnabledTextView.enableSoftKeyboard();
  }
}
