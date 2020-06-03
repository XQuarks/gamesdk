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

#include "tuningfork/tuningfork.h"
#include "tuningfork/tuningfork_extra.h"
#include "protobuf_util.h"

#include <stdint.h>
#include <string>
#include <chrono>
#include <vector>

class AAsset;

namespace tuningfork {

// The instrumentation key identifies a tick point within a frame or a trace segment
typedef uint16_t InstrumentationKey;
typedef uint64_t TraceHandle;
typedef std::chrono::steady_clock::time_point TimePoint;
typedef std::chrono::steady_clock::duration Duration;
typedef std::chrono::system_clock::time_point SystemTimePoint;
typedef std::chrono::system_clock::duration SystemDuration;

struct TimeInterval {
    std::chrono::system_clock::time_point start, end;
};

struct TFHistogram {
    int32_t instrument_key;
    float bucket_min;
    float bucket_max;
    int32_t n_buckets;
};

struct Settings {
    struct AggregationStrategy {
        enum class Submission {
            TICK_BASED,
            TIME_BASED
        };
        Submission method;
        uint32_t intervalms_or_count;
        uint32_t max_instrumentation_keys;
        std::vector<uint32_t> annotation_enum_size;
    };
    TuningFork_Settings c_settings;
    AggregationStrategy aggregation_strategy;
    std::vector<TFHistogram> histograms;
    std::string base_uri;
    std::string api_key;
    std::string default_fidelity_parameters_filename;
    uint32_t initial_request_timeout_ms;
    uint32_t ultimate_request_timeout_ms;
    int32_t loading_annotation_index;
    int32_t level_annotation_index;
    std::string EndpointUri() const {
        std::string uri;
        if (c_settings.endpoint_uri_override==nullptr)
            uri = base_uri;
        else
            uri = c_settings.endpoint_uri_override;
        if (!uri.empty() && uri.back()!='/')
            uri += '/';
        return uri;
    }
};

// Extra information that is uploaded with the proto.
struct ExtraUploadInfo {
    std::string experiment_id;
    std::string session_id;
    uint64_t total_memory_bytes;
    uint32_t gl_es_version;
    std::string build_fingerprint;
    std::string build_version_sdk;
    std::vector<uint64_t> cpu_max_freq_hz;
    std::string apk_package_name;
    uint32_t apk_version_code;
    uint32_t tuningfork_version;
};

class IdProvider {
  public:
    virtual ~IdProvider() {}
    virtual uint64_t DecodeAnnotationSerialization(const ProtobufSerialization& ser,
                                                   bool* loading = nullptr) const = 0;
    virtual TuningFork_ErrorCode MakeCompoundId(InstrumentationKey k,
                                       uint64_t annotation_id,
                                       uint64_t& id) = 0;
};

class Backend {
public:
    virtual ~Backend() {};
    virtual TuningFork_ErrorCode Process(
        const std::string& tuningfork_log_event) = 0;
};

class Request {
  protected:
    const ExtraUploadInfo& info_;
    std::string base_url_;
    std::string api_key_;
    Duration timeout_;
  public:
    Request(const ExtraUploadInfo& info, std::string base_url, std::string api_key,
            Duration timeout) : info_(info), base_url_(base_url), api_key_(api_key),
                                  timeout_(timeout) {}
    virtual ~Request() {}
    std::string GetURL(std::string rpcname) const;
    const ExtraUploadInfo& Info() const { return info_; }
    virtual TuningFork_ErrorCode Send(
        const std::string& rpc_name,
        const std::string& request,
        int& response_code,
        std::string& response_body);
};

class ParamsLoader {
public:
    virtual ~ParamsLoader() {};
    virtual TuningFork_ErrorCode GetFidelityParams(
        Request& request,
        const ProtobufSerialization* training_mode_fps,
        ProtobufSerialization& fidelity_params,
        std::string& experiment_id);
};

// TODO(willosborn): remove this
class ProtoPrint {
public:
    virtual ~ProtoPrint() {};
    virtual void Print(const ProtobufSerialization& tuningfork_log_event);
};

// You can provide your own time source rather than steady_clock by inheriting this and passing
//   it to init.
class ITimeProvider {
public:
    virtual std::chrono::steady_clock::time_point Now() = 0;
    virtual std::chrono::system_clock::time_point SystemNow() = 0;
};

// Provider of system memory information.
class IMemInfoProvider {
  public:
    virtual uint64_t GetNativeHeapAllocatedSize() = 0;
    virtual void SetEnabled(bool enable) = 0;
    virtual bool GetEnabled() const = 0;
    virtual void SetDeviceMemoryBytes(uint64_t bytesize) = 0;
    virtual uint64_t GetDeviceMemoryBytes() const = 0;
};

// If no backend is passed, the default backend, which uploads to the google endpoint is used.
// If no timeProvider is passed, std::chrono::steady_clock is used.
// If no env is passed, there can be no upload or download.
TuningFork_ErrorCode Init(const Settings& settings,
                 const ExtraUploadInfo* extra_info = nullptr,
                 Backend* backend = 0, ParamsLoader* loader = nullptr,
                 ITimeProvider* time_provider = nullptr,
                 IMemInfoProvider* meminfo_provider = nullptr);

// Use save_dir to initialize the persister if it's not already set
void CheckSettings(Settings& c_settings, const std::string& save_dir);

// Blocking call to get fidelity parameters from the server.
// Returns true if parameters could be downloaded within the timeout, false otherwise.
// Note that once fidelity parameters are downloaded, any timing information is recorded
//  as being associated with those parameters.
// If you subsequently call GetFidelityParameters, any data that is already collected will be
// submitted to the backend.
TuningFork_ErrorCode GetFidelityParameters(
    const ProtobufSerialization& default_params,
    ProtobufSerialization& params,
    uint32_t timeout_ms);

// Protobuf serialization of the current annotation
TuningFork_ErrorCode SetCurrentAnnotation(
    const ProtobufSerialization& annotation);

// Record a frame tick that will be associated with the instrumentation key and the current
//   annotation
TuningFork_ErrorCode FrameTick(InstrumentationKey id);

// Record a frame tick using an external time, rather than system time
TuningFork_ErrorCode FrameDeltaTimeNanos(InstrumentationKey id, Duration dt);

// Start a trace segment
TuningFork_ErrorCode StartTrace(InstrumentationKey key, TraceHandle& handle);

// Record a trace with the key and annotation set using startTrace
TuningFork_ErrorCode EndTrace(TraceHandle h);

TuningFork_ErrorCode SetUploadCallback(TuningFork_UploadCallback cbk);

TuningFork_ErrorCode Flush();

TuningFork_ErrorCode Destroy();

// The default histogram that is used if the user doesn't specify one in Settings
TFHistogram DefaultHistogram(InstrumentationKey ikey);

// Load default fidelity params from either the saved file or the file in
//  settings.default_fidelity_parameters_filename, then start the download thread.
TuningFork_ErrorCode GetDefaultsFromAPKAndDownloadFPs(
    const Settings& settings);

TuningFork_ErrorCode KillDownloadThreads();

// Load settings from assets/tuningfork/tuningfork_settings.bin.
// Ownership of @p settings is passed to the caller: call
//  TuningFork_Settings_Free to deallocate data stored in the struct.
// Returns TFERROR_OK and fills 'settings' if the file could be loaded.
// Returns TFERROR_NO_SETTINGS if the file was not found.
TuningFork_ErrorCode FindSettingsInApk(Settings* settings);

// Get the current settings (TF must have been initialized)
const Settings* GetSettings();

TuningFork_ErrorCode SetFidelityParameters(
    const ProtobufSerialization& params);

// Perform a blocking call to upload debug info to a server.
TuningFork_ErrorCode UploadDebugInfo(Request& request);

TuningFork_ErrorCode FindFidelityParamsInApk(const std::string& filename,
                                    ProtobufSerialization& fp);

TuningFork_ErrorCode EnableMemoryRecording(bool enable);

} // namespace tuningfork
