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

#include <gtest/gtest.h>

#include <chrono>

#include "core/tuningfork_utils.h"
#include "http_backend/json_serializer.h"
#include "tf_test_utils.h"

namespace serialization_test {

using namespace tuningfork;
using namespace json11;
using namespace test;
using namespace std::chrono;

RequestInfo test_device_info{"expt", {},      "sess",    2387,       349587,
                             "fing", "6.3",   {1, 2, 3}, "packname", 0,
                             10,     "MODEL", "BRAND",   "PRODUCT",  "DEVICE"};
std::string test_device_info_ser = R"TF({
  "brand": "BRAND",
  "build_version": "6.3",
  "cpu_core_freqs_hz": [1, 2, 3],
  "device": "DEVICE",
  "fingerprint": "fing",
  "gles_version": {
    "major": 5, "minor": 21907
  },
  "model": "MODEL",
  "product": "PRODUCT",
  "total_memory_bytes": 2387
})TF";

void CheckDeviceInfo(const RequestInfo& info) {
    EXPECT_EQ(json_utils::GetResourceName(info), "applications/packname/apks/0")
        << "GetResourceName";
    EXPECT_TRUE(CompareIgnoringWhitespace(
        Json(json_utils::DeviceSpecJson(info)).dump(), test_device_info_ser))
        << "DeviceSpecJson";
}

TEST(SerializationTest, DeviceInfo) { CheckDeviceInfo(test_device_info); }

std::string report_start = R"TF({
  "name": "applications/packname/apks/0",
  "session_context": {
    "device": {
      "brand": "BRAND",
      "build_version": "6.3",
      "cpu_core_freqs_hz": [1, 2, 3],
      "device": "DEVICE",
      "fingerprint": "fing",
      "gles_version": {
        "major": 5,
        "minor": 21907
      },
      "model": "MODEL",
      "product": "PRODUCT",
      "total_memory_bytes": 2387
    },
    "game_sdk_info": {
      "session_id": "sess",
      "version": "1.0"
    },
    "time_period": {
      "end_time": "1970-01-01T00:00:00.000000Z",
      "start_time": "1970-01-01T00:00:00.000000Z"
    }
  },
  "telemetry": [)TF";
std::string report_end = "]}";

std::string single_tick_with_loading = R"TF({
  "context": {
    "annotations": "AQID",
    "duration": "1.51s",
    "tuning_parameters": {
      "experiment_id": "expt",
      "serialized_fidelity_parameters": ""
    }
  },
  "report": {
    "loading": {
      "loading_events": [{
        "loading_metadata": {
          "network_info": {
            "bandwidth_bps": "1000000000",
            "connectivity": 1,
            "latency": "0.05s"
          },
          "source":5,
          "state":1
        },
        "times_ms": [1500]
      }]
    },
    "rendering": {
      "render_time_histogram": [{"counts": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                            0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
      "instrument_id": 1234
      }]
    }
  }
})TF";

class IdMap : public IdProvider {
    TuningFork_ErrorCode SerializedAnnotationToAnnotationId(
        const ProtobufSerialization& ser, AnnotationId& id,
        bool* loading = nullptr) const override {
        id = 0;
        return TUNINGFORK_ERROR_OK;
    }

    // Return a new id that is made up of <annotation_id> and <k>.
    // Gives an error if the id is out-of-bounds.
    TuningFork_ErrorCode MakeCompoundId(InstrumentationKey k,
                                        AnnotationId annotation_id,
                                        MetricId& id) override {
        id = MetricId::FrameTime(annotation_id, k);
        return TUNINGFORK_ERROR_OK;
    }

    TuningFork_ErrorCode AnnotationIdToSerializedAnnotation(
        AnnotationId id, SerializedAnnotation& ann) override {
        ann = {1, 2, 3};
        return TUNINGFORK_ERROR_OK;
    }
    TuningFork_ErrorCode MetricIdToMemoryMetric(MetricId id,
                                                MemoryMetric& m) override {
        // Not used
        m = {};
        return TUNINGFORK_ERROR_OK;
    }
    TuningFork_ErrorCode MetricIdToLoadingTimeMetadata(
        MetricId id, LoadingTimeMetadata& m) override {
        m = {};
        m.state = LoadingTimeMetadata::FIRST_RUN;
        m.source = LoadingTimeMetadata::NETWORK;
        m.network_latency_ns = 50000000;  // 50ms
        m.network_connectivity = LoadingTimeMetadata::NetworkConnectivity::WIFI;
        m.network_transfer_speed_bps = 1000000000;  // 1Gb/s
        return TUNINGFORK_ERROR_OK;
    }
};

TEST(SerializationTest, SerializationWithLoading) {
    Session session{};
    MetricId loading_time_metric = MetricId::LoadingTime(0, 0);
    MetricId frame_time_metric = MetricId::FrameTime(0, 0);
    session.CreateLoadingTimeSeries(loading_time_metric);
    session.CreateFrameTimeHistogram(frame_time_metric,
                                     Settings::DefaultHistogram(1));

    std::string evt_ser;
    session.SetInstrumentationKeys({1234});
    IdMap metric_map;
    JsonSerializer serializer(session, &metric_map);
    serializer.SerializeEvent(test_device_info, evt_ser);
    auto empty_report = report_start + report_end;
    EXPECT_TRUE(CompareIgnoringWhitespace(evt_ser, empty_report))
        << evt_ser << "\n!=\n"
        << empty_report;

    // Fill in some data
    auto p1 = session.GetData<LoadingTimeMetricData>(loading_time_metric);
    ASSERT_NE(p1, nullptr);
    p1->Record(milliseconds(1500));

    auto p2 = session.GetData<FrameTimeMetricData>(frame_time_metric);
    ASSERT_NE(p2, nullptr);
    p2->Record(milliseconds(10));

    // Try to record with some LoadingTimeMetadata - this will fail because we
    // didn't allocate any extra loading time space.
    MetricId new_loading_time_metric = MetricId::LoadingTime(0, 1);
    EXPECT_EQ(session.GetData<LoadingTimeMetricData>(new_loading_time_metric),
              nullptr);

    serializer.SerializeEvent(test_device_info, evt_ser);
    auto report = report_start + single_tick_with_loading + report_end;
    EXPECT_TRUE(CompareIgnoringWhitespace(evt_ser, report))
        << evt_ser << "\n!=\n"
        << report;
}

std::string single_tick = R"TF({
  "context": {
    "annotations": "AQID",
    "duration": "0.03s",
    "tuning_parameters": {
      "experiment_id": "expt",
      "serialized_fidelity_parameters": ""
    }
  },
  "report": {
    "rendering": {
      "render_time_histogram": [{
        "counts": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                   0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
        "instrument_id": 0
      }]
    }
  }
})TF";

void CheckSessions(Session& pc0, Session& pc1) {
    auto p0 = pc0.GetData<FrameTimeMetricData>(MetricId::FrameTime(0, 0));
    auto p1 = pc1.GetData<FrameTimeMetricData>(MetricId::FrameTime(0, 0));
    EXPECT_TRUE(p0 != nullptr && p1 != nullptr);
    EXPECT_EQ(p0->histogram_, p1->histogram_);
}

Settings::Histogram DefaultHistogram() { return {-1, 10, 40, 30}; }

TEST(SerializationTest, GEDeserialization) {
    Session session{};
    FrameTimeMetric metric(0);
    MetricId metric_id{0};
    session.CreateFrameTimeHistogram(metric_id, DefaultHistogram());
    std::string evt_ser;
    IdMap metric_map;
    JsonSerializer serializer(session, &metric_map);
    serializer.SerializeEvent(test_device_info, evt_ser);
    auto empty_report = report_start + report_end;
    EXPECT_TRUE(CompareIgnoringWhitespace(evt_ser, empty_report))
        << evt_ser << "\n!=\n"
        << empty_report;
    // Fill in some data
    auto p = session.GetData<FrameTimeMetricData>(metric_id);
    p->Record(milliseconds(30));
    serializer.SerializeEvent(test_device_info, evt_ser);
    auto report = report_start + single_tick + report_end;
    EXPECT_TRUE(CompareIgnoringWhitespace(evt_ser, report))
        << evt_ser << "\n!=\n"
        << report;
    Session session1{};
    session1.CreateFrameTimeHistogram(metric_id, DefaultHistogram());
    EXPECT_EQ(
        JsonSerializer::DeserializeAndMerge(evt_ser, metric_map, session1),
        TUNINGFORK_ERROR_OK)
        << "Deserialize single";
    CheckSessions(session1, session);
}

}  // namespace serialization_test
