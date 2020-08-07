/*
 * Copyright 2018 The Android Open Source Project
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

#include "core/backend.h"
#include "core/common.h"
#include "core/id_provider.h"
#include "core/meminfo_provider.h"
#include "core/request_info.h"
#include "core/settings.h"
#include "core/time_provider.h"
#include "core/tuningfork_extra.h"
#include "proto/protobuf_util.h"

// These functions are implemented in tuningfork.cpp.
// They are mostly the same as the C interface, but take C++ types.

namespace tuningfork {

// If no request_info is passed, the info for this device and game are used.
// If no backend is passed, the default backend, which uploads to the google
// http endpoint is used. If no timeProvider is passed,
// std::chrono::steady_clock is used. If no env is passed, there can be no
// upload or download.
TuningFork_ErrorCode Init(const Settings& settings,
                          const RequestInfo* request_info = nullptr,
                          IBackend* backend = nullptr,
                          ITimeProvider* time_provider = nullptr,
                          IMemInfoProvider* meminfo_provider = nullptr);

// Blocking call to get fidelity parameters from the server.
// Returns true if parameters could be downloaded within the timeout, false
// otherwise. Note that once fidelity parameters are downloaded, any timing
// information is recorded
//  as being associated with those parameters.
// If you subsequently call GetFidelityParameters, any data that is already
// collected will be submitted to the backend.
TuningFork_ErrorCode GetFidelityParameters(
    const ProtobufSerialization& default_params, ProtobufSerialization& params,
    uint32_t timeout_ms);

// Protobuf serialization of the current annotation
TuningFork_ErrorCode SetCurrentAnnotation(
    const ProtobufSerialization& annotation);

// Record a frame tick that will be associated with the instrumentation key and
// the current
//   annotation
TuningFork_ErrorCode FrameTick(InstrumentationKey id);

// Record a frame tick using an external time, rather than system time
TuningFork_ErrorCode FrameDeltaTimeNanos(InstrumentationKey id, Duration dt);

// Start a trace segment
TuningFork_ErrorCode StartTrace(InstrumentationKey key, TraceHandle& handle);

// Record a trace with the key and annotation set using startTrace
TuningFork_ErrorCode EndTrace(TraceHandle h);

// Set a callback to be called on a separate thread every time TuningFork
// performs an upload.
TuningFork_ErrorCode SetUploadCallback(TuningFork_UploadCallback cbk);

// Force upload of the current histograms.
TuningFork_ErrorCode Flush();

// Clean up all memory owned by Tuning Fork and kill any threads.
TuningFork_ErrorCode Destroy();

// Get the current settings (TF must have been initialized or nullptr is
// returned).
const Settings* GetSettings();

// Set the currently active fidelity parameters.
TuningFork_ErrorCode SetFidelityParameters(const ProtobufSerialization& params);

// Enable or disable memory telemetry recording.
TuningFork_ErrorCode EnableMemoryRecording(bool enable);

// Record a loading time event
TuningFork_ErrorCode RecordLoadingTime(Duration duration,
                                       const LoadingTimeMetadata& d);

TuningFork_ErrorCode ReportLifecycleEvent(TuningFork_LifecycleState state);

}  // namespace tuningfork
