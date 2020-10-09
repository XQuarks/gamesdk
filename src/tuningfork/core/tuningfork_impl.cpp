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

#include "tuningfork_impl.h"

#include <chrono>
#include <cinttypes>
#include <map>
#include <sstream>
#include <vector>

#define LOG_TAG "TuningFork"
#include "Log.h"
#include "activity_lifecycle_state.h"
#include "annotation_util.h"
#include "histogram.h"
#include "http_backend/http_backend.h"
#include "memory_telemetry.h"
#include "metric.h"
#include "tuningfork_utils.h"

namespace tuningfork {

TuningForkImpl::TuningForkImpl(const Settings &settings, IBackend *backend,
                               ITimeProvider *time_provider,
                               IMemInfoProvider *meminfo_provider,
                               bool first_run)
    : settings_(settings),
      trace_(gamesdk::Trace::create()),
      backend_(backend),
      upload_thread_(backend, this),
      current_annotation_id_(MetricId::FrameTime(0, 0)),
      time_provider_(time_provider),
      meminfo_provider_(meminfo_provider),
      ikeys_(settings.aggregation_strategy.max_instrumentation_keys),
      next_ikey_(0),
      loading_start_(TimePoint::min()),
      before_first_tick_(true),
      app_first_run_(first_run) {
    if (backend == nullptr) {
        default_backend_ = std::make_unique<HttpBackend>();
        TuningFork_ErrorCode err = default_backend_->Init(settings);
        if (err == TUNINGFORK_ERROR_OK) {
            ALOGI("TuningFork.GoogleEndpoint: OK");
            backend_ = default_backend_.get();
        } else {
            ALOGE("TuningFork.GoogleEndpoint: FAILED");
            initialization_error_code_ = err;
            return;
        }
    }

    if (time_provider_ == nullptr) {
        default_time_provider_ = std::make_unique<ChronoTimeProvider>();
        time_provider_ = default_time_provider_.get();
    }

    if (meminfo_provider_ == nullptr) {
        default_meminfo_provider_ = std::make_unique<DefaultMemInfoProvider>();
        meminfo_provider_ = default_meminfo_provider_.get();
        meminfo_provider_->SetDeviceMemoryBytes(
            RequestInfo::CachedValue().total_memory_bytes);
    }

    auto start_time = time_provider_->TimeSinceProcessStart();

    ALOGI(
        "TuningFork Settings:\n  method: %d\n  interval: %d\n  n_ikeys: %d\n  "
        "n_annotations: %zu"
        "\n  n_histograms: %zu\n  base_uri: %s\n  api_key: %s\n  fp filename: "
        "%s\n  itimeout: %d"
        "\n  utimeout: %d",
        settings.aggregation_strategy.method,
        settings.aggregation_strategy.intervalms_or_count,
        settings.aggregation_strategy.max_instrumentation_keys,
        settings.aggregation_strategy.annotation_enum_size.size(),
        settings.histograms.size(), settings.base_uri.c_str(),
        settings.api_key.c_str(),
        settings.default_fidelity_parameters_filename.c_str(),
        settings.initial_request_timeout_ms,
        settings.ultimate_request_timeout_ms);

    last_submit_time_ = time_provider_->Now();

    InitHistogramSettings();
    InitAnnotationRadixes();
    InitTrainingModeParams();

    size_t max_num_frametime_metrics = 0;
    int max_ikeys = settings.aggregation_strategy.max_instrumentation_keys;

    if (annotation_radix_mult_.size() == 0 || max_ikeys == 0)
        ALOGE(
            "Neither max_annotations nor max_instrumentation_keys can be zero");
    else
        max_num_frametime_metrics = max_ikeys * annotation_radix_mult_.back();
    for (int i = 0; i < 2; ++i) {
        sessions_[i] = std::make_unique<Session>();
        CreateSessionFrameHistograms(*sessions_[i], max_num_frametime_metrics,
                                     max_ikeys, settings_.histograms,
                                     settings.c_settings.max_num_metrics);
        MemoryTelemetry::CreateMemoryHistograms(
            *sessions_[i], meminfo_provider_,
            settings.c_settings.max_num_metrics.memory);
    }
    current_session_ = sessions_[0].get();
    live_traces_.resize(max_num_frametime_metrics);
    for (auto &t : live_traces_) t = TimePoint::min();
    auto crash_callback = [this]() -> bool {
        std::stringstream ss;
        ss << std::this_thread::get_id();
        TuningFork_ErrorCode ret = this->Flush();
        ALOGI("Crash flush result : %d", ret);
        return true;
    };

    crash_handler_.Init(crash_callback);

    // Check if there are any files waiting to be uploaded
    // + merge any histograms that are persisted.
    upload_thread_.InitialChecks(*current_session_, *this,
                                 settings_.c_settings.persistent_cache);

    InitAsyncTelemetry();

    // Record the time before we were initialized.
    if (RecordLoadingTime(
            start_time,
            LoadingTimeMetadata{
                app_first_run_ ? LoadingTimeMetadata::LoadingState::FIRST_RUN
                               : LoadingTimeMetadata::LoadingState::COLD_START,
                LoadingTimeMetadata::LoadingSource::PRE_ACTIVITY},
            {}, true /* relativeToStart */) != TUNINGFORK_ERROR_OK) {
        ALOGW(
            "Warning: could not record pre-activity loading time. Increase the "
            "maximum number of loading time metrics?");
    }

    ALOGI("TuningFork initialized");
}

TuningForkImpl::~TuningForkImpl() {
    // Stop the threads before we delete Tuning Fork internals
    if (backend_) backend_->Stop();
    upload_thread_.Stop();
    if (async_telemetry_) async_telemetry_->Stop();
}

void TuningForkImpl::CreateSessionFrameHistograms(
    Session &session, size_t size, int max_num_instrumentation_keys,
    const std::vector<Settings::Histogram> &histogram_settings,
    const TuningFork_MetricLimits &limits) {
    InstrumentationKey ikey = 0;
    int num_loading_created = 0;
    int num_frametime_created = 0;
    for (int i = num_frametime_created; i < limits.frame_time; ++i) {
        auto &h =
            histogram_settings[ikey < histogram_settings.size() ? ikey : 0];
        session.CreateFrameTimeHistogram(MetricId::FrameTime(0, ikey), h);
        ++ikey;
        if (ikey >= max_num_instrumentation_keys) ikey = 0;
    }
    // Add extra loading time metrics
    for (int i = num_loading_created; i < limits.loading_time; ++i) {
        session.CreateLoadingTimeSeries(MetricId::LoadingTime(0, 0));
    }
}

// Return the set annotation id or -1 if it could not be set
MetricId TuningForkImpl::SetCurrentAnnotation(
    const ProtobufSerialization &annotation) {
    current_annotation_ = annotation;
    AnnotationId id;
    SerializedAnnotationToAnnotationId(annotation, id);
    if (id == annotation_util::kAnnotationError) {
        ALOGW("Error setting annotation of size %zu", annotation.size());
        current_annotation_id_ = MetricId::FrameTime(0, 0);
        return MetricId{annotation_util::kAnnotationError};
    } else {
        ALOGV("Set annotation id to %" PRIu32, id);
        current_annotation_id_ = MetricId::FrameTime(id, 0);
        return current_annotation_id_;
    }
}

TuningFork_ErrorCode TuningForkImpl::SerializedAnnotationToAnnotationId(
    const tuningfork::SerializedAnnotation &ser, tuningfork::AnnotationId &id) {
    return annotation_map_.GetOrInsert(ser, id);
}

TuningFork_ErrorCode TuningForkImpl::MakeCompoundId(InstrumentationKey key,
                                                    AnnotationId annotation_id,
                                                    MetricId &id) {
    int key_index;
    auto err = GetOrCreateInstrumentKeyIndex(key, key_index);
    if (err != TUNINGFORK_ERROR_OK) return err;
    id = MetricId::FrameTime(annotation_id, key_index);
    return TUNINGFORK_ERROR_OK;
}

TuningFork_ErrorCode TuningForkImpl::GetFidelityParameters(
    const ProtobufSerialization &default_params,
    ProtobufSerialization &params_ser, uint32_t timeout_ms) {
    std::string experiment_id;
    if (settings_.EndpointUri().empty()) {
        ALOGW("The base URI in Tuning Fork TuningFork_Settings is invalid");
        return TUNINGFORK_ERROR_BAD_PARAMETER;
    }
    if (settings_.api_key.empty()) {
        ALOGE("The API key in Tuning Fork TuningFork_Settings is invalid");
        return TUNINGFORK_ERROR_BAD_PARAMETER;
    }
    Duration timeout =
        (timeout_ms <= 0)
            ? std::chrono::milliseconds(settings_.initial_request_timeout_ms)
            : std::chrono::milliseconds(timeout_ms);
    HttpRequest web_request(settings_.EndpointUri(), settings_.api_key,
                            timeout);
    auto result = backend_->GenerateTuningParameters(
        web_request, training_mode_params_.get(), params_ser, experiment_id);
    if (result == TUNINGFORK_ERROR_OK) {
        RequestInfo::CachedValue().current_fidelity_parameters = params_ser;
    } else if (training_mode_params_.get()) {
        RequestInfo::CachedValue().current_fidelity_parameters =
            *training_mode_params_;
    }
    RequestInfo::CachedValue().experiment_id = experiment_id;
    if (Debugging() && jni::IsValid()) {
        backend_->UploadDebugInfo(web_request);
    }
    return result;
}
TuningFork_ErrorCode TuningForkImpl::GetOrCreateInstrumentKeyIndex(
    InstrumentationKey key, int &index) {
    int nkeys = next_ikey_;
    for (int i = 0; i < nkeys; ++i) {
        if (ikeys_[i] == key) {
            index = i;
            return TUNINGFORK_ERROR_OK;
        }
    }
    // Another thread could have incremented next_ikey while we were checking,
    // but we mandate that different threads not use the same key, so we are OK
    // adding our key, if we can.
    int next = next_ikey_++;
    if (next < ikeys_.size()) {
        ikeys_[next] = key;
        index = next;
        return TUNINGFORK_ERROR_OK;
    } else {
        next_ikey_--;
    }
    return TUNINGFORK_ERROR_INVALID_INSTRUMENT_KEY;
}
TuningFork_ErrorCode TuningForkImpl::StartTrace(InstrumentationKey key,
                                                TraceHandle &handle) {
    if (LoadingNextScene())
        return TUNINGFORK_ERROR_OK;  // No recording when loading

    MetricId id{0};
    auto err =
        MakeCompoundId(key, current_annotation_id_.detail.annotation, id);
    if (err != TUNINGFORK_ERROR_OK) return err;
    handle = id.detail.annotation *
                 settings_.aggregation_strategy.max_instrumentation_keys +
             id.detail.frame_time.ikey;
    trace_->beginSection("TFTrace");
    if (handle < live_traces_.size()) {
        live_traces_[handle] = time_provider_->Now();
        return TUNINGFORK_ERROR_OK;
    } else {
        return TUNINGFORK_ERROR_INVALID_ANNOTATION;
    }
}

TuningFork_ErrorCode TuningForkImpl::EndTrace(TraceHandle h) {
    if (LoadingNextScene())
        return TUNINGFORK_ERROR_OK;  // No recording when loading
    if (h >= live_traces_.size()) return TUNINGFORK_ERROR_INVALID_TRACE_HANDLE;
    auto i = live_traces_[h];
    if (i != TimePoint::min()) {
        trace_->endSection();
        auto err = TraceNanos(MetricId{h}, time_provider_->Now() - i, nullptr);
        live_traces_[h] = TimePoint::min();
        return err;
    } else {
        return TUNINGFORK_ERROR_INVALID_TRACE_HANDLE;
    }
}

TuningFork_ErrorCode TuningForkImpl::FrameTick(InstrumentationKey key) {
    if (LoadingNextScene())
        return TUNINGFORK_ERROR_OK;  // No recording when loading
    MetricId id{0};
    auto err =
        MakeCompoundId(key, current_annotation_id_.detail.annotation, id);
    if (err != TUNINGFORK_ERROR_OK) return err;
    trace_->beginSection("TFTick");
    current_session_->Ping(time_provider_->SystemNow());
    auto t = time_provider_->Now();
    MetricData *p;
    err = TickNanos(id, t, &p);
    if (err != TUNINGFORK_ERROR_OK) return err;
    if (p) CheckForSubmit(t, p);
    trace_->endSection();
    return TUNINGFORK_ERROR_OK;
}

TuningFork_ErrorCode TuningForkImpl::FrameDeltaTimeNanos(InstrumentationKey key,
                                                         Duration dt) {
    if (LoadingNextScene())
        return TUNINGFORK_ERROR_OK;  // No recording when loading
    MetricId id{0};
    auto err =
        MakeCompoundId(key, current_annotation_id_.detail.annotation, id);
    if (err != TUNINGFORK_ERROR_OK) return err;
    MetricData *p;
    err = TraceNanos(id, dt, &p);
    if (err != TUNINGFORK_ERROR_OK) return err;
    if (p) CheckForSubmit(time_provider_->Now(), p);
    return TUNINGFORK_ERROR_OK;
}

TuningFork_ErrorCode TuningForkImpl::TickNanos(MetricId compound_id,
                                               TimePoint t, MetricData **pp) {
    if (before_first_tick_) {
        before_first_tick_ = false;
        // Record the time to the first tick.
        if (RecordLoadingTime(
                time_provider_->TimeSinceProcessStart(),
                LoadingTimeMetadata{
                    app_first_run_
                        ? LoadingTimeMetadata::LoadingState::FIRST_RUN
                        : LoadingTimeMetadata::LoadingState::COLD_START,
                    LoadingTimeMetadata::LoadingSource::
                        FIRST_TOUCH_TO_FIRST_FRAME},
                {}, true /* relativeToStart */) != TUNINGFORK_ERROR_OK) {
            ALOGW(
                "Warning: could not record first frame loading time. Increase "
                "the maximum number of loading time metrics?");
        }
    }

    // Don't record while we have any loading events live
    if (live_loading_events_.size() > 0) return TUNINGFORK_ERROR_OK;

    // Find the appropriate histogram and add this time
    auto p = current_session_->GetData<FrameTimeMetricData>(compound_id);
    if (p) {
        p->Tick(t);
        if (pp != nullptr) *pp = p;
        return TUNINGFORK_ERROR_OK;
    } else {
        return TUNINGFORK_ERROR_NO_MORE_SPACE_FOR_FRAME_TIME_DATA;
    }
}

TuningFork_ErrorCode TuningForkImpl::TraceNanos(MetricId compound_id,
                                                Duration dt, MetricData **pp) {
    // Don't record while we have any loading events live
    if (live_loading_events_.size() > 0) return TUNINGFORK_ERROR_OK;

    // Find the appropriate histogram and add this time
    auto h = current_session_->GetData<FrameTimeMetricData>(compound_id);
    if (h) {
        h->Record(dt);
        if (pp != nullptr) *pp = h;
        return TUNINGFORK_ERROR_OK;
    } else {
        return TUNINGFORK_ERROR_NO_MORE_SPACE_FOR_FRAME_TIME_DATA;
    }
}

void TuningForkImpl::SetUploadCallback(TuningFork_UploadCallback cbk) {
    upload_thread_.SetUploadCallback(cbk);
}

bool TuningForkImpl::ShouldSubmit(TimePoint t, MetricData *histogram) {
    auto method = settings_.aggregation_strategy.method;
    auto count = settings_.aggregation_strategy.intervalms_or_count;
    switch (settings_.aggregation_strategy.method) {
        case Settings::AggregationStrategy::Submission::TIME_BASED:
            return (t - last_submit_time_) >= std::chrono::milliseconds(count);
        case Settings::AggregationStrategy::Submission::TICK_BASED:
            if (histogram) return histogram->Count() >= count;
    }
    return false;
}

TuningFork_ErrorCode TuningForkImpl::CheckForSubmit(TimePoint t,
                                                    MetricData *histogram) {
    TuningFork_ErrorCode ret_code = TUNINGFORK_ERROR_OK;
    if (ShouldSubmit(t, histogram)) {
        ret_code = Flush(t, true);
    }
    return ret_code;
}

void TuningForkImpl::InitHistogramSettings() {
    auto max_keys = settings_.aggregation_strategy.max_instrumentation_keys;
    if (max_keys != settings_.histograms.size()) {
        InstrumentationKey default_keys[] = {TFTICK_RAW_FRAME_TIME,
                                             TFTICK_PACED_FRAME_TIME,
                                             TFTICK_CPU_TIME, TFTICK_GPU_TIME};
        // Add histograms that are missing
        auto key_present = [this](InstrumentationKey k) {
            for (auto &h : settings_.histograms) {
                if (k == h.instrument_key) return true;
            }
            return false;
        };
        std::vector<InstrumentationKey> to_add;
        for (auto &k : default_keys) {
            if (!key_present(k)) {
                if (settings_.histograms.size() < max_keys) {
                    ALOGI(
                        "Couldn't get histogram for key index %d. Using "
                        "default histogram",
                        k);
                    settings_.histograms.push_back(
                        Settings::DefaultHistogram(k));
                } else {
                    ALOGE(
                        "Can't fit default histograms: change "
                        "max_instrumentation_keys");
                }
            }
        }
    }
    for (uint32_t i = 0; i < max_keys; ++i) {
        if (i > settings_.histograms.size()) {
            ALOGW(
                "Couldn't get histogram for key index %d. Using default "
                "histogram",
                i);
            settings_.histograms.push_back(Settings::DefaultHistogram(-1));
        } else {
            int index;
            GetOrCreateInstrumentKeyIndex(
                settings_.histograms[i].instrument_key, index);
        }
    }
    // If there was an instrument key but no other settings, update the
    // histogram
    auto check_histogram = [](Settings::Histogram &h) {
        if (h.bucket_max == 0 || h.n_buckets == 0) {
            h = Settings::DefaultHistogram(h.instrument_key);
        }
    };
    for (auto &h : settings_.histograms) {
        check_histogram(h);
    }
    ALOGI("Settings::Histograms");
    for (uint32_t i = 0; i < settings_.histograms.size(); ++i) {
        auto &h = settings_.histograms[i];
        ALOGI("ikey: %d min: %f max: %f nbkts: %d", h.instrument_key,
              h.bucket_min, h.bucket_max, h.n_buckets);
    }
}

void TuningForkImpl::InitAnnotationRadixes() {
    annotation_util::SetUpAnnotationRadixes(
        annotation_radix_mult_,
        settings_.aggregation_strategy.annotation_enum_size);
}

TuningFork_ErrorCode TuningForkImpl::Flush() {
    auto t = std::chrono::steady_clock::now();
    return Flush(t, false);
}

void TuningForkImpl::SwapSessions() {
    if (current_session_ == sessions_[0].get()) {
        sessions_[1]->ClearData();
        current_session_ = sessions_[1].get();
    } else {
        sessions_[0]->ClearData();
        current_session_ = sessions_[0].get();
    }
    async_telemetry_->SetSession(current_session_);
}
TuningFork_ErrorCode TuningForkImpl::Flush(TimePoint t, bool upload) {
    ALOGV("Flush %d", upload);
    TuningFork_ErrorCode ret_code;
    current_session_->SetInstrumentationKeys(ikeys_);
    if (upload_thread_.Submit(current_session_, upload)) {
        SwapSessions();
        ret_code = TUNINGFORK_ERROR_OK;
    } else {
        ret_code = TUNINGFORK_ERROR_PREVIOUS_UPLOAD_PENDING;
    }
    if (upload) last_submit_time_ = t;
    return ret_code;
}

void TuningForkImpl::InitTrainingModeParams() {
    auto cser = settings_.c_settings.training_fidelity_params;
    if (cser != nullptr)
        training_mode_params_ = std::make_unique<ProtobufSerialization>(
            ToProtobufSerialization(*cser));
}

TuningFork_ErrorCode TuningForkImpl::SetFidelityParameters(
    const ProtobufSerialization &params) {
    auto flush_result = Flush();
    if (flush_result != TUNINGFORK_ERROR_OK) {
        ALOGW("Warning, previous data could not be flushed.");
        SwapSessions();
    }
    RequestInfo::CachedValue().current_fidelity_parameters = params;
    // We clear the experiment id here.
    RequestInfo::CachedValue().experiment_id = "";
    return TUNINGFORK_ERROR_OK;
}

bool TuningForkImpl::Debugging() const {
#ifndef NDEBUG
    // Always return true if we are a debug build
    return true;
#else
    // Otherwise, check the APK and system settings
    if (jni::IsValid())
        return apk_utils::GetDebuggable();
    else
        return false;
#endif
}

TuningFork_ErrorCode TuningForkImpl::EnableMemoryRecording(bool enable) {
    if (meminfo_provider_ != nullptr) {
        meminfo_provider_->SetEnabled(enable);
    }
    return TUNINGFORK_ERROR_OK;
}

void TuningForkImpl::InitAsyncTelemetry() {
    async_telemetry_ = std::make_unique<AsyncTelemetry>(time_provider_);
    MemoryTelemetry::SetUpAsyncWork(*async_telemetry_, meminfo_provider_);
    async_telemetry_->SetSession(current_session_);
    async_telemetry_->Start();
}

TuningFork_ErrorCode TuningForkImpl::MetricIdToMemoryMetric(MetricId id,
                                                            MemoryMetric &m) {
    m.memory_record_type_ = (MemoryRecordType)id.detail.memory.record_type;
    m.period_ms_ =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            MemoryTelemetry::UploadPeriodForMemoryType(m.memory_record_type_))
            .count();
    return TUNINGFORK_ERROR_OK;
}

TuningFork_ErrorCode TuningForkImpl::AnnotationIdToSerializedAnnotation(
    tuningfork::AnnotationId id, tuningfork::SerializedAnnotation &ser) {
    auto err = annotation_map_.Get(id, ser);
    if (err != TUNINGFORK_ERROR_OK) return err;
    return TUNINGFORK_ERROR_OK;
}

TuningFork_ErrorCode TuningForkImpl::LoadingTimeMetadataToId(
    const LoadingTimeMetadata &metadata, LoadingTimeMetadataId &id) {
    std::lock_guard<std::mutex> lock(loading_time_metadata_map_mutex_);
    static LoadingTimeMetadataId loading_time_metadata_next_id =
        1;  // 0 is implicitly an empty LoadingTimeMetadata struct
    auto it = loading_time_metadata_map_.find(metadata);
    if (it != loading_time_metadata_map_.end()) {
        id = it->second;
    } else {
        id = loading_time_metadata_next_id++;
        loading_time_metadata_map_.insert({metadata, id});
    }
    return TUNINGFORK_ERROR_OK;
}

TuningFork_ErrorCode TuningForkImpl::MetricIdToLoadingTimeMetadata(
    MetricId id, LoadingTimeMetadata &md) {
    std::lock_guard<std::mutex> lock(loading_time_metadata_map_mutex_);
    auto metadata_id = id.detail.loading_time.metadata;
    for (auto &m : loading_time_metadata_map_) {
        if (m.second == metadata_id) {
            md = m.first;
            return TUNINGFORK_ERROR_OK;
        }
    }
    return TUNINGFORK_ERROR_BAD_PARAMETER;
}

TuningFork_ErrorCode TuningForkImpl::RecordLoadingTime(
    Duration duration, const LoadingTimeMetadata &metadata,
    const ProtobufSerialization &annotation, bool relativeToStart) {
    LoadingTimeMetadataId metadata_id;
    LoadingTimeMetadataToId(metadata, metadata_id);
    AnnotationId ann_id = 0;
    auto err = SerializedAnnotationToAnnotationId(annotation, ann_id);
    if (err != TUNINGFORK_ERROR_OK) return err;
    auto metric_id = MetricId::LoadingTime(ann_id, metadata_id);
    auto data = current_session_->GetData<LoadingTimeMetricData>(metric_id);
    if (data == nullptr)
        return TUNINGFORK_ERROR_NO_MORE_SPACE_FOR_LOADING_TIME_DATA;
    if (relativeToStart)
        data->Record({std::chrono::nanoseconds(0), duration});
    else
        data->Record(duration);
    return TUNINGFORK_ERROR_OK;
}

TuningFork_ErrorCode TuningForkImpl::StartRecordingLoadingTime(
    const LoadingTimeMetadata &metadata,
    const ProtobufSerialization &annotation, LoadingHandle &handle) {
    LoadingTimeMetadataId metadata_id;
    LoadingTimeMetadataToId(metadata, metadata_id);
    AnnotationId ann_id = 0;
    auto err = SerializedAnnotationToAnnotationId(annotation, ann_id);
    if (err != TUNINGFORK_ERROR_OK) return err;
    auto metric_id = MetricId::LoadingTime(ann_id, metadata_id);
    handle = metric_id.base;
    std::lock_guard<std::mutex> lock(live_loading_events_mutex_);
    if (live_loading_events_.find(handle) != live_loading_events_.end())
        return TUNINGFORK_ERROR_DUPLICATE_START_LOADING_EVENT;
    live_loading_events_[handle] = time_provider_->TimeSinceProcessStart();
    return TUNINGFORK_ERROR_OK;
}

TuningFork_ErrorCode TuningForkImpl::StopRecordingLoadingTime(
    LoadingHandle handle) {
    ProcessTimeInterval interval;
    {
        std::lock_guard<std::mutex> lock(live_loading_events_mutex_);
        auto it = live_loading_events_.find(handle);
        if (it == live_loading_events_.end())
            return TUNINGFORK_ERROR_INVALID_LOADING_HANDLE;
        interval.start = it->second;
        interval.end = time_provider_->TimeSinceProcessStart();
        live_loading_events_.erase(it);
    }
    MetricId metric_id;
    metric_id.base = handle;
    auto data = current_session_->GetData<LoadingTimeMetricData>(metric_id);
    if (data == nullptr)
        return TUNINGFORK_ERROR_NO_MORE_SPACE_FOR_LOADING_TIME_DATA;
    data->Record(interval);
    return TUNINGFORK_ERROR_OK;
}

TuningFork_ErrorCode TuningForkImpl::ReportLifecycleEvent(
    TuningFork_LifecycleState state) {
    if (!activity_lifecycle_state_.SetNewState(state)) {
        ALOGV("Discrepancy in lifecycle states, reporting as a crash");
        current_session_->RecordCrash(CRASH_REASON_UNSPECIFIED);
    }
    return TUNINGFORK_ERROR_OK;
}

}  // namespace tuningfork
