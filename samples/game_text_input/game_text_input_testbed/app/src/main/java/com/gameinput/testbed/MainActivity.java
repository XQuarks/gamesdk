/*
 * Copyright (C) 2021 The Android Open Source Project
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
package com.gametextinput.testbed;

import androidx.appcompat.app.AppCompatActivity;

import android.os.Bundle;
import android.text.InputType;
import android.view.inputmethod.EditorInfo;
import android.widget.Button;
import android.widget.TextView;

import com.google.androidgamesdk.gametextinput.InputConnection;

public class MainActivity extends AppCompatActivity {
    static {
        System.loadLibrary("native-lib");
    }

    InputEnabledTextView inputEnabledTextView;
    TextView displayedText;

    native void onCreated();
    native void setInputConnectionNative(InputConnection c);
    native void showIme();
    native void hideIme();
    native void sendSelectionToStart();
    native void sendSelectionToEnd();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        inputEnabledTextView =
                (InputEnabledTextView) findViewById(R.id.input_enabled_text_view);
        assert(inputEnabledTextView != null);

        displayedText =
                (TextView) findViewById(R.id.displayed_text);
        assert(displayedText != null);

        Button showImeButton = (Button) findViewById(R.id.show_ime_button);
        showImeButton.setOnClickListener(view -> showIme());
        Button hideImeButton = (Button) findViewById(R.id.hide_ime_button);
        hideImeButton.setOnClickListener(view -> hideIme());
        Button selectionStartButton = (Button) findViewById(R.id.selection_start_button);
        selectionStartButton.setOnClickListener(view -> sendSelectionToStart());
        Button selectionEndButton = (Button) findViewById(R.id.selection_end_button);
        selectionEndButton.setOnClickListener(view -> sendSelectionToEnd());
        Button typeNullButton = (Button) findViewById(R.id.type_null_button);
        typeNullButton.setOnClickListener(view -> {
            EditorInfo editorInfo = new EditorInfo();
            editorInfo.inputType = InputType.TYPE_NULL;
            inputEnabledTextView.mInputConnection.setEditorInfo(editorInfo);
            inputEnabledTextView.mInputConnection.restartInput();
        });
        Button typeTextButton = (Button) findViewById(R.id.type_text_button);
        typeTextButton.setOnClickListener(view -> {
            EditorInfo editorInfo = new EditorInfo();
            editorInfo.inputType = InputType.TYPE_CLASS_TEXT;
            inputEnabledTextView.mInputConnection.setEditorInfo(editorInfo);
            inputEnabledTextView.mInputConnection.restartInput();
        });
        Button typeNumberButton = (Button) findViewById(R.id.type_number_button);
        typeNumberButton.setOnClickListener(view -> {
            EditorInfo editorInfo = new EditorInfo();
            editorInfo.inputType = InputType.TYPE_CLASS_NUMBER;
            inputEnabledTextView.mInputConnection.setEditorInfo(editorInfo);
            inputEnabledTextView.mInputConnection.restartInput();
        });

        onCreated();
        inputEnabledTextView.createInputConnection(InputType.TYPE_CLASS_TEXT);
        setInputConnectionNative(inputEnabledTextView.mInputConnection);
    }

    public void setDisplayedText(String text) {
        displayedText.setText(text);
    }
}