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
package com.google.androidgamesdk.gametextinput;

import android.app.Activity;
import android.content.Context;
import android.os.Build.VERSION;
import android.os.Build.VERSION_CODES;
import android.text.Editable;
import android.text.InputFilter;
import android.text.SpannableString;
import android.text.Spanned;
import android.text.SpannableStringBuilder;
import android.text.TextUtils;
import android.util.Log;
import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import androidx.annotation.Keep;
import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;
import com.google.androidgamesdk.gametextinput.GameTextInput.Pair;
import java.util.BitSet;

@Keep
public class InputConnection
    extends BaseInputConnection
    implements View.OnKeyListener {
  private static final String TAG = "gti.InputConnection";
  // TODO: (b/183179971) We should react to most of these events rather than ignoring them? Plus
  // there are others that should be ignored.
  private static final int[] notInsertedKeyCodes = {
      // Start of common game controller button keycodes
      KeyEvent.KEYCODE_DPAD_CENTER, KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_DPAD_UP,
      KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_DPAD_DOWN_LEFT,
      KeyEvent.KEYCODE_DPAD_UP_LEFT, KeyEvent.KEYCODE_DPAD_UP_LEFT, KeyEvent.KEYCODE_DPAD_UP_RIGHT,
      KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BUTTON_X,
      KeyEvent.KEYCODE_BUTTON_Y, KeyEvent.KEYCODE_BUTTON_L1, KeyEvent.KEYCODE_BUTTON_L2,
      KeyEvent.KEYCODE_BUTTON_R1, KeyEvent.KEYCODE_BUTTON_R2, KeyEvent.KEYCODE_BUTTON_THUMBL,
      KeyEvent.KEYCODE_BUTTON_THUMBR, KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_BUTTON_START,
      KeyEvent.KEYCODE_BUTTON_MODE, KeyEvent.KEYCODE_MEDIA_RECORD, KeyEvent.KEYCODE_BUTTON_Z,
      KeyEvent.KEYCODE_BUTTON_C,

      // End of common game controller button keycodes
      KeyEvent.KEYCODE_DEL, KeyEvent.KEYCODE_FORWARD_DEL, KeyEvent.KEYCODE_CTRL_RIGHT,
      KeyEvent.KEYCODE_SHIFT_LEFT, KeyEvent.KEYCODE_SHIFT_RIGHT, KeyEvent.KEYCODE_BACK,
      KeyEvent.KEYCODE_VOLUME_UP, KeyEvent.KEYCODE_VOLUME_DOWN, KeyEvent.KEYCODE_VOLUME_MUTE,
      KeyEvent.KEYCODE_ALT_LEFT, KeyEvent.KEYCODE_ALT_RIGHT, KeyEvent.KEYCODE_CTRL_LEFT,
      KeyEvent.KEYCODE_F1, KeyEvent.KEYCODE_F10, KeyEvent.KEYCODE_F11, KeyEvent.KEYCODE_F12,
      KeyEvent.KEYCODE_F2, KeyEvent.KEYCODE_F3, KeyEvent.KEYCODE_F4, KeyEvent.KEYCODE_F5,
      KeyEvent.KEYCODE_F6, KeyEvent.KEYCODE_F7, KeyEvent.KEYCODE_F8, KeyEvent.KEYCODE_F9,
      KeyEvent.KEYCODE_INSERT, KeyEvent.KEYCODE_MOVE_HOME, KeyEvent.KEYCODE_MOVE_END,
      KeyEvent.KEYCODE_PAGE_UP, KeyEvent.KEYCODE_PAGE_DOWN, KeyEvent.KEYCODE_UNKNOWN,
      KeyEvent.KEYCODE_SEARCH,

      // all media keys
      KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE,
      KeyEvent.KEYCODE_MEDIA_STOP,
      KeyEvent.KEYCODE_MEDIA_NEXT,
      KeyEvent.KEYCODE_MEDIA_PREVIOUS,
      KeyEvent.KEYCODE_MEDIA_REWIND,
      KeyEvent.KEYCODE_MEDIA_FAST_FORWARD,
      KeyEvent.KEYCODE_MEDIA_PLAY,
      KeyEvent.KEYCODE_MEDIA_PAUSE,
      KeyEvent.KEYCODE_MEDIA_CLOSE,
      KeyEvent.KEYCODE_MEDIA_EJECT,
      KeyEvent.KEYCODE_MEDIA_RECORD,
      KeyEvent.KEYCODE_MEDIA_AUDIO_TRACK,
      KeyEvent.KEYCODE_MEDIA_TOP_MENU,
      KeyEvent.KEYCODE_TV_MEDIA_CONTEXT_MENU,
      KeyEvent.KEYCODE_MEDIA_SKIP_FORWARD,
      KeyEvent.KEYCODE_MEDIA_SKIP_BACKWARD,
      KeyEvent.KEYCODE_MEDIA_STEP_FORWARD,
      KeyEvent.KEYCODE_MEDIA_STEP_BACKWARD,
  };
  private final InputMethodManager imm;
  private final View targetView;
  private final Settings settings;
  private final Editable mEditable;
  // The characters we should not insert into a string.
  private final BitSet dontInsertChars;
  private Listener listener;
  private boolean mSoftKeyboardActive;

  /*
   * This class filters EOL characters from the input. For details of how InputFilter.filter
   * function works, refer to its documentation. If the suggested change is accepted without
   * modifications, filter() should return null.
   */
  private class SingeLineFilter implements InputFilter {
     public CharSequence filter(CharSequence source, int start, int end,
                                Spanned dest, int dstart, int dend) {

       boolean keepOriginal = true;
       StringBuilder builder = new StringBuilder(end - start);

       for (int i = start; i < end; i++) {
         char c = source.charAt(i);

         if (c == '\n') {
           keepOriginal = false;
         } else {
           builder.append(c);
         }
       }

       if (keepOriginal) {
         return null;
       }

       if (source instanceof Spanned) {
         SpannableString s = new SpannableString(builder);
         TextUtils.copySpansFrom((Spanned) source, start, builder.length(), null, s, 0);
         return s;
       } else {
         return builder;
       }
     }
  }

  private static final int MAX_LENGTH_FOR_SINGLE_LINE_EDIT_TEXT = 5000;

  /**
   * Constructor
   *
   * @param ctx        The app's context
   * @param targetView The view created this input connection
   * @param settings   EditorInfo and other settings needed by this class
   *                   InputConnection.
   */
  public InputConnection(Context ctx, View targetView, Settings settings) {
    super(targetView, settings.mEditorInfo.inputType != 0);
    this.targetView = targetView;
    this.settings = settings;
    Object imm = ctx.getSystemService(Context.INPUT_METHOD_SERVICE);
    if (imm == null) {
      throw new java.lang.RuntimeException("Can't get IMM");
    } else {
      this.imm = (InputMethodManager) imm;
      this.mEditable = (Editable) (new SpannableStringBuilder());
    }
    // BitSet.valueOf is only available in API 30 so insert manually.
    dontInsertChars = new BitSet();
    for (int c : notInsertedKeyCodes) {
      dontInsertChars.set(c);
    }
    // Listen for insets changes
    WindowCompat.setDecorFitsSystemWindows(((Activity)targetView.getContext()).getWindow(), false);
    targetView.setOnKeyListener(this);
    // Apply EditorInfo settings
    this.setEditorInfo(settings.mEditorInfo);
  }

  /**
   * Restart the input method manager. This is useful to apply changes to the keyboard
   * after calling setEditorInfo.
   */
  public void restartInput() {
    this.imm.restartInput(targetView);
  }

  /**
   * Get whether the soft keyboard is visible.
   *
   * @return true if the soft keyboard is visible, false otherwise
   */
  public final boolean getSoftKeyboardActive() {
    return this.mSoftKeyboardActive;
  }

  /**
   * Request the soft keyboard to become visible or invisible.
   *
   * @param active True if the soft keyboard should be made visible, otherwise false.
   * @param flags  See
   *     https://developer.android.com/reference/android/view/inputmethod/InputMethodManager#showSoftInput(android.view.View,%20int)
   */
  public final void setSoftKeyboardActive(boolean active, int flags) {
    if (active) {
      this.targetView.setFocusableInTouchMode(true);
      this.targetView.requestFocus();
      this.imm.showSoftInput(this.targetView, flags);
    } else {
      this.imm.hideSoftInputFromWindow(this.targetView.getWindowToken(), flags);
    }
  }

  /**
   * Get the current EditorInfo used to configure the InputConnection's behaviour.
   *
   * @return The current EditorInfo.
   */
  public final EditorInfo getEditorInfo() {
    return this.settings.mEditorInfo;
  }

  /**
   * Set the current EditorInfo used to configure the InputConnection's behaviour.
   *
   * @param editorInfo The EditorInfo to use
   */
  public final void setEditorInfo(EditorInfo editorInfo) {
    this.settings.mEditorInfo = editorInfo;

    // Depending on the multiline state, we might need a different set of filters.
    // Filters are being used to filter specific characters for hardware keyboards
    // (software input methods already support TYPE_TEXT_FLAG_MULTI_LINE).
    if ((settings.mEditorInfo.inputType & EditorInfo.TYPE_TEXT_FLAG_MULTI_LINE) == 0) {
      mEditable.setFilters(new InputFilter[]{
              new InputFilter.LengthFilter(MAX_LENGTH_FOR_SINGLE_LINE_EDIT_TEXT),
              new SingeLineFilter()
      });
    } else {
      mEditable.setFilters(new InputFilter[]{ });
    }
  }

  /**
   * Set the text, selection and composing region state.
   *
   * @param state The state to be used by the IME.
   *              This replaces any text, selections and composing regions currently active.
   */
  public final void setState(State state) {
    if (state == null)
      return;
    Log.d(TAG,
        "setState: '" + state.text + "', selection=(" + state.selectionStart + ","
            + state.selectionEnd + "), composing region=(" + state.composingRegionStart + ","
            + state.composingRegionEnd + ")");
    this.mEditable.clear();
    this.mEditable.insert(0, (CharSequence) state.text);
    this.setSelectionInternal(state.selectionStart, state.selectionEnd);
    this.setComposingRegionInternal(state.composingRegionStart, state.composingRegionEnd);
    this.informIMM();
  }

  /**
   * Get the current listener for state changes.
   *
   * @return The current Listener
   */
  public final Listener getListener() {
    return this.listener;
  }

  /**
   * Set a listener for state changes.
   *
   * @param listener
   * @return This InputConnection, for setter chaining.
   */
  public final InputConnection setListener(Listener listener) {
    this.listener = listener;
    return this;
  }

  // From View.OnKeyListener
  @Override
  public boolean onKey(View view, int i, KeyEvent keyEvent) {
    // Don't call sendKeyEvent as it might produce an infinite loop.
    return processKeyEvent(keyEvent);
  }

  // From BaseInputConnection
  @Override
  public Editable getEditable() {
    Log.d(TAG, "getEditable ");
    return this.mEditable;
  }

  // From BaseInputConnection
  @Override
  public boolean setSelection(int start, int end) {
    Log.d(TAG, "setSelection: " + start + ":" + end);
    this.setSelectionInternal(start, end);
    return true;
  }

  // From BaseInputConnection
  @Override
  public boolean setComposingText(CharSequence text, int newCursorPosition) {
    Log.d(TAG, String.format("setComposingText='%s' newCursorPosition=%d",
            text, newCursorPosition));
    if (text == null) {
      return false;
    } else {
      Pair composingRegion = this.getComposingRegion();
      if (composingRegion.first == -1) {
        composingRegion = this.getSelection();
        if (composingRegion.first == -1) {
          composingRegion = new Pair(0, 0);
        }
      }

      this.mEditable.delete(composingRegion.first, composingRegion.second);
      this.mEditable.insert(composingRegion.first, text);
      this.setComposingRegion(composingRegion.first, composingRegion.first + text.length());
      composingRegion = this.getComposingRegion();
      int actualNewCursorPosition = newCursorPosition > 0
          ? Math.min(composingRegion.second + newCursorPosition - 1, this.mEditable.length())
          : Math.max(0, composingRegion.first + newCursorPosition);
      this.setSelection(actualNewCursorPosition, actualNewCursorPosition);
      this.stateUpdated(false);
      return true;
    }
  }

  // From BaseInputConnection
  @Override
  public boolean setComposingRegion(int start, int end) {
    Log.d(TAG, "setComposingRegion: " + start + ":" + end);
    this.setComposingRegionInternal(start, end);
    this.stateUpdated(false);
    return true;
  }

  // From BaseInputConnection
  @Override
  public boolean finishComposingText() {
    Log.d(TAG, "finishComposingText");
    this.setComposingRegion(-1, -1);
    return true;
  }

  // From BaseInputConnection
  @Override
  public boolean commitText(CharSequence text, int newCursorPosition) {
    Log.d(TAG,
        (new StringBuilder())
            .append("Commit: ")
            .append(text)
            .append(", new pos = ")
            .append(newCursorPosition)
            .toString());
    this.setComposingText(text, newCursorPosition);
    this.finishComposingText();
    this.informIMM();
    return true;
  }

  // From BaseInputConnection
  @Override
  public boolean deleteSurroundingText(int beforeLength, int afterLength) {
    Log.d(TAG, "deleteSurroundingText: " + beforeLength + ":" + afterLength);
    Pair selection = this.getSelection();
    int first = Math.min(selection.first, selection.second);
    int second = Math.max(selection.first, selection.second);

    if (first == -1) {
      return false;
    }

    if (afterLength > 0) {
      this.mEditable.delete(Math.max(0, second),
              Math.min(this.mEditable.length(), second + afterLength));
    }

    if (beforeLength > 0) {
      this.mEditable.delete(Math.max(0, first - beforeLength), first);
    }

    this.stateUpdated(false);
    return true;
  }

  // From BaseInputConnection
  @Override
  public boolean deleteSurroundingTextInCodePoints(int beforeLength, int afterLength) {
    Log.d(TAG, "deleteSurroundingTextInCodePoints: " + beforeLength + ":" + afterLength);
    boolean res = super.deleteSurroundingTextInCodePoints(beforeLength, afterLength);
    this.stateUpdated(false);
    return res;
  }

  // From BaseInputConnection
  @Override
  public boolean sendKeyEvent(KeyEvent event) {
    if (settings.mForwardKeyEvents && VERSION.SDK_INT >= 24
        && this.settings.mEditorInfo.inputType == 0 && event != null) {
      this.imm.dispatchKeyEventFromInputMethod(this.targetView, event);
    }
    return processKeyEvent(event);
  }

  // From BaseInputConnection
  @Override
  public CharSequence getSelectedText(int flags) {
    Log.d(TAG, "getSelectedText: " + flags);
    return super.getSelectedText(flags);
  }

  // From BaseInputConnection
  @Override
  public CharSequence getTextAfterCursor(int length, int flags) {
    Log.d(TAG, "getTextAfterCursor: " + length + ":" + flags);
    if (length < 0) {
      Log.i(TAG, "getTextAfterCursor: returning null to due to an invalid length=" + length);
      return null;
    }
    return super.getTextAfterCursor(length, flags);
  }

  // From BaseInputConnection
  @Override
  public CharSequence getTextBeforeCursor(int length, int flags) {
    Log.d(TAG, "getTextBeforeCursor: " + length + ", flags=" + flags);
    if (length < 0) {
      Log.i(TAG, "getTextBeforeCursor: returning null to due to an invalid length=" + length);
      return null;
    }
    return super.getTextBeforeCursor(length, flags);
  }

  // From BaseInputConnection
  @Override
  public boolean requestCursorUpdates(int cursorUpdateMode) {
    Log.d(TAG, "Request cursor updates: " + cursorUpdateMode);
    return super.requestCursorUpdates(cursorUpdateMode);
  }

  // From BaseInputConnection
  @Override
  public void closeConnection() {
    Log.d(TAG, "closeConnection");
    super.closeConnection();
  }

  private final void informIMM() {
    Pair selection = this.getSelection();
    Pair cr = this.getComposingRegion();
    this.imm.updateSelection(
        this.targetView, selection.first, selection.second, cr.first, cr.second);
  }

  private final Pair getSelection() {
    return GameTextInput.getSelection(this.mEditable);
  }

  private final Pair getComposingRegion() {
    return GameTextInput.getComposingRegion(this.mEditable);
  }

  private final void setSelectionInternal(int start, int end) {
    GameTextInput.setSelection(this.mEditable, start, end);
  }

  private final void setComposingRegionInternal(int start_in, int end_in) {
    // start_in might be greater than end_in
    int start = Math.min(start_in, end_in);
    int end = Math.max(start_in, end_in);

    if (start == -1) {
      GameTextInput.removeComposingRegion(this.mEditable);
    } else {
      start = Math.min(this.mEditable.length(), Math.max(0, start));
      end = Math.min(this.mEditable.length(), Math.max(0, end));
      GameTextInput.setComposingRegion(this.mEditable, start, end);
    }
  }

  private boolean processKeyEvent(KeyEvent event) {
    Log.d(TAG, String.format("processKeyEvent(key=%d)", event.getKeyCode()));

    // Filter out Enter keys if multi-line mode is disabled.
    if ((settings.mEditorInfo.inputType & EditorInfo.TYPE_TEXT_FLAG_MULTI_LINE) == 0 &&
        (event.getKeyCode() == KeyEvent.KEYCODE_ENTER ||
        event.getKeyCode() == KeyEvent.KEYCODE_NUMPAD_ENTER) && event.hasNoModifiers()) {
      this.performEditorAction(settings.mEditorInfo.actionId);
      return true;
    }

    Pair selection = this.getSelection();
    if (event == null) {
      return false;
    } else if (event.getAction() != 0) {
      return true;
    } else {
      // If no selection is set, move the selection to the end.
      // This is the case when first typing on keys when the selection is not set.
      // Note that for InputType.TYPE_CLASS_TEXT, this is not be needed because the
      // selection is set in setComposingText.
      if (selection.first == -1) {
        selection.first = this.mEditable.length();
        selection.second = this.mEditable.length();
      }

      if (selection.first != selection.second) {
        this.mEditable.delete(selection.first, selection.second);
      } else if (event.getKeyCode() == KeyEvent.KEYCODE_DEL && selection.first > 0) {
        this.mEditable.delete(selection.first - 1, selection.first);
        this.stateUpdated(false);
        return true;
      } else if (event.getKeyCode() == KeyEvent.KEYCODE_FORWARD_DEL
          && selection.first < this.mEditable.length() - 1) {
        this.mEditable.delete(selection.first, selection.first + 1);
        this.stateUpdated(false);
        return true;
      }

      int code = event.getKeyCode();
      if (!dontInsertChars.get(code)) {
        String charsToInsert = Character.toString((char) event.getUnicodeChar());
        this.mEditable.insert(
            selection.first, (CharSequence) charsToInsert);
        int length = this.mEditable.length();

        // Same logic as in setComposingText(): we must update composing region,
        // so make sure it points to a valid range.
        Pair composingRegion = this.getComposingRegion();
        if (composingRegion.first == -1) {
          composingRegion = this.getSelection();
          if (composingRegion.first == -1) {
            composingRegion = new Pair(0, 0);
          }
        }

        // IMM seems to cache the content of Editable, so we update it with restartInput
        // Also it caches selection and composing region, so let's notify it about updates.
        composingRegion.second = composingRegion.first + length;
        this.setComposingRegion(composingRegion.first, composingRegion.second);
        int new_cursor = selection.first + charsToInsert.length();
        setSelectionInternal(new_cursor, new_cursor);
        this.informIMM();
        this.restartInput();
        this.stateUpdated(false);
        return true;
      } else {
        return false;
      }
    }
  }

  private final void stateUpdated(boolean dismissed) {
    Pair selection = this.getSelection();
    Pair cr = this.getComposingRegion();
    State state = new State(
        this.mEditable.toString(), selection.first, selection.second, cr.first, cr.second);

    // Keep a reference to the listener to avoid a race condition when setting the listener.
    Listener listener = this.listener;

    // Only propagate the state change when the keyboard is set to active.
    // If we don't do this, 'back' events can be passed unnecessarily.
    if (listener != null && this.mSoftKeyboardActive) {
      listener.stateChanged(state, dismissed);
    }
  }

  /**
   * This function is called whenever software keyboard (IME) changes its visible dimensions.
   *
   * @param v main application View
   * @param insets insets of the software keyboard (IME)
   * @return this function should return original insets object unless it wants to modify insets.
   */
  public WindowInsetsCompat onApplyWindowInsets(View v, WindowInsetsCompat insets) {
    Log.d(TAG, "onApplyWindowInsets" + this.isSoftwareKeyboardVisible());

    if (listener != null) {
      listener.onImeInsetsChanged(insets.getInsets(WindowInsetsCompat.Type.ime()));
    }

    boolean visible = this.isSoftwareKeyboardVisible();
    if (visible == this.mSoftKeyboardActive) {
      return insets;
    }

    this.mSoftKeyboardActive = visible;
    if (!visible && VERSION.SDK_INT >= VERSION_CODES.O) {
      this.targetView.clearFocus();
    }

    if (listener != null) {
      listener.onSoftwareKeyboardVisibilityChanged(visible);
    }

    return insets;
  }

  /**
   * Get the current IME insets.
   *
   * @return The current IME insets
   */
  public Insets getImeInsets() {
    WindowInsetsCompat insets = ViewCompat.getRootWindowInsets(this.targetView);
    return insets.getInsets(WindowInsetsCompat.Type.ime());
  }

  /**
   * Returns true if software keyboard is visible, false otherwise.
   *
   * @return whether software IME is visible or not.
   */
  public boolean isSoftwareKeyboardVisible() {
    WindowInsetsCompat insets = ViewCompat.getRootWindowInsets(this.targetView);
    return insets.isVisible(WindowInsetsCompat.Type.ime());
  }

  /**
   * This is an event handler from InputConnection interface.
   * It's called when action button is triggered (typically this means Enter was pressed).
   *
   * @param action Action code, either one from EditorInfo.imeOptions or a custom one.
   * @return Returns true on success, false if the input connection is no longer valid.
   */
  @Override
  public boolean performEditorAction(int action) {
    if (listener != null) {
      listener.onEditorAction(action);
      return true;
    } else {
      return false;
    }
  }
}
