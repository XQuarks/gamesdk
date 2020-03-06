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

/**
 * This operation queries the GLES and EGL libraries for the list of extensions
 * available on the device. The initial use case was to query a set of devices
 * in order to locate one that supported a specific extension.
 *
 * Input configuration:
 * - None
 *
 * Output report:
 * - gl_extensions:  the list of supported GLES extensions
 * - egl_extensions: the list of supported EGl extensions
 *
 * (Note: No manipulation is done to the strings in the report lists of
 * extensions; therefore, they should match those found in online documentation
 * and specifications.)
 */

#include <ancer/BaseGLES3Operation.hpp>
#include <ancer/DatumReporting.hpp>
#include <ancer/System.hpp>

using namespace ancer;

//==============================================================================

namespace {
constexpr Log::Tag TAG{"GetExtensionsGLES3Operation"};
}  // anonymous namespace

namespace {

struct datum {
  std::vector<std::string> gl_extensions;
  std::vector<std::string> egl_extensions;
};

void WriteDatum(report_writers::Struct w, const datum& d) {
  ADD_DATUM_MEMBER(w, d, gl_extensions);
  ADD_DATUM_MEMBER(w, d, egl_extensions);
}

}  // anonymous namespace

//==============================================================================

class GetExtensionsGLES3Operation : public BaseGLES3Operation {
 public:
  GetExtensionsGLES3Operation() = default;

  ~GetExtensionsGLES3Operation() {}

  void OnGlContextReady(const GLContextConfig& ctx_config) override {
    Log::D(TAG, "GlContextReady");

    SetHeartbeatPeriod(500ms);

    _egl_context = eglGetCurrentContext();
    if (_egl_context == EGL_NO_CONTEXT) {
      FatalError(TAG, "No EGL context available");
    }

    LogExtensions();
    Stop();
  }

  void Draw(double delta_seconds) override {
    BaseGLES3Operation::Draw(delta_seconds);
  }

  void OnHeartbeat(Duration elapsed) override {}

 private:
  void LogExtensions() {
    const auto gl_extensions = glh::GetGlExtensions();
    const auto egl_extensions = glh::GetEglExtensions();
    datum d = {};
    d.gl_extensions = gl_extensions;
    d.egl_extensions = egl_extensions;
    Report(d);
  }

 private:
  EGLContext _egl_context = EGL_NO_CONTEXT;
};

EXPORT_ANCER_OPERATION(GetExtensionsGLES3Operation)