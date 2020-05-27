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

#include "LibAndroid.hpp"

#include "LibLoader.hpp"

namespace libandroid {
void *GetLib() { return LoadLibrary("libandroid.so"); }

// -----------------------------------------------------------------------------

FP_ACHOREOGRAPHER_GET_INSTANCE GetFP_AChoreographer_getInstance() {
  return reinterpret_cast<FP_ACHOREOGRAPHER_GET_INSTANCE>(
      LoadSymbol(GetLib(), "AChoreographer_getInstance"));
}

FP_AChoreographer_postFrameCallback GetFP_AChoreographer_postFrameCallback() {
  return reinterpret_cast<FP_AChoreographer_postFrameCallback>(
      LoadSymbol(GetLib(), "AChoreographer_postFrameCallback"));
}

// -----------------------------------------------------------------------------

FP_AHB_ALLOCATE GetFP_AHardwareBuffer_allocate() {
  return reinterpret_cast<FP_AHB_ALLOCATE>(
      LoadSymbol(GetLib(), "AHardwareBuffer_allocate"));
}

FP_AHB_RELEASE GetFP_AHardwareBuffer_release() {
  return reinterpret_cast<FP_AHB_RELEASE>(
      LoadSymbol(GetLib(), "AHardwareBuffer_release"));
}

FP_AHB_LOCK GetFP_AHardwareBuffer_lock() {
  return reinterpret_cast<FP_AHB_LOCK>(
      LoadSymbol(GetLib(), "AHardwareBuffer_lock"));
}

FP_AHB_UNLOCK GetFP_AHardwareBuffer_unlock() {
  return reinterpret_cast<FP_AHB_UNLOCK>(
      LoadSymbol(GetLib(), "AHardwareBuffer_unlock"));
}
}  // namespace libandroid
