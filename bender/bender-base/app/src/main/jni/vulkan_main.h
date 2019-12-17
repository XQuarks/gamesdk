// Copyright 2016 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#ifndef BENDER_BASE_VULKAN_MAIN_H__
#define BENDER_BASE_VULKAN_MAIN_H__

#include "trace.h"
#include "input.h"

#include <android_native_app_glue.h>

// Initialize vulkan device context
// after return, vulkan is ready to draw
bool InitVulkan(android_app *app);

void StartVulkan(android_app *app);

// delete vulkan device context when application goes away
void DeleteVulkan(void);

// Check if vulkan is ready to draw
bool IsVulkanReady(void);

// Ask Vulkan to Render a frame
bool VulkanDrawFrame(input::Data *input_data);

void ResizeCallback(ANativeActivity *activity, ANativeWindow *window);

void OnOrientationChange();

#endif // BENDER_BASE_VULKAN_MAIN_H__


