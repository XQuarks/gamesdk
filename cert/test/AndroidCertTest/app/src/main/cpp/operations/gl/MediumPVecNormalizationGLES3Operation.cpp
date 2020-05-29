//
// Created by Ganesa Chinnathambi on 2019-10-29.
//
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

/*
 * MediumPVecNormalizationGLES3Operation
 *
 * This test aims to identify devices which fail to correctly normalize
 * 3-component half-precision vectors - per reports from developers that
 * some hardware produced "garbage" when attempting to do so.
 *
 * In short, given a 3-component half precision vector, then incrementing
 * one or more fields to a value > 127, when normalized incorrect results
 * are observed from some hardware.
 *
 * Input:
 *
 * vertex_stage_configuration: configuration params for the test as run on vertex processor
 *  enabled: [bool] is this test enabled?
 *  offset_steps:[int] number of steps in range to test
 *  offset_scale:[float] each channel will be incremented by (i / offset_steps) * offset_scale
 *    resulting in offset values from [0, offset_scale]
 * fragment_stage_configuration: configuration params for running the test
 *  on the fragment processor - same fields as above
 * varying_configuration: configuration params for running the test on
 *  vertex and fragment stages, passing data through the varying pipeline. Same
 *  params as above.
 *
 * Output:
 *
 *  test:[string] the test for which this datum is issued, one of
 *    "VertexStageTest", "FragmentStageTest", "VaryingPassthroughTest"
 *  failure:[bool] if true, this datum represents a failed normalization
 *  expected_failure:[bool] if true, normalization failure is expected due to precision
 *  expected_rgb:[rgb triplet] expected framebuffer rgb value for the read fragment
 *  actual_rgb:[rgb triplet] value read from framebuffer
 *  offset:[float] offset applied to one ore more components of the vec3
 *  squared_magnitude:[float] squared mag of the vector that was normalized,
 *    i.e. (v.x*v.x + v.y*v.y + v.z*v.z)
 */

#pragma clang diagnostic push
#pragma ide diagnostic ignored "misc-non-private-member-variables-in-classes"

#include <cmath>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <ancer/BaseGLES3Operation.hpp>
#include <ancer/DatumReporting.hpp>
#include <ancer/System.hpp>
#include <ancer/util/GLPixelBuffer.hpp>
#include <ancer/util/Json.hpp>
#include <iostream>

#include <list>

using namespace ancer;

//==============================================================================

namespace {
constexpr Log::Tag TAG{"MediumPVecNormalizationGLES3Operation"};

struct rgb_u8 {

  rgb_u8() = default;

  rgb_u8(GLubyte r, GLubyte g, GLubyte b)
      : r(r), g(g), b(b) {}

  rgb_u8(vec3 v)
      : r(static_cast<GLubyte>(ceil(v.r * 255))),
        g(static_cast<GLubyte>(ceil(v.g * 255))),
        b(static_cast<GLubyte>(ceil(v.b * 255))) {}

  rgb_u8(glm::u8vec4 v)
      : r(v.r), g(v.g), b(v.b) {}

  GLubyte r = 0;
  GLubyte g = 0;
  GLubyte b = 0;
};

bool operator==(const rgb_u8& lhs, const rgb_u8& rhs) {
  return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b;
}

bool operator!=(const rgb_u8& lhs, const rgb_u8& rhs) {
  return lhs.r != rhs.r || lhs.g != rhs.g || lhs.b != rhs.b;
}

void WriteDatum(report_writers::Struct w, const rgb_u8& d) {
  ADD_DATUM_MEMBER(w, d, r);
  ADD_DATUM_MEMBER(w, d, g);
  ADD_DATUM_MEMBER(w, d, b);
}

//==============================================================================

struct test_configuration {
  bool enabled = true;

  // number of increments each channel will go through
  int offset_steps = 16;

  // each channel will be incremented by (i / offset_steps) * offset_scale
  // resulting in offset values from [0, offset_scale]
  float offset_scale = 255;
};

JSON_CONVERTER(test_configuration) {
  JSON_OPTVAR(enabled);
  JSON_REQVAR(offset_steps);
  JSON_REQVAR(offset_scale);
}

struct configuration {
  test_configuration vertex_stage_configuration;
  test_configuration fragment_stage_configuration;
  test_configuration varying_configuration;
};

JSON_CONVERTER(configuration) {
  JSON_REQVAR(vertex_stage_configuration);
  JSON_REQVAR(fragment_stage_configuration);
  JSON_REQVAR(varying_configuration);
}

//------------------------------------------------------------------------------

struct result {
  // name of test, one of TestNames[]
  std::string test;

  // did the framebuffer read differ from expected value
  bool failure = false;

  // was the value used "big enough" to expect mediump float error
  bool expected_failure = false;

  // expected value
  rgb_u8 expected_rgb8;

  // value read from framebuffer
  rgb_u8 actual_rgb8;

  float offset;

  float squared_magnitude;
};

void WriteDatum(report_writers::Struct w, const result& d) {
  ADD_DATUM_MEMBER(w, d, test);
  ADD_DATUM_MEMBER(w, d, failure);
  ADD_DATUM_MEMBER(w, d, expected_failure);
  ADD_DATUM_MEMBER(w, d, expected_rgb8);
  ADD_DATUM_MEMBER(w, d, actual_rgb8);
  ADD_DATUM_MEMBER(w, d, offset);
  ADD_DATUM_MEMBER(w, d, squared_magnitude);
}

struct datum {
  result mediump_vec_normalization_result;
};

void WriteDatum(report_writers::Struct w, const datum& d) {
  ADD_DATUM_MEMBER(w, d, mediump_vec_normalization_result);
}

//------------------------------------------------------------------------------

struct ProgramState {
 private:
  GLuint _program = 0;
  GLint _uniform_loc_offset_scale = -1;

 public:
  ProgramState() = default;
  ProgramState(const ProgramState&) = delete;
  ProgramState(const ProgramState&&) = delete;

  ~ProgramState() {
    if (_program > 0) {
      glDeleteProgram(_program);
    }
  }

  bool Build(const std::string& vertFile, const std::string& fragFile) {
    auto vert_src = LoadText(vertFile);
    auto frag_src = LoadText(fragFile);
    _program = glh::CreateProgramSrc(vert_src.c_str(), frag_src.c_str());
    if (_program == 0) {
      return false;
    }
    glh::CheckGlError("created program");
    _uniform_loc_offset_scale = glGetUniformLocation(_program, "uOffsetScale");
    return true;
  }

  void Bind(float offset_scale) const {
    glUseProgram(_program);
    glUniform1f(_uniform_loc_offset_scale, offset_scale);
  }
};

struct Vertex {
  enum class Attributes : GLuint {
    Pos = 0,
    BaseColor = 1,
    ColorOffset = 2
  };

  // position
  vec3 pos;

  // base color (offset will be applied to)
  vec3 base_color;

  // offset base (normalized, will be scaled by a uniform in shader)
  vec3 color_offset;
};

struct Probe {
  // position in framebuffer to look up value from
  ivec2 pos;

  // if true, the values used are expected to
  // exceed mediump float precision
  bool failure_acceptable;

  // the expected result, pre-normalization (in range 0,BIG)
  vec3 expected_result_non_normalized;

  // the expected result, normalized (in range 0,1)
  vec3 expected_result_normalized;

  float offset;

  float squared_magnitude;
};

enum class Test : int {
  VertexStageTest = 0, FragmentStageTest, VaryingPassthroughTest
};

static const char* TestNames[] = {
    "VertexStageTest", "FragmentStageTest", "VaryingPassthroughTest"
};

static constexpr size_t NumTests = 3;

static const std::array<vec3, 7> ColorMasks = {
    vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1),
    vec3(1, 1, 0), vec3(1, 0, 1), vec3(0, 1, 1),
    vec3(1, 1, 1)
};

} // anonymous namespace

//==============================================================================

class MediumPVecNormalizationGLES3Operation : public BaseGLES3Operation {
 public:

  MediumPVecNormalizationGLES3Operation() = default;

  ~MediumPVecNormalizationGLES3Operation() {
    Cleanup();
  }

  void OnGlContextReady(const GLContextConfig& ctx_config) override {
    Log::D(TAG, "GlContextReady");
    _configuration = GetConfiguration<configuration>();

    _pixel_capture = std::make_unique<GLPixelBuffer>();

    _currentTest = 0;
    _test_configurations = {
        _configuration.vertex_stage_configuration,
        _configuration.fragment_stage_configuration,
        _configuration.varying_configuration,
    };

    _program_states[static_cast<size_t>(Test::VertexStageTest)].Build(
        "Shaders/MediumPVecNormalizationGLES3Operation/vertex_stage_normalization.vsh",
        "Shaders/MediumPVecNormalizationGLES3Operation/vertex_stage_normalization.fsh"
    );

    _program_states[static_cast<size_t>(Test::FragmentStageTest)].Build(
        "Shaders/MediumPVecNormalizationGLES3Operation/fragment_stage_normalization.vsh",
        "Shaders/MediumPVecNormalizationGLES3Operation/fragment_stage_normalization.fsh"
    );

    _program_states[static_cast<size_t>(Test::VaryingPassthroughTest)].Build(
        "Shaders/MediumPVecNormalizationGLES3Operation/varying_passthrough.vsh",
        "Shaders/MediumPVecNormalizationGLES3Operation/varying_passthrough.fsh"
    );

    SetHeartbeatPeriod(1s);
    UpdateTestPattern();

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
  }

  void Draw(double delta_seconds) override {
    BaseGLES3Operation::Draw(delta_seconds);

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (_num_indices > 0) {
      const auto& program = _program_states[static_cast<size_t>(_currentTest)];
      const auto& config = _test_configurations[_currentTest];
      program.Bind(config.offset_scale);

      glBindVertexArray(_vbo_state);
      glDrawElements(GL_TRIANGLES, _num_indices, GL_UNSIGNED_SHORT, nullptr);

      if (_frames_drawn++ == ReadPixelsOnFrame) {
        ReadCurrentTestPattern();
      }
    }
  }

  void OnHeartbeat(Duration elapsed) override {
    // select next test, or stop if we've exhausted the tests
    if (_currentColorMask < ColorMasks.size() - 1) {
      _currentColorMask++;
    } else {
      _currentTest++;
      _currentColorMask = 0;
      if (_currentTest >= NumTests) {
        Log::D(TAG, "Exhausted test permutations, finishing...");
        Stop();
        return;
      }
    }

    if (!IsStopped()) {
      UpdateTestPattern();
    }
  }

 private:

  void UpdateTestPattern() {
    _frames_drawn = 0;
    CreateTestPattern(_test_configurations[_currentTest],
                      ColorMasks[_currentColorMask]);
  }

  void ReadCurrentTestPattern() {
    ReadTestPattern(_test_configurations[_currentTest],
                    ColorMasks[_currentColorMask]);
  }

  void CreateTestPattern(test_configuration conf, vec3 base_color) {
    // clean up previous test pattern
    Cleanup();

    Log::D(TAG,
           "CreatePattern[%s] - _currentTest: %d _currentColorMask: %d",
           TestNames[_currentTest],
           _currentTest,
           _currentColorMask);

    const int width = GetGlContextSize().x;
    const int height = GetGlContextSize().y;
    const int slices = conf.offset_steps;
    const float col_width = 1.0F / slices;
    const float offset_step = 1.0F / (slices - 1);

    std::vector<Vertex> vertices;
    std::vector<GLushort> indices;

    for (int i = 0; i < slices; i++) {
      // NOTE: We're generating vertices in clip space (-1:1)
      const float z = 0;
      const float left = -1 + 2 * i * col_width;
      const float right = left + 2 * col_width;
      const float top = 1;
      const float bottom = -1;

      const auto v_a = vec3(left, bottom, z);
      const auto v_b = vec3(left, top, z);
      const auto v_c = vec3(right, top, z);
      const auto v_d = vec3(right, bottom, z);

      const auto offset_color = vec3(base_color.r * i * offset_step,
                                     base_color.g * i * offset_step,
                                     base_color.b * i * offset_step);

      auto offset = vertices.size();
      vertices.push_back(Vertex{v_a, base_color, offset_color});
      vertices.push_back(Vertex{v_b, base_color, offset_color});
      vertices.push_back(Vertex{v_c, base_color, offset_color});
      vertices.push_back(Vertex{v_d, base_color, offset_color});

      indices.push_back(static_cast<unsigned short>(offset + 0));
      indices.push_back(static_cast<unsigned short>(offset + 1));
      indices.push_back(static_cast<unsigned short>(offset + 2));

      indices.push_back(static_cast<unsigned short>(offset + 0));
      indices.push_back(static_cast<unsigned short>(offset + 2));
      indices.push_back(static_cast<unsigned short>(offset + 3));

      // create a probe using the centroid of the quad
      // and truth values for testing
      const float middle_x = (left + right) * 0.5F;
      const float across_x = (middle_x + 1) * 0.5F; // remap from (-1,1)->(0,1)
      const int middle_x_px = static_cast<int>(floorf(across_x * width));

      const float middle_y = (top + bottom) * 0.5F;
      const float across_y = (middle_y + 1) * 0.5F; // remap from (-1,1)->(0,1)
      const int middle_y_px = static_cast<int>(floorf(across_y * height));

      const auto summed = base_color + (conf.offset_scale * offset_color);
      const auto expected = normalize(summed);

      /*
       * Normalizing a vec3 requires (in principle) something like:
       * float t = (x*x) + (y*y) + (z*z)
       * float normalized = sqrt(t)
       * Using a mediump precision simulator, I found that when t > 65536
       * (an auspicious number) the error of the sqrt() passes from < 1 to > 1
       * https://oletus.github.io/float16-simulator.js/
       */
      const auto squared_magnitude =
          (summed.r * summed.r) + (summed.g * summed.g) + (summed.b * summed.b);
      const auto failure_acceptable = squared_magnitude >= 65536;

      Log::D(TAG, "generating pattern for %d slices; base_color(%.3f,%.3f,%.3f)"
                  " offset_color(%.3f,%.3f,%.3f) summed(%.3f,%.3f,%.3f)"
                  " expected(%.3f,%.3f,%.3f) failure_acceptable: %s",
             slices, base_color.r, base_color.g, base_color.b,
             offset_color.r, offset_color.g, offset_color.b,
             summed.r, summed.g, summed.b,
             expected.r, expected.g, expected.b,
             failure_acceptable ? "true" : "false");

      // generate a probe to read in ReadTestPattern()
      _probes.push_back(Probe{
          ivec2(middle_x_px, middle_y_px),
          failure_acceptable,
          summed,
          expected,
          (i * offset_step) * conf.offset_scale,
          squared_magnitude
      });
    }

    // generate storage for new test pattern
    _num_indices = static_cast<GLsizei>(indices.size());

    glGenBuffers(1, &_vertex_vbo_id);
    glBindBuffer(GL_ARRAY_BUFFER, _vertex_vbo_id);
    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(Vertex) * vertices.size(),
        vertices.data(),
        GL_STREAM_DRAW);

    glGenBuffers(1, &_index_vbo_id);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _index_vbo_id);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        sizeof(GLushort) * indices.size(),
        indices.data(),
        GL_STATIC_DRAW);

    glh::CheckGlError("building buffers");

    glGenVertexArrays(1, &_vbo_state);
    glBindVertexArray(_vbo_state);

    glh::CheckGlError("binding vertex array");

    glVertexAttribPointer(
        static_cast<GLuint>(Vertex::Attributes::Pos), 3, GL_FLOAT, GL_FALSE,
        sizeof(Vertex),
        (const GLvoid*) offsetof(Vertex, pos));

    glVertexAttribPointer(
        static_cast<GLuint>(Vertex::Attributes::BaseColor),
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        (const GLvoid*) offsetof(Vertex, base_color));

    glVertexAttribPointer(
        static_cast<GLuint>(Vertex::Attributes::ColorOffset),
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        (const GLvoid*) offsetof(Vertex, color_offset));

    glh::CheckGlError("setting attrib pointers");

    glEnableVertexAttribArray(static_cast<GLuint>(Vertex::Attributes::Pos));
    glEnableVertexAttribArray(static_cast<GLuint>(Vertex::Attributes::BaseColor));
    glEnableVertexAttribArray(static_cast<GLuint>(Vertex::Attributes::ColorOffset));

    glBindBuffer(GL_ARRAY_BUFFER, _vertex_vbo_id);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _index_vbo_id);
  }

  void ReadTestPattern(test_configuration conf, vec3 base_color) {
    glFinish();

    // capture our pixels
    _pixel_capture->CopyFromFramebuffer();

    // allow a delta of 1 in r, g and b (this is arbitrary but allows for
    // some wiggle room)
    const auto error_margin = 1;
    bool errors = false;
    for (auto const& probe : _probes) {
      // read value_rgb at probe, and compute delta r,g,b
      auto value_rgb = rgb_u8{
        _pixel_capture->ReadPixel(probe.pos.x, probe.pos.y)};

      const auto expected_value_rgb = rgb_u8{
        probe.expected_result_normalized};

      const auto dr =
          abs(static_cast<int>(value_rgb.r)
                  - static_cast<int>(expected_value_rgb.r));

      const auto dg =
          abs(static_cast<int>(value_rgb.g)
                  - static_cast<int>(expected_value_rgb.g));

      const auto db =
          abs(static_cast<int>(value_rgb.b)
                  - static_cast<int>(expected_value_rgb.b));

      const auto failure =
          dr > error_margin || dg > error_margin || db > error_margin;

      // report our read
      const auto r = result{
          TestNames[_currentTest],
          failure,
          probe.failure_acceptable,
          expected_value_rgb,
          value_rgb,
          probe.offset,
          probe.squared_magnitude
      };

      Report(datum{r});

      if (!probe.failure_acceptable) {
        if (failure) {
          // looks like we got an unexpected value_rgb
          Log::E(TAG,
                 "ReadPattern[%s] MISMATCH (_currentTest: %d _currentColorMask: %d)"
                 " base_color: (%f,%f,%f) offset: %f squared_magnitude: %f"
                 " expected_color_rgb: (%d,%d,%d) got (%d,%d,%d)",
                 TestNames[_currentTest],
                 _currentTest,
                 _currentColorMask,
                 base_color.r,
                 base_color.g,
                 base_color.b,
                 probe.offset,
                 probe.squared_magnitude,
                 expected_value_rgb.r,
                 expected_value_rgb.g,
                 expected_value_rgb.b,
                 value_rgb.r,
                 value_rgb.g,
                 value_rgb.b
          );
        } else {
          Log::D(TAG,
                 "ReadPattern[%s] CORRECT (_currentTest: %d _currentColorMask: %d)"
                 " base_color: (%f,%f,%f) offset: %f squared_magnitude: %f"
                 " expected_color_rgb: (%d,%d,%d) got (%d,%d,%d)",
                 TestNames[_currentTest],
                 _currentTest,
                 _currentColorMask,
                 base_color.r,
                 base_color.g,
                 base_color.b,
                 probe.offset,
                 probe.squared_magnitude,
                 expected_value_rgb.r,
                 expected_value_rgb.g,
                 expected_value_rgb.b,
                 value_rgb.r,
                 value_rgb.g,
                 value_rgb.b
          );
        }
      }
    }
  }

  void Cleanup() {
    if (_vbo_state > 0) glDeleteVertexArrays(1, &_vbo_state);
    if (_vertex_vbo_id > 0) glDeleteBuffers(1, &_vertex_vbo_id);
    if (_index_vbo_id > 0) glDeleteBuffers(1, &_index_vbo_id);
    _vbo_state = 0;
    _vertex_vbo_id = 0;
    _index_vbo_id = 0;
    _num_indices = 0;
    _probes.clear();
  }

 private:
  configuration _configuration;

  size_t _currentTest;
  size_t _currentColorMask = 0;
  std::array<ProgramState, NumTests> _program_states;
  std::array<test_configuration, NumTests> _test_configurations;

  std::vector<Probe> _probes;

  GLuint _vertex_vbo_id = 0;
  GLuint _index_vbo_id = 0;
  GLuint _vbo_state = 0;
  GLsizei _num_indices = 0;

  size_t _frames_drawn = 0;
  static constexpr size_t ReadPixelsOnFrame = 10;
  std::unique_ptr<GLPixelBuffer> _pixel_capture;
};

EXPORT_ANCER_OPERATION(MediumPVecNormalizationGLES3Operation)

#pragma clang diagnostic pop

