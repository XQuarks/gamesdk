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

#include "battery_reporting_task.h"

namespace tuningfork {

class Session;

void BatteryReportingTask::DoWork(Session *session) {
    if (battery_provider_ != nullptr &&
        battery_provider_->IsBatteryReportingEnabled()) {
        std::lock_guard<std::mutex> lock(mutex_);
        session->GetData<BatteryMetricData>(id_)->Record(
            activity_lifecycle_state_->IsAppOnForeground(),
            time_provider_->TimeSinceProcessStart(), battery_provider_);
    }
}

void BatteryReportingTask::UpdateMetricId(MetricId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    id_ = id;
}

}  // namespace tuningfork
