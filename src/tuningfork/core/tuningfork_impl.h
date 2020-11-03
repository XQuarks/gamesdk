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

#include <atomic>
#include <memory>

#include "Trace.h"
#include "activity_lifecycle_state.h"
#include "annotation_map.h"
#include "async_telemetry.h"
#include "crash_handler.h"
#include "http_backend/http_backend.h"
#include "meminfo_provider.h"
#include "session.h"
#include "time_provider.h"
#include "tuningfork_internal.h"
#include "tuningfork_swappy.h"
#include "uploadthread.h"

namespace tuningfork {

class TuningForkImpl : public IdProvider {
   private:
    CrashHandler crash_handler_;
    Settings settings_;
    std::unique_ptr<Session> sessions_[2];
    Session *current_session_;
    TimePoint last_submit_time_;
    std::unique_ptr<gamesdk::Trace> trace_;
    std::vector<TimePoint> live_traces_;
    IBackend *backend_;
    UploadThread upload_thread_;
    SerializedAnnotation current_annotation_;
    std::vector<uint32_t> annotation_radix_mult_;
    MetricId current_annotation_id_;
    ITimeProvider *time_provider_;
    IMemInfoProvider *meminfo_provider_;
    std::vector<InstrumentationKey> ikeys_;
    std::atomic<int> next_ikey_;
    TimePoint loading_start_;
    std::unique_ptr<ProtobufSerialization> training_mode_params_;
    std::unique_ptr<AsyncTelemetry> async_telemetry_;
    std::mutex loading_time_metadata_map_mutex_;
    std::unordered_map<LoadingTimeMetadata, LoadingTimeMetadataId>
        loading_time_metadata_map_;
    ActivityLifecycleState activity_lifecycle_state_;
    bool before_first_tick_;
    bool app_first_run_;
    std::unordered_map<LoadingHandle, ProcessTime> live_loading_events_;
    std::mutex live_loading_events_mutex_;
    AnnotationMap annotation_map_;

    std::unique_ptr<ITimeProvider> default_time_provider_;
    std::unique_ptr<HttpBackend> default_backend_;
    std::unique_ptr<IMemInfoProvider> default_meminfo_provider_;

    TuningFork_ErrorCode initialization_error_code_ = TUNINGFORK_ERROR_OK;

   public:
    TuningForkImpl(const Settings &settings, IBackend *backend,
                   ITimeProvider *time_provider,
                   IMemInfoProvider *memory_provider,
                   bool first_run /* whether we have just installed the app*/);

    ~TuningForkImpl();

    void InitHistogramSettings();

    void InitAnnotationRadixes();

    void InitTrainingModeParams();

    // Returns true if the fidelity params were retrieved
    TuningFork_ErrorCode GetFidelityParameters(
        const ProtobufSerialization &defaultParams,
        ProtobufSerialization &fidelityParams, uint32_t timeout_ms);

    // Returns the set annotation id or -1 if it could not be set
    MetricId SetCurrentAnnotation(const ProtobufSerialization &annotation);

    TuningFork_ErrorCode FrameTick(InstrumentationKey id);

    TuningFork_ErrorCode FrameDeltaTimeNanos(InstrumentationKey id,
                                             Duration dt);

    // Fills handle with that to be used by EndTrace
    TuningFork_ErrorCode StartTrace(InstrumentationKey key,
                                    TraceHandle &handle);

    TuningFork_ErrorCode EndTrace(TraceHandle);

    void SetUploadCallback(TuningFork_UploadCallback cbk);

    TuningFork_ErrorCode Flush();

    TuningFork_ErrorCode Flush(TimePoint t, bool upload);

    const Settings &GetSettings() const { return settings_; }

    TuningFork_ErrorCode SetFidelityParameters(
        const ProtobufSerialization &params);

    TuningFork_ErrorCode EnableMemoryRecording(bool enable);

    TuningFork_ErrorCode RecordLoadingTime(
        Duration duration, const LoadingTimeMetadata &metadata,
        const ProtobufSerialization &annotation, bool relativeToStart);

    TuningFork_ErrorCode StartRecordingLoadingTime(
        const LoadingTimeMetadata &metadata,
        const ProtobufSerialization &annotation, LoadingHandle &handle);

    TuningFork_ErrorCode StopRecordingLoadingTime(LoadingHandle handle);

    TuningFork_ErrorCode ReportLifecycleEvent(TuningFork_LifecycleState state);

    TuningFork_ErrorCode InitializationErrorCode() {
        return initialization_error_code_;
    }

   private:
    // Record the time between t and the previous tick in the histogram
    // associated with compound_id. Return the MetricData associated with
    // compound_id in *ppdata if ppdata is non-null and there is no error.
    TuningFork_ErrorCode TickNanos(MetricId compound_id, TimePoint t,
                                   MetricData **ppdata);

    // Record dt in the histogram associated with compound_id.
    // Return the MetricData associated with compound_id in *ppdata if
    // ppdata is non-null and there is no error.
    TuningFork_ErrorCode TraceNanos(MetricId compound_id, Duration dt,
                                    MetricData **ppdata);

    TuningFork_ErrorCode CheckForSubmit(TimePoint t, MetricData *metric_data);

    bool ShouldSubmit(TimePoint t, MetricData *metric_data);

    TuningFork_ErrorCode SerializedAnnotationToAnnotationId(
        const SerializedAnnotation &ser, AnnotationId &id) override;

    // Return a new id that is made up of <annotation_id> and <k>.
    // Gives an error if the id is out-of-bounds.
    TuningFork_ErrorCode MakeCompoundId(InstrumentationKey k,
                                        AnnotationId annotation_id,
                                        MetricId &id) override;

    TuningFork_ErrorCode AnnotationIdToSerializedAnnotation(
        AnnotationId id, SerializedAnnotation &ser) override;

    TuningFork_ErrorCode MetricIdToMemoryMetric(MetricId id,
                                                MemoryMetric &m) override;

    TuningFork_ErrorCode LoadingTimeMetadataToId(
        const LoadingTimeMetadata &metadata, LoadingTimeMetadataId &id);

    TuningFork_ErrorCode MetricIdToLoadingTimeMetadata(
        MetricId id, LoadingTimeMetadata &md) override;

    bool keyIsValid(InstrumentationKey key) const;

    TuningFork_ErrorCode GetOrCreateInstrumentKeyIndex(InstrumentationKey key,
                                                       int &index);

    bool Loading() const { return live_loading_events_.size() > 0; }

    void SwapSessions();

    bool Debugging() const;

    void InitAsyncTelemetry();

    void CreateSessionFrameHistograms(
        Session &session, size_t size, int max_num_instrumentation_keys,
        const std::vector<Settings::Histogram> &histogram_settings,
        const TuningFork_MetricLimits &limits);
};

}  // namespace tuningfork
