/*
 * Copyright 2021 The Android Open Source Project
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

#include <mutex>
#include <string>

#include "async_telemetry.h"
#include "session.h"
#include "tuningfork_internal.h"

namespace tuningfork {

class ThermalReportingTask : public RepeatingTask {
   private:
    ITimeProvider* time_provider_;
    IBatteryProvider* battery_provider_;
    std::mutex mutex_;
    MetricId id_;

   public:
    ThermalReportingTask(ITimeProvider* time_provider,
                         IBatteryProvider* battery_provider, MetricId id)
        : RepeatingTask(std::chrono::seconds(60)),
          time_provider_(time_provider),
          battery_provider_(battery_provider),
          id_(id) {}
    virtual void DoWork(Session* session) override;
    void UpdateMetricId(MetricId id);
};

}  // namespace tuningfork
