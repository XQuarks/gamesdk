/*
 * Copyright 2020 The Android Open Source Project
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

#pragma once

// This function is exported by the test library and should be called to run the tests if you have a
// Java env.
// messages is filled with a summary of the tests run, including failure messages.
extern "C" int shared_main(int argc, char * argv[], JNIEnv* env, jobject context,
                           std::string& messages);

extern "C" size_t test_summary(char* result, size_t len);
