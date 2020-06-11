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

#pragma once

#include "tuningfork/tuningfork.h"
#include "core/common.h"
#include "proto/protobuf_util.h"

class AAsset;

namespace tuningfork {

// Interface to an object that can decode protobuf serializations of Annotations and
// create compound ids using an InstrumentationKey.
class IdProvider {
  public:
    virtual ~IdProvider() {}
    // Decode <ser> into an AnnotationId. If loading is non-null, it returns whether the
    // annotation is a loading annotation.
    virtual AnnotationId DecodeAnnotationSerialization(const ProtobufSerialization& ser,
                                                   bool* loading = nullptr) const = 0;
    // Return a new id that is made up of <annotation_id> and <k>.
    // Gives an error if the id is out-of-bounds.
    virtual TuningFork_ErrorCode MakeCompoundId(InstrumentationKey k,
                                       AnnotationId annotation_id,
                                       AnnotationId& id) = 0;
};

} // namespace tuningfork
