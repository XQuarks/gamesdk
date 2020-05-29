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

#pragma once

#include <sstream>
#include <jni.h>
#include <string>
#include <memory>

#include "tuningfork_internal.h"
#include "web.h"

namespace tuningfork {

class UltimateUploader;

// Google Endpoint backend
class GEBackend : public Backend {
public:
    TuningFork_ErrorCode Init(const Settings& settings,
                     const ExtraUploadInfo& extra_upload_info);
    ~GEBackend() override;
    TuningFork_ErrorCode Process(const std::string &json_event) override;
    void KillThreads();
private:
    std::shared_ptr<UltimateUploader> ultimate_uploader_;
    const TFCache* persister_;
};

} //namespace tuningfork {
