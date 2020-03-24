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

#include <android/hardware_buffer.h>
#include <android/hardware_buffer_jni.h>

namespace libandroid {
void *GetLib();

typedef int (*PFN_AHB_ALLOCATE)(const AHardwareBuffer_Desc *,
                                AHardwareBuffer **);
typedef void (*PFN_AHB_RELEASE)(AHardwareBuffer *);
typedef int (*PFN_AHB_LOCK)(AHardwareBuffer *, uint64_t, int32_t, const ARect *,
                            void **);
typedef int (*PFN_AHB_UNLOCK)(AHardwareBuffer *, int32_t *);

// Returns function pointer to AHardwareBuffer_allocate()
PFN_AHB_ALLOCATE PfnAHardwareBuffer_Allocate();

// Returns function pointer to AHardwareBuffer_release()
PFN_AHB_RELEASE PfnAHardwareBuffer_Release();

// Returns function pointer to AHardwareBuffer_lock()
PFN_AHB_LOCK PfnAHardwareBuffer_Lock();

// Returns function pointer to AHardwareBuffer_unlock()
PFN_AHB_UNLOCK PfnAHardwareBuffer_Unlock();
}  // namespace libandroid
