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

#include "json_serializer.h"

#include <set>
#include <sstream>

#define LOG_TAG "TuningFork"
#include "Log.h"

#include "core/tuningfork_utils.h"
#include "core/annotation_util.h"

#include "modp_b64.h"

// These string functions are needed for date.h when using gnustl in the NDK

#if (defined ANDROID_GNUSTL) || ((defined ANDROID_NDK_VERSION) && ANDROID_NDK_VERSION<=17)

namespace std {

template <typename T>
std::string to_string(T value)
{
    std::stringstream os;
    os << value;
    return os.str();
}
template <typename T>
std::wstring to_wstring(T value)
{
    std::wstringstream os;
    os << value;
    return os.str();
}

} // namespace std

#endif

#if (defined ANDROID_GNUSTL)

namespace std {

long double stold( const std::string& str, std::size_t* pos = 0 ) {
    long double d;
    std::stringstream is(str);
    auto p0 = is.tellg();
    is >> d;
    if (pos != nullptr) {
        *pos = is.tellg() - p0;
    }
    return d;
    }

} // namespace std
#endif

// TODO(b/140155101): Move the date library into aosp/external
#include "date/date.h"

namespace tuningfork {

using namespace json11;
using namespace std::chrono;
using namespace date;

Json::object GameSdkInfoJson(const RequestInfo& request_info) {
    std::stringstream version_str;
    version_str << TUNINGFORK_MAJOR_VERSION << "." << TUNINGFORK_MINOR_VERSION;
    return Json::object {
        {"version", version_str.str() },
        {"session_id", request_info.session_id}
    };
}

std::string TimeToRFC3339(system_clock::time_point tp) {
    std::stringstream str;
    //"{year}-{month}-{day}T{hour}:{min}:{sec}[.{frac_sec}]Z"
    auto const dp = date::floor<days>(tp);
    str << year_month_day(dp) << 'T' << make_time(tp-dp) << 'Z';
    return str.str();
}

system_clock::time_point RFC3339ToTime(const std::string& s) {
    std::istringstream str(s);
    int y, m, d;
    char delim;
    str >> y >> delim >> m >> delim >> d >> delim;
    int hours, mins;
    double secs;
    str >> hours >> delim >> mins >> secs;
    return sys_days(year_month_day(year(y), month(m), day(d)))
            + microseconds(static_cast<uint64_t>((hours*3600 + mins*60 + secs)*1000000.0));
}

std::string DurationToSecondsString(Duration d) {
    std::stringstream str;
    str << (duration_cast<nanoseconds>(d).count()/1000000000.0) << 's';
    return str.str();
}
Duration StringToDuration(const std::string& s) {
    double d;
    std::stringstream str(s);
    str >> d;
    return nanoseconds(static_cast<uint64_t>(d*1000000000));
}

std::string B64Encode(const std::vector<uint8_t>& bytes) {
    if (bytes.size()==0) return "";
    std::string enc(modp_b64_encode_len(bytes.size()), ' ');
    size_t l = modp_b64_encode(const_cast<char*>(enc.c_str()),
                               reinterpret_cast<const char*>(bytes.data()), bytes.size());
    enc.resize(l);
    return enc;
}
std::vector<uint8_t> B64Decode(const std::string& s) {
    if (s.length()==0) return std::vector<uint8_t>();
    std::vector<uint8_t> ret(modp_b64_decode_len(s.length()));
    size_t l = modp_b64_decode(reinterpret_cast<char*>(ret.data()), s.c_str(), s.length());
    ret.resize(l);
    return ret;
}

std::string JsonUint64(uint64_t x) {
    // Json doesn't support 64-bit integers, so protobufs use strings
    // https://developers.google.com/protocol-buffers/docs/proto3#json
    std::stringstream s;
    s << x;
    return s.str();
}
Json::object TelemetryContextJson(const ProngCache& prong_cache,
                                  const SerializedAnnotation& annotation,
                                  const RequestInfo& request_info,
                                  const Duration& duration) {
    return Json::object {
        {"annotations", B64Encode(annotation)},
        {"tuning_parameters", Json::object {
                {"experiment_id", request_info.experiment_id},
                {"serialized_fidelity_parameters", B64Encode(request_info.current_fidelity_parameters)}
            }
        },
        {"duration", DurationToSecondsString(duration)}
    };
}

Json::object TelemetryReportJson(const ProngCache& prong_cache,
                                 const SerializedAnnotation& annotation,
                                 bool& empty, Duration& duration) {
    std::vector<Json::object> render_histograms;
    std::vector<Json::object> loading_histograms;
    std::vector<int> loading_events_times; // Ignore fractional milliseconds
    duration = Duration::zero();
    for(auto& p: prong_cache.prongs()) {
        if (p.get()!=nullptr && p->Count()>0 && p->annotation_==annotation) {
            if (p->histogram_.GetMode() != HistogramBase::Mode::HISTOGRAM) {
                for(auto c: p->histogram_.samples()) {
                    auto v = static_cast<int>(c);
                    if (v != 0)
                        loading_events_times.push_back(v);
                }
            }
            else {
                std::vector<int32_t> counts;
                for (auto &c: p->histogram_.buckets())
                    counts.push_back(static_cast<int32_t>(c));
                Json::object o {
                    {"counts",        counts}};
                if (p->IsLoading()) {
                    // Should never get here: we make loading histograms events-only in prong.cpp
                    loading_histograms.push_back(o);
                }
                else {
                    o["instrument_id"] = p->instrumentation_key_;
                    render_histograms.push_back(o);
                }
            }
            duration += p->duration_;
        }
    }
    int total_size = render_histograms.size() + loading_histograms.size() +
            loading_events_times.size();
    empty = (total_size==0);
    // Use the average duration for this annotation
    if (!empty)
        duration /= total_size;
    Json::object ret;
    if (render_histograms.size()>0) {
        ret["rendering"] = Json::object {
            {"render_time_histogram", render_histograms }
        };
    }
    // Loading histograms
    if (loading_histograms.size()>0 || loading_events_times.size()>0) {
        Json::object loading;
        if (loading_histograms.size()>0) {
            loading["loading_time_histogram"] = loading_histograms;
        }
        if (loading_events_times.size()>0) {
            loading["loading_events"] = Json::object{{"times_ms", loading_events_times}};
        }
        ret["loading"] = loading;
    }
    return ret;
}
Json::object MemoryTelemetryReportJson(const ProngCache& prong_cache,
                                 const SerializedAnnotation& annotation,
                                 bool& empty) {
    std::vector<Json::object> memory_histograms;
    for( auto& mem_histogram: prong_cache.GetMemoryTelemetry().GetHistograms()) {
        auto h = mem_histogram.histogram;
        if (h.Count() != 0) {
            if (h.GetMode()==HistogramBase::Mode::AUTO_RANGE)
                h.CalcBucketsFromSamples();
            std::vector<int32_t> counts;
            for(auto c: h.buckets())
                counts.push_back(static_cast<int32_t>(c));

            Json::object histogram_config {
                {"bucket_min_bytes", JsonUint64(h.BucketStart())},
                {"bucket_max_bytes", JsonUint64(h.BucketEnd())}
            };
            Json::object o {
                {"type", mem_histogram.type},
                {"period_ms", static_cast<int>(mem_histogram.period_ms)},
                {"histogram_config", histogram_config},
                {"counts",        counts}};
            memory_histograms.push_back(o);
        }
    }
    // Memory histogram
    Json::object ret;
    empty = memory_histograms.size()==0;
    if (!empty) {
        Json::object memory;
        memory["memory_histogram"] = memory_histograms;
        ret["memory"] = memory;
    }
    return ret;
}

Json::object TelemetryJson(const ProngCache& prong_cache,
                           const SerializedAnnotation& annotation,
                           const RequestInfo& request_info,
                           Duration& duration,
                           bool& empty)
{
    auto report = TelemetryReportJson(prong_cache, annotation, empty, duration);
    return Json::object {
        {"context", TelemetryContextJson(prong_cache, annotation, request_info, duration)},
        {"report", report}
    };

}

Json::object MemoryTelemetryJson(const ProngCache& prong_cache,
                                 const SerializedAnnotation& annotation,
                                 const RequestInfo& request_info,
                                 const Duration& duration,
                                 bool& empty) {
    auto report = MemoryTelemetryReportJson(prong_cache, annotation, empty);
    return Json::object {
        {"context", TelemetryContextJson(prong_cache, annotation, request_info, duration)},
        {"report", report}
    };
}

/*static*/ void JsonSerializer::SerializeEvent(const ProngCache& prongs,
                                             const RequestInfo& request_info,
                                             std::string& evt_json_ser) {
    Json session_context = Json::object {
        {"device", json_utils::DeviceSpecJson(request_info)},
        {"game_sdk_info", GameSdkInfoJson(request_info)},
        {"time_period",
            Json::object {
                {"start_time", TimeToRFC3339(prongs.time().start)},
                {"end_time", TimeToRFC3339(prongs.time().end)}
            }
        }
    };
    std::vector<Json::object> telemetry;
    // Loop over unique annotations
    std::set<SerializedAnnotation> annotations;
    for(auto& p: prongs.prongs()) {
        if (p.get() != nullptr)
            annotations.insert(p->annotation_);
    }
    Duration sum_duration = Duration::zero();
    for(auto& a: annotations) {
        bool empty;
        Duration duration;
        auto tel = TelemetryJson(prongs, a, request_info, duration, empty);
        sum_duration += duration;
        if(!empty)
            telemetry.push_back(tel);
    }
    if (!annotations.empty()) {
        bool empty;
        // Rather than recording a memory histogram for each annotation, we are recording a single
        // histogram, but each report needs to be associated with a context, including an
        // annotation. We use the first one and expect it to be ignored  on the Play side.
        auto& a = *annotations.begin();
        auto tel = MemoryTelemetryJson(prongs, a, request_info, sum_duration, empty);
        if(!empty)
            telemetry.push_back(tel);
    }
    Json upload_telemetry_request = Json::object {
        {"name", json_utils::GetResourceName(request_info)},
        {"session_context", session_context},
        {"telemetry", telemetry}
    };
    evt_json_ser = upload_telemetry_request.dump();
}

std::string Serialize(std::vector<uint32_t> vs) {
    std::stringstream str;
    str << "[";
    for(auto& v: vs) {
        str << " " << v;
    }
    str << "]";
    return str.str();
}
namespace {
struct Hist {
    ProtobufSerialization annotation;
    ProtobufSerialization fidelity_params;
    uint64_t instrument_id;
    Duration duration;
    std::vector<uint32_t> counts;
};
}

/*static*/ TuningFork_ErrorCode JsonSerializer::DeserializeAndMerge(
    const std::string& evt_json_ser,
    IdProvider& id_provider,
    ProngCache& pc) {
    std::string err;
    Json in = Json::parse(evt_json_ser, err);
    if (!err.empty()) {
        ALOGE("Failed to deserialize %s\n%s", evt_json_ser.c_str(), err.c_str());
        return TUNINGFORK_ERROR_BAD_PARAMETER;
    }

    // Deserialize
    auto start = RFC3339ToTime(in["session_context"]["time_period"]["start_time"].string_value());
    auto end = RFC3339ToTime(in["session_context"]["time_period"]["start_time"].string_value());
    std::vector<Hist> hists;
    for (auto& telemetry : in["telemetry"].array_items()) {
        // Context
        auto& context = telemetry["context"];
        if (context.is_null())
            return TUNINGFORK_ERROR_BAD_PARAMETER;
        auto annotation = B64Decode(context["annotations"].string_value());
        auto fps = B64Decode(context["tuning_parameters"]["serialized_fidelity_parameters"]
                                 .string_value());
        auto duration = StringToDuration(context["duration"].string_value());
        // Report
        auto& report = telemetry["report"]["rendering"];
        if (report.is_null())
            return TUNINGFORK_ERROR_BAD_PARAMETER;
        for(auto& histogram: report["render_time_histogram"].array_items()) {
            uint64_t instrument_id = histogram["instrument_id"].int_value();
            std::vector<uint32_t> cs;
            for(auto& c: histogram["counts"].array_items()) {
                cs.push_back( c.int_value() );
            }
            if (cs.size()>0)
                hists.push_back({annotation, fps, instrument_id, duration, cs});
        }
    }

    // Merge
    for(auto& h: hists) {
        uint64_t id;
        auto annotation_id = id_provider.DecodeAnnotationSerialization(h.annotation);
        if (annotation_id== annotation_util::kAnnotationError)
            return TUNINGFORK_ERROR_BAD_PARAMETER;
        auto r = id_provider.MakeCompoundId(h.instrument_id, annotation_id, id);
        if (r!=TUNINGFORK_ERROR_OK)
            return r;
        auto p = pc.Get(id);
        if (p==nullptr)
            return TUNINGFORK_ERROR_BAD_PARAMETER;
        auto& orig_counts = p->histogram_.buckets();
        p->histogram_.AddCounts(h.counts);
    }
    return TUNINGFORK_ERROR_OK;
}

} // namespace tuningfork
