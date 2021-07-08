/*
 * Copyright 2019 The Android Open Source Project
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

#include "histogram.h"
#include "jni/jni_helper.h"
#include "tuningfork_internal.h"
#include "tuningfork_utils.h"

#define LOG_TAG "TuningFork"
#include <android/asset_manager_jni.h>

#include <algorithm>

#include "Log.h"
#include "annotation_util.h"
#include "file_cache.h"
#include "nano/tuningfork.pb.h"
#include "pb_decode.h"
#include "proto/protobuf_nano_util.h"
#include "protobuf_util_internal.h"
using PBSettings = com_google_tuningfork_Settings;

namespace tuningfork {

static FileCache sFileCache;

constexpr char kPerformanceParametersBaseUri[] =
    "https://performanceparameters.googleapis.com/v1/";

constexpr uint64_t kDefaultFrameTimeAnnotationCombinationLimit = 64;

// Use the default persister if the one passed in is null
static void CheckPersister(const TuningFork_Cache*& persister,
                           std::string save_dir) {
    if (persister == nullptr) {
        if (save_dir.empty()) {
            save_dir = DefaultTuningForkSaveDirectory();
        }
        ALOGI("Using local file cache at %s", save_dir.c_str());
        sFileCache.SetDir(save_dir);
        persister = sFileCache.GetCCache();
    }
}

void Settings::Check(const std::string& save_dir) {
    CheckPersister(c_settings.persistent_cache, save_dir);
    if (base_uri.empty()) base_uri = kPerformanceParametersBaseUri;
    if (base_uri.back() != '/') base_uri += '/';
    if (aggregation_strategy.intervalms_or_count == 0) {
        aggregation_strategy.method =
            Settings::AggregationStrategy::Submission::TIME_BASED;
#ifndef NDEBUG
        // For debug builds, upload every 10 seconds
        aggregation_strategy.intervalms_or_count = 10000;
#else
        // For non-debug builds, upload every 2 hours
        aggregation_strategy.intervalms_or_count = 7200000;
#endif
    }
    if (initial_request_timeout_ms == 0) initial_request_timeout_ms = 1000;
    if (ultimate_request_timeout_ms == 0) ultimate_request_timeout_ms = 100000;

    if (c_settings.max_num_metrics.frame_time == 0) {
        auto num_annotation_combinations = NumAnnotationCombinations();
        if (num_annotation_combinations >
            kDefaultFrameTimeAnnotationCombinationLimit)
            ALOGI(
                "You have a large number of annotation combinations. Check "
                "that %" PRIu64
                " is enough for a typical session. If not, set "
                "Settings.max_num_metrics.frame_time.",
                kDefaultFrameTimeAnnotationCombinationLimit);
        c_settings.max_num_metrics.frame_time =
            std::min(kDefaultFrameTimeAnnotationCombinationLimit,
                     num_annotation_combinations) *
            aggregation_strategy.max_instrumentation_keys;
    }
    if (c_settings.max_num_metrics.loading_time == 0)
        c_settings.max_num_metrics.loading_time = 32;
    if (c_settings.max_num_metrics.memory == 0)
        c_settings.max_num_metrics.memory = 15;
    if (c_settings.max_num_metrics.battery == 0)
        c_settings.max_num_metrics.battery = 32;
    if (c_settings.max_num_metrics.thermal == 0)
        c_settings.max_num_metrics.thermal = 32;
}

uint64_t Settings::NumAnnotationCombinations() {
    uint64_t n = 1;
    for (auto x : aggregation_strategy.annotation_enum_size) {
        uint64_t m = n;
        n *= x;
        // Check for overflow
        if (n < m) return std::numeric_limits<uint64_t>::max();
    }
    return n;
}

static bool decodeAnnotationEnumSizes(pb_istream_t* stream,
                                      const pb_field_t* field, void** arg) {
    Settings* settings = static_cast<Settings*>(*arg);
    uint64_t a;
    if (pb_decode_varint(stream, &a)) {
        settings->aggregation_strategy.annotation_enum_size.push_back(
            (uint32_t)a);
        return true;
    } else {
        return false;
    }
}
static bool decodeHistograms(pb_istream_t* stream, const pb_field_t* field,
                             void** arg) {
    Settings* settings = static_cast<Settings*>(*arg);
    com_google_tuningfork_Settings_Histogram hist;
    if (pb_decode(stream, com_google_tuningfork_Settings_Histogram_fields,
                  &hist)) {
        settings->histograms.push_back({hist.instrument_key, hist.bucket_min,
                                        hist.bucket_max, hist.n_buckets});
        return true;
    } else {
        return false;
    }
}

/*static*/ TuningFork_ErrorCode Settings::DeserializeSettings(
    const ProtobufSerialization& settings_ser, Settings* settings) {
    PBSettings pbsettings = com_google_tuningfork_Settings_init_zero;
    pbsettings.aggregation_strategy.annotation_enum_size.funcs.decode =
        decodeAnnotationEnumSizes;
    pbsettings.aggregation_strategy.annotation_enum_size.arg = settings;
    pbsettings.histograms.funcs.decode = decodeHistograms;
    pbsettings.histograms.arg = settings;
    pbsettings.base_uri.funcs.decode = pb_nano::DecodeString;
    pbsettings.base_uri.arg = (void*)&settings->base_uri;
    pbsettings.api_key.funcs.decode = pb_nano::DecodeString;
    pbsettings.api_key.arg = (void*)&settings->api_key;
    pbsettings.default_fidelity_parameters_filename.funcs.decode =
        pb_nano::DecodeString;
    pbsettings.default_fidelity_parameters_filename.arg =
        (void*)&settings->default_fidelity_parameters_filename;
    ByteStream str{const_cast<uint8_t*>(settings_ser.data()),
                   settings_ser.size(), 0};
    pb_istream_t stream = {ByteStream::Read, &str, settings_ser.size()};
    if (!pb_decode(&stream, com_google_tuningfork_Settings_fields, &pbsettings))
        return TUNINGFORK_ERROR_BAD_SETTINGS;
    if (pbsettings.aggregation_strategy.method ==
        com_google_tuningfork_Settings_AggregationStrategy_Submission_TICK_BASED)
        settings->aggregation_strategy.method =
            Settings::AggregationStrategy::Submission::TICK_BASED;
    else
        settings->aggregation_strategy.method =
            Settings::AggregationStrategy::Submission::TIME_BASED;
    settings->aggregation_strategy.intervalms_or_count =
        pbsettings.aggregation_strategy.intervalms_or_count;
    settings->aggregation_strategy.max_instrumentation_keys =
        pbsettings.aggregation_strategy.max_instrumentation_keys;
    settings->initial_request_timeout_ms =
        pbsettings.initial_request_timeout_ms;
    settings->ultimate_request_timeout_ms =
        pbsettings.ultimate_request_timeout_ms;
    // Convert from 1-based to 0 based indices (-1 = not present)
    settings->loading_annotation_index =
        pbsettings.loading_annotation_index - 1;
    settings->level_annotation_index = pbsettings.level_annotation_index - 1;
    // Override API key if passed from c_settings.
    if (settings->c_settings.api_key != nullptr)
        settings->api_key = settings->c_settings.api_key;

    return TUNINGFORK_ERROR_OK;
}

TuningFork_ErrorCode Settings::FindInApk(Settings* settings) {
    if (settings) {
        ProtobufSerialization settings_ser;
        if (apk_utils::GetAssetAsSerialization(
                "tuningfork/tuningfork_settings.bin", settings_ser)) {
            ALOGI("Got settings from tuningfork/tuningfork_settings.bin");
            TuningFork_ErrorCode err =
                DeserializeSettings(settings_ser, settings);
            if (err != TUNINGFORK_ERROR_OK) return err;
            if (settings->aggregation_strategy.annotation_enum_size.size() ==
                0) {
                // If enum sizes are missing, use the descriptor in
                // dev_tuningfork.descriptor
                if (!annotation_util::GetEnumSizesFromDescriptors(
                        settings->aggregation_strategy.annotation_enum_size)) {
                    return TUNINGFORK_ERROR_NO_SETTINGS_ANNOTATION_ENUM_SIZES;
                }
            }
            return TUNINGFORK_ERROR_OK;
        } else {
            return TUNINGFORK_ERROR_NO_SETTINGS;
        }
    } else {
        return TUNINGFORK_ERROR_BAD_PARAMETER;
    }
}

// Default histogram, used e.g. for TF Scaled or when a histogram is missing in
// the settings.
/*static*/ Settings::Histogram Settings::DefaultHistogram(
    InstrumentationKey ikey) {
    Settings::Histogram default_histogram;
    if (ikey == TFTICK_RAW_FRAME_TIME || ikey == TFTICK_PACED_FRAME_TIME) {
        default_histogram.bucket_min = 6.54f;
        default_histogram.bucket_max = 60.0f;
        default_histogram.n_buckets = HistogramBase::kDefaultNumBuckets;
    } else {
        default_histogram.bucket_min = 0.0f;
        default_histogram.bucket_max = 20.0f;
        default_histogram.n_buckets = HistogramBase::kDefaultNumBuckets;
    }
    if (ikey >= TFTICK_RAW_FRAME_TIME) {
        // Fill in the well-known keys
        default_histogram.instrument_key = ikey;
    } else {
        default_histogram.instrument_key = -1;
    }
    return default_histogram;
}

}  // namespace tuningfork
