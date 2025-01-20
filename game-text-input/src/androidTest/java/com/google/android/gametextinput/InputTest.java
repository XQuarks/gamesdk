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
 *
 *
 * These are tests for GameTextInput. This tests emulate both software and
 * hardware keyboards and can on a real Android devices or an emulator.
 *
 * Espresso doesn't properly print reasons of failures. If tests fail,
 * run this to see real and expected values:
 *
 * adb logcat | grep -A 5 -B 5 "Expected:"
 */
package com.google.androidgamesdk.gametextinput.test;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.action.ViewActions.*;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.ViewMatchers.*;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static androidx.test.espresso.matcher.ViewMatchers.withText;
import static org.junit.Assert.*;

import android.view.KeyEvent;
import android.view.View;
import androidx.test.espresso.UiController;
import androidx.test.espresso.ViewAction;
import androidx.test.ext.junit.rules.ActivityScenarioRule;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.filters.LargeTest;
import com.google.androidgamesdk.gametextinput.InputConnection;
import com.google.androidgamesdk.gametextinput.test.MainActivity;
import com.google.androidgamesdk.gametextinput.test.R;
import java.util.concurrent.TimeUnit;
import org.hamcrest.Matcher;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * Instrumented test, which will execute on an Android device.
 *
 * @see <a href="http://d.android.com/tools/testing">Testing documentation</a>
 */
@RunWith(AndroidJUnit4.class)
@LargeTest
public class InputTest {
  static private final String INITIAL_VALUE = "this is test text";

  @Rule
  public ActivityScenarioRule<MainActivity> activityScenarioRule =
      new ActivityScenarioRule<>(MainActivity.class);

  // Espresso doesn't support selecting text, so this action implements this ability.
  // Note that start = end means in Android this is just a text cursor.
  // For example, start = end = 3 means that there is no selection and the cursor
  // is at position 3.
  private ViewAction selectText(int start, int end) {
    return new ViewAction() {
      @Override
      public Matcher<View> getConstraints() {
        return isDisplayed();
      }

      @Override
      public String getDescription() {
        return "Select text";
      }

      @Override
      public void perform(UiController uiController, View view) {
        InputEnabledTextView inputView = (InputEnabledTextView) view;
        InputConnection ic = inputView.getInputConnection();

        ic.setSelection(start, end);
      }
    };
  }

  //////////////////////////////////////////////////////////////////////////
  // Below are tests. First group of tests is for simple text typing.
  //////////////////////////////////////////////////////////////////////////

  @Test
  public void canTypeCharOnSoftwareKeyboard() {
    onView(withId(R.id.displayed_text)).check(matches(withText(INITIAL_VALUE)));

    activityScenarioRule.getScenario().onActivity(activity -> activity.enableSoftKeyboard());

    onView(withId(R.id.input_enabled_text_view)).perform(typeText("s"), closeSoftKeyboard());

    onView(withId(R.id.displayed_text)).check(matches(withText("s")));
  }

  @Test
  public void canTypeTextOnSoftwareKeyboard() {
    onView(withId(R.id.displayed_text)).check(matches(withText(INITIAL_VALUE)));

    activityScenarioRule.getScenario().onActivity(activity -> activity.enableSoftKeyboard());

    onView(withId(R.id.input_enabled_text_view)).perform(typeText("abcdef"), closeSoftKeyboard());

    onView(withId(R.id.displayed_text)).check(matches(withText("abcdef")));
  }

  @Test
  public void canTypeCharOnHardwareKeyboard() {
    onView(withId(R.id.displayed_text)).check(matches(withText(INITIAL_VALUE)));

    activityScenarioRule.getScenario().onActivity(activity -> activity.enableSoftKeyboard());

    onView(withId(R.id.input_enabled_text_view))
        .perform(pressKey(KeyEvent.KEYCODE_1), closeSoftKeyboard());

    onView(withId(R.id.displayed_text)).check(matches(withText("1")));
  }

  @Test
  public void canTypeTextOnHardwareKeyboard() {
    onView(withId(R.id.displayed_text)).check(matches(withText(INITIAL_VALUE)));

    activityScenarioRule.getScenario().onActivity(activity -> activity.enableSoftKeyboard());

    onView(withId(R.id.input_enabled_text_view))
        .perform(pressKey(KeyEvent.KEYCODE_1), pressKey(KeyEvent.KEYCODE_A),
            pressKey(KeyEvent.KEYCODE_2), pressKey(KeyEvent.KEYCODE_Z),
            pressKey(KeyEvent.KEYCODE_9), pressKey(KeyEvent.KEYCODE_R), closeSoftKeyboard());

    onView(withId(R.id.displayed_text)).check(matches(withText("1a2z9r")));
  }

  //////////////////////////////////////////////////////////////////////////
  // Tests for Backspace and Delete buttons - single character.
  //////////////////////////////////////////////////////////////////////////

  @Test
  public void hardwareBackspaceAtTheEndWorks() {
    onView(withId(R.id.displayed_text)).check(matches(withText(INITIAL_VALUE)));

    activityScenarioRule.getScenario().onActivity(activity -> activity.enableSoftKeyboard());

    onView(withId(R.id.input_enabled_text_view))
        .perform(typeText("abcdef"), pressKey(KeyEvent.KEYCODE_DEL), closeSoftKeyboard());

    onView(withId(R.id.displayed_text)).check(matches(withText("abcde")));
  }

  @Test
  public void hardwareBackspaceAtTheStartDoesNothing() {
    onView(withId(R.id.displayed_text)).check(matches(withText(INITIAL_VALUE)));

    activityScenarioRule.getScenario().onActivity(activity -> activity.enableSoftKeyboard());

    onView(withId(R.id.input_enabled_text_view))
        .perform(
            typeText("abc"), selectText(0, 0), pressKey(KeyEvent.KEYCODE_DEL), closeSoftKeyboard());

    onView(withId(R.id.displayed_text)).check(matches(withText("abc")));
  }

  @Test
  public void hardwareDeleteAtTheEndDoesNothing() {
    onView(withId(R.id.displayed_text)).check(matches(withText(INITIAL_VALUE)));

    activityScenarioRule.getScenario().onActivity(activity -> activity.enableSoftKeyboard());

    onView(withId(R.id.input_enabled_text_view))
        .perform(typeText("abc"), selectText(3, 3), pressKey(KeyEvent.KEYCODE_FORWARD_DEL),
            closeSoftKeyboard());

    onView(withId(R.id.displayed_text)).check(matches(withText("abc")));
  }

  @Test
  public void hardwareBackspaceInTheMiddleWorks() {
    onView(withId(R.id.displayed_text)).check(matches(withText(INITIAL_VALUE)));

    activityScenarioRule.getScenario().onActivity(activity -> activity.enableSoftKeyboard());

    onView(withId(R.id.input_enabled_text_view))
        .perform(typeText("abcdef"), selectText(1, 1), pressKey(KeyEvent.KEYCODE_DEL));

    onView(withId(R.id.displayed_text)).check(matches(withText("bcdef")));

    onView(withId(R.id.input_enabled_text_view))
        .perform(selectText(3, 3), pressKey(KeyEvent.KEYCODE_DEL), pressKey(KeyEvent.KEYCODE_DEL));

    onView(withId(R.id.displayed_text)).check(matches(withText("bef")));

    onView(withId(R.id.input_enabled_text_view)).perform(typeText("xyz"));

    onView(withId(R.id.displayed_text)).check(matches(withText("bxyzef")));
  }

  @Test
  public void multipleHardwareDeletesWork() {
    onView(withId(R.id.displayed_text)).check(matches(withText(INITIAL_VALUE)));

    activityScenarioRule.getScenario().onActivity(activity -> activity.enableSoftKeyboard());

    onView(withId(R.id.input_enabled_text_view))
        .perform(typeText("abcde"), selectText(3, 3), pressKey(KeyEvent.KEYCODE_FORWARD_DEL),
            pressKey(KeyEvent.KEYCODE_FORWARD_DEL));

    onView(withId(R.id.displayed_text)).check(matches(withText("abc")));
  }

  @Test
  public void hardwareDeleteInTheMiddleWorks() {
    onView(withId(R.id.displayed_text)).check(matches(withText(INITIAL_VALUE)));

    activityScenarioRule.getScenario().onActivity(activity -> activity.enableSoftKeyboard());

    onView(withId(R.id.input_enabled_text_view))
        .perform(typeText("abcdef"), selectText(1, 1), pressKey(KeyEvent.KEYCODE_FORWARD_DEL));

    onView(withId(R.id.displayed_text)).check(matches(withText("acdef")));

    onView(withId(R.id.input_enabled_text_view))
        .perform(selectText(3, 3), pressKey(KeyEvent.KEYCODE_FORWARD_DEL),
            pressKey(KeyEvent.KEYCODE_FORWARD_DEL));

    onView(withId(R.id.displayed_text)).check(matches(withText("acd")));

    onView(withId(R.id.input_enabled_text_view)).perform(selectText(1, 1), typeText("xyz"));

    onView(withId(R.id.displayed_text)).check(matches(withText("axyzcd")));
  }

  //////////////////////////////////////////////////////////////////////////
  // Tests for deletion of selected text.
  //////////////////////////////////////////////////////////////////////////

  @Test
  public void selectAllWithOverwriteWorks() {
    onView(withId(R.id.displayed_text)).check(matches(withText(INITIAL_VALUE)));

    activityScenarioRule.getScenario().onActivity(activity -> activity.enableSoftKeyboard());

    onView(withId(R.id.input_enabled_text_view))
        .perform(typeText("abcdef"), selectText(0, 6), typeText("xyz"), closeSoftKeyboard());

    onView(withId(R.id.displayed_text)).check(matches(withText("xyz")));
  }

  @Test
  public void selectAllWithHardwareBackspaceWorks() {
    onView(withId(R.id.displayed_text)).check(matches(withText(INITIAL_VALUE)));

    activityScenarioRule.getScenario().onActivity(activity -> activity.enableSoftKeyboard());

    onView(withId(R.id.input_enabled_text_view))
        .perform(typeText("abcdef"), selectText(0, 6), pressKey(KeyEvent.KEYCODE_DEL),
            closeSoftKeyboard());

    onView(withId(R.id.displayed_text)).check(matches(withText("")));
  }

  @Test
  public void selectAllWithHardwareDeleteWorks() {
    onView(withId(R.id.displayed_text)).check(matches(withText(INITIAL_VALUE)));

    activityScenarioRule.getScenario().onActivity(activity -> activity.enableSoftKeyboard());

    onView(withId(R.id.input_enabled_text_view))
        .perform(typeText("abcdef"), selectText(0, 6), pressKey(KeyEvent.KEYCODE_FORWARD_DEL),
            closeSoftKeyboard());

    onView(withId(R.id.displayed_text)).check(matches(withText("")));
  }

  @Test
  public void hardwareBackspaceWorksWithAnySelection() {
    onView(withId(R.id.displayed_text)).check(matches(withText(INITIAL_VALUE)));

    activityScenarioRule.getScenario().onActivity(activity -> activity.enableSoftKeyboard());

    onView(withId(R.id.input_enabled_text_view))
        .perform(typeText("abcdefgh"), selectText(6, 8), pressKey(KeyEvent.KEYCODE_DEL));

    onView(withId(R.id.displayed_text)).check(matches(withText("abcdef")));

    onView(withId(R.id.input_enabled_text_view))
        .perform(selectText(0, 2), pressKey(KeyEvent.KEYCODE_DEL), typeText("mn"));

    onView(withId(R.id.displayed_text)).check(matches(withText("mncdef")));

    onView(withId(R.id.input_enabled_text_view))
        .perform(selectText(1, 4), pressKey(KeyEvent.KEYCODE_DEL), typeText("xyz"));

    onView(withId(R.id.displayed_text)).check(matches(withText("mxyzef")));
  }

  @Test
  public void hardwareDeleteWorksWithAnySelection() {
    onView(withId(R.id.displayed_text)).check(matches(withText(INITIAL_VALUE)));

    activityScenarioRule.getScenario().onActivity(activity -> activity.enableSoftKeyboard());

    onView(withId(R.id.input_enabled_text_view))
        .perform(typeText("abcdefgh"), selectText(6, 8), pressKey(KeyEvent.KEYCODE_FORWARD_DEL));

    onView(withId(R.id.displayed_text)).check(matches(withText("abcdef")));

    onView(withId(R.id.input_enabled_text_view))
        .perform(selectText(0, 2), pressKey(KeyEvent.KEYCODE_FORWARD_DEL), typeText("mn"));

    onView(withId(R.id.displayed_text)).check(matches(withText("mncdef")));

    onView(withId(R.id.input_enabled_text_view))
        .perform(selectText(1, 4), pressKey(KeyEvent.KEYCODE_FORWARD_DEL), typeText("xyz"));

    onView(withId(R.id.displayed_text)).check(matches(withText("mxyzef")));
  }

  @Test
  public void pressingLeftMovesCursorLeft() {
    onView(withId(R.id.displayed_text)).check(matches(withText(INITIAL_VALUE)));

    activityScenarioRule.getScenario().onActivity(activity -> activity.enableSoftKeyboard());

    onView(withId(R.id.input_enabled_text_view))
        .perform(typeText("abcdefgh"), selectText(6, 6), pressKey(KeyEvent.KEYCODE_DPAD_LEFT),
            pressKey(KeyEvent.KEYCODE_X));

    onView(withId(R.id.displayed_text)).check(matches(withText("abcdexfgh")));
  }

  @Test
  public void pressingRightMovesCursorRight() {
    onView(withId(R.id.displayed_text)).check(matches(withText(INITIAL_VALUE)));

    activityScenarioRule.getScenario().onActivity(activity -> activity.enableSoftKeyboard());

    onView(withId(R.id.input_enabled_text_view))
        .perform(typeText("abcdefgh"), selectText(6, 6), pressKey(KeyEvent.KEYCODE_DPAD_RIGHT),
            pressKey(KeyEvent.KEYCODE_X));

    onView(withId(R.id.displayed_text)).check(matches(withText("abcdefgxh")));
  }
}
