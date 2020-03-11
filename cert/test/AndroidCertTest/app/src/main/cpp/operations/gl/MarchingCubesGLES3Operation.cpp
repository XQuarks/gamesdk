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
 * MarchingCubesGLES3Operation
 *
 * This is an experimental platform to aid in finding best practices for
 * maintaining high performance in (potentially-multithreaded) CPU/RAM
 * workloads over time while minimizing consequences of thermal throttling.
 *
 * The test uses the marching cubes algorithm to hammer the CPU heavily via a
 * job queue; it has a ton of non-cache-friendly memory lookups, and streams
 * vertices to the GPU.
 *
 * Input:
 *
 * configuration:
 *  warm_up_time:[Duration] how long to run before start measuring perf data
 *  thread_affinity:[ThreadAffinitySetup enum], one of:
 *    OneBigCore: pin work to a single big core
 *    OneLittleCore: pin work to a single little core
 *    AllBigCores: job queue sets thread affinity to big cores
 *    AllLittleCores: job queue sets thread affinity to little cores
 *    AllCores: job queue just uses vanilla unpinned threads without core type affinity
 *  thread_pinned: if true, threads are pinned to whatever core they are running on
 *  job_batching_setup: [JobBatchingSetup enum] one of:
 *    OneNodePerJob: naive job scheduling, each job gets one work unit with
 *      no attempt to balance load or do anything otherwsie "clever"
 *    ManyNodesPerJob: minimally clever job scheduler; does several chunks
 *      of work per work unit
 *    AutoBalancedNodesPerJob: creates one job unit per hardware thread and
 *      attempts to distribute their work to evenly balance computational expense
 *      (to hopefully avoid scenario where some threads get "easy" jobs and
 *      finish early while other threads are burdoned with all the "heavy" jobs)
 *    AutoQueuedNodesPerJob: inverts the queue such that each worker takes the
 *      next available job, but fixes the worker count to num hardware threads
 *   sleep_config:
 *      period:[Duration] worker threads are slept after this much work completed
 *      duration:[Duration] how long a worker thread is allowed to sleep after
 *        completing a work period
 *      method:[WaitMethod enum] one of:
 *        None: No sleep; threads will run full bore without sleeping
 *        Sleep: threads will sleep using std::this_thread::sleep()
 *        Spilock: threads will sleep using a simple spinlock with a yeild
 *
 * Output:
 *
 * datum:
 *  marching_cubes_permutation_results:
 *    exec_configuration: (copy of input configuration)
 *    num_threads_used: [int] number of threads used to do work
 *    num_iterations:[int] number of times the volume was marched
 *    min_vps:[double] minimum voxels-per-second during run
 *    max_vps:[double] max voxels-per-second during run
 *    average_vps:[double] average voxels-per-second during run
 *    median_vps:[double] median voxels-per-second during run
 *    fifth_percentile_vps:[double] 5th percentile voxels-per-second during run
 *    twentyfifth_percentile_vps:[double] 25th percentile voxels-per-second during run
 *    seventyfifth_percentile_vps:[double] 75th percentile voxels-per-second during run
 *    ninetyfifth_percentile_vps:[double] 95th percentile voxels-per-second during run
 *
 */


#include <array>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <random>
#include <thread>

#include <ancer/BaseGLES3Operation.hpp>
#include <ancer/DatumReporting.hpp>
#include <ancer/util/GLHelpers.hpp>
#include <ancer/util/Json.hpp>
#include <ancer/util/ThreadSyncPoint.hpp>
#include <ancer/util/UnownedPtr.hpp>

#include "operations/gl/marching_cubes/MarchingCubes.hpp"
#include "operations/gl/marching_cubes/Volume.hpp"
#include "operations/gl/marching_cubes/VolumeSamplers.hpp"
#include "operations/gl/marching_cubes/Demos.hpp"

using std::make_unique;
using std::unique_ptr;
using namespace ancer;
namespace mc = marching_cubes;

namespace {

//
//  Constants
//

constexpr Log::Tag TAG{"MarchingCubesGLES3Operation"};
constexpr float NEAR_PLANE = 0.1f;
constexpr float FAR_PLANE = 1000.0f;
constexpr float FOV_DEGREES = 50.0F;
constexpr float OCTREE_NODE_VISUAL_INSET_FACTOR = 0.0025F;

//
//  Configuration
//

enum class ThreadAffinitySetup {
  OneBigCore, OneLittleCore, AllBigCores, AllLittleCores, AllCores
};

constexpr const char* ThreadAffinitySetupNames[] = {
    "OneBigCore", "OneLittleCore", "AllBigCores", "AllLittleCores", "AllCores"
};

enum class JobBatchingSetup {
  OneNodePerJob, ManyNodesPerJob, AutoBalancedNodesPerJob, AutoQueuedNodesPerJob
};

constexpr const char* JobBatchingSetupNames[] = {
    "OneNodePerJob", "ManyNodesPerJob", "AutoBalancedNodesPerJob",
    "AutoQueuedNodesPerJob"
};

enum class WaitMethod {
  None, Sleep, Spinlock
};

constexpr const char* WaitMethodNames[] = {
    "None", "Sleep", "Spinlock"
};

struct sleep_configuration {
  Duration period;
  Duration duration;
  WaitMethod method;

  MarchingCubesThreadPool::SleepConfig ToSleepConfig() const {
    const auto mthd = [=](){
      switch (method){
        case WaitMethod::None:
          return MarchingCubesThreadPool::SleepConfig::Method::None;
        case WaitMethod::Sleep:
          return MarchingCubesThreadPool::SleepConfig::Method::Sleep;
        case WaitMethod::Spinlock:
          return MarchingCubesThreadPool::SleepConfig::Method::Spinlock;
      }
    }();
    return MarchingCubesThreadPool::SleepConfig{period, duration, mthd };
  }
};

JSON_CONVERTER(sleep_configuration) {
  JSON_REQVAR(period);
  JSON_REQVAR(duration);
  JSON_REQENUM(method, WaitMethodNames);
}

struct configuration {
  Duration warm_up_time = 5s;
  ThreadAffinitySetup thread_affinity = ThreadAffinitySetup::AllBigCores;
  bool thread_pinned = true;
  JobBatchingSetup
      job_batching_setup = JobBatchingSetup::AutoBalancedNodesPerJob;
  sleep_configuration sleep_config;
};

JSON_CONVERTER(configuration) {
  JSON_OPTVAR(warm_up_time);
  JSON_REQENUM(thread_affinity, ThreadAffinitySetupNames);
  JSON_REQVAR(thread_pinned);
  JSON_REQENUM(job_batching_setup, JobBatchingSetupNames);
  JSON_REQVAR(sleep_config);
}

//
// Helper functions
//

constexpr auto ToAffinity(ThreadAffinitySetup setup) {
  switch (setup) {
    case ThreadAffinitySetup::OneBigCore:
    case ThreadAffinitySetup::AllBigCores:return ThreadAffinity::kBigCore;
    case ThreadAffinitySetup::OneLittleCore:
    case ThreadAffinitySetup::AllLittleCores:return ThreadAffinity::kLittleCore;
    case ThreadAffinitySetup::AllCores: return ThreadAffinity::kAll;
  }
}

//
//  Reporting Data
//

struct result {
  configuration exec_configuration;
  size_t num_threads_used = 0;
  size_t num_iterations = 0;
  double min_vps = 0;
  double max_vps = 0;
  double average_vps = 0;
  double median_vps = 0;
  double fifth_percentile_vps = 0;
  double twentyfifth_percentile_vps = 0;
  double seventyfifth_percentile_vps = 0;
  double ninetyfifth_percentile_vps = 0;
};

struct datum {
  result marching_cubes_permutation_results;
};

void WriteDatum(report_writers::Struct w, const sleep_configuration& c) {
  ADD_DATUM_MEMBER(w, c, period);
  ADD_DATUM_MEMBER(w, c, duration);
  w.AddItem("method", WaitMethodNames[(int)c.method]);
}

void WriteDatum(report_writers::Struct w, const configuration& c) {
  ADD_DATUM_MEMBER(w, c, warm_up_time);
  ADD_DATUM_MEMBER(w, c, thread_pinned);
  w.AddItem("thread_setup", ThreadAffinitySetupNames[(int)c.thread_affinity]);
  w.AddItem("job_batching_setup", JobBatchingSetupNames[(int)c.job_batching_setup]);
  ADD_DATUM_MEMBER(w, c, sleep_config);
}

void WriteDatum(report_writers::Struct w, const result& r) {
  ADD_DATUM_MEMBER(w, r, exec_configuration);
  ADD_DATUM_MEMBER(w, r, num_threads_used);
  ADD_DATUM_MEMBER(w, r, num_iterations);
  ADD_DATUM_MEMBER(w, r, min_vps);
  ADD_DATUM_MEMBER(w, r, max_vps);
  ADD_DATUM_MEMBER(w, r, average_vps);
  ADD_DATUM_MEMBER(w, r, median_vps);
  ADD_DATUM_MEMBER(w, r, fifth_percentile_vps);
  ADD_DATUM_MEMBER(w, r, twentyfifth_percentile_vps);
  ADD_DATUM_MEMBER(w, r, seventyfifth_percentile_vps);
  ADD_DATUM_MEMBER(w, r, ninetyfifth_percentile_vps);
}

void WriteDatum(report_writers::Struct w, const datum& d) {
  ADD_DATUM_MEMBER(w, d, marching_cubes_permutation_results);
}

} // anonymous namespace

//==============================================================================

class MarchingCubesGLES3Operation : public BaseGLES3Operation {
 public:

  MarchingCubesGLES3Operation() = default;
  ~MarchingCubesGLES3Operation() = default;

  void OnGlContextReady(const GLContextConfig& ctx_config) override {
    _configuration = GetConfiguration<configuration>();

    {
      auto vert_file = "Shaders/MarchingCubesGLES3Operation/volume.vsh";
      auto frag_file = "Shaders/MarchingCubesGLES3Operation/volume.fsh";
      if (!_volume_program.Build(vert_file, frag_file)) {
        Stop();
        return;
      }
    }

    {
      auto vert_file = "Shaders/MarchingCubesGLES3Operation/line.vsh";
      auto frag_file = "Shaders/MarchingCubesGLES3Operation/line.fsh";
      if (!_line_program.Build(vert_file, frag_file)) {
        Stop();
        return;
      }
    }

    glClearColor(0.2F, 0.2F, 0.22F, 0.0F);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    BuildExecConfiguration();
  }

  void OnGlContextResized(int width, int height) override {
    BaseOperation::OnGlContextResized(width, height);
    _aspect = static_cast<float>(width) / static_cast<float>(height);
  }

  void Draw(double delta_seconds) override {
    BaseGLES3Operation::Draw(delta_seconds);

    Step(delta_seconds);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const auto mvp = MVP();

    _volume_program.Bind(mvp, _model);

    glDepthMask(GL_TRUE);
    for (auto& tc : _triangle_consumers) {
      tc->Draw();
    }

    glDepthMask(GL_FALSE);
    _line_program.Bind(mvp, _model);
    _node_aabb_line_buffer.Draw();

    glDepthMask(GL_TRUE);
  }

  void Step(double delta_seconds) {
    _animation_time += static_cast<float>(delta_seconds);
    if (_volume && _current_demo) {

      // make camera orbit about y, with a gently bobbing up and down
      float orbit_y = _animation_time * glm::pi<float>() * 0.125F;
      float orbit_tilt_phase = _animation_time * glm::pi<float>() * 0.0625F;
      float orbit_tilt = sin(orbit_tilt_phase) * glm::pi<float>() * 0.125F;
      _trackball_rotation =
          glm::rotate(glm::rotate(mat4{1}, orbit_tilt, vec3(1, 0, 0)),
                      orbit_y,
                      vec3(0, 1, 0));

      // update demo animation cycle
      _current_demo->Step(_animation_time);

      // update geometry for rendering
      MarchVolume();
    }
  }

  void Stop() override {
    BaseGLES3Operation::Stop();
    ReportPerformanceData();
  }

 private:

  /*
   * Create a model-view-projection matrix for rendering the current frame
   */
  mat4 MVP() const {
    // extract trackball Z and Y for building view matrix via lookAt
    auto trackball_y =
        glm::vec3{_trackball_rotation[0][1], _trackball_rotation[1][1],
                  _trackball_rotation[2][1]};
    auto trackball_z =
        glm::vec3{_trackball_rotation[0][2], _trackball_rotation[1][2],
                  _trackball_rotation[2][2]};
    mat4 view, proj;

    if (_use_ortho_projection) {
      auto bounds = _volume->GetBounds();
      auto size = length(bounds.Size());

      auto scaleMin = 0.1F;
      auto scaleMax = 5.0F;
      auto scale = mix(scaleMin, scaleMax, pow(_dolly, 2.5F));

      auto width = scale * _aspect * size;
      auto height = scale * size;

      auto distance = FAR_PLANE / 2;
      view = lookAt(-distance * trackball_z, vec3(0), trackball_y);

      proj = glm::ortho(-width / 2,
                        width / 2,
                        -height / 2,
                        height / 2,
                        NEAR_PLANE,
                        FAR_PLANE);
    } else {
      auto bounds = _volume->GetBounds();
      auto minDistance = 0.1F;
      auto maxDistance = length(bounds.Size()) * 2;

      auto distance = mix(minDistance, maxDistance, pow<float>(_dolly, 2));
      view = lookAt(-distance * trackball_z, vec3(0), trackball_y);

      proj = glm::perspective(radians(FOV_DEGREES),
                              _aspect,
                              NEAR_PLANE,
                              FAR_PLANE);
    }
    return proj * view * _model;
  }

  void ReportPerformanceData() {
    if (_march_performance_data.empty()) {
      Log::E(TAG, "ReportPerformanceData - _march_durations is empty");
      return;
    }

    // transform our perf datums into an array of voxels per second
    std::vector<double> voxels_per_second;
    voxels_per_second.resize(_march_performance_data.size());
    std::transform(_march_performance_data.begin(),
                   _march_performance_data.end(), voxels_per_second.begin(),
                   [](const PerfDatum& d) {
                     return d.VoxelsPerSecond();
                   });

    // compute the average
    const auto avg_vps = std::accumulate(
        voxels_per_second.begin(), voxels_per_second.end(), 0.0
    ) / voxels_per_second.size();

    // compute min/max/median
    std::sort(voxels_per_second.begin(), voxels_per_second.end());
    const auto min_vps = voxels_per_second.front();
    const auto max_vps = voxels_per_second.back();

    const auto median_vps = [&voxels_per_second]() {
      if (voxels_per_second.size() % 2 == 1) {
        return voxels_per_second[voxels_per_second.size() / 2];
      } else {
        size_t center = voxels_per_second.size() / 2;
        return (voxels_per_second[std::max<size_t>(center - 1, 0)]
            + voxels_per_second[center]) / 2;
      }
    }();

    // compute percentiles
    auto find_percentile = [&voxels_per_second](int percentile) -> double {
      percentile = min(max(percentile, 0), 100);
      auto fractional_percentile = static_cast<double>(percentile) / 100.0;
      auto idx = fractional_percentile * (voxels_per_second.size() - 1);
      auto fractional = fract(idx);
      if (fractional > 0.0) {
        // if we landed between two indices, use the latter
        return voxels_per_second[static_cast<size_t>(ceil(idx))];
      } else {
        // if we landed on an index, use the average of it and the one following
        auto i_idx = static_cast<size_t>(floor(idx));
        auto j_idx = min(i_idx + 1, voxels_per_second.size() - 1);
        return (voxels_per_second[i_idx] + voxels_per_second[j_idx]) / 2;
      }
    };

    const auto fifth_percentile_vps = find_percentile(5);
    const auto twentyfifth_percentile_vps = find_percentile(25);
    const auto seventyfifth_percentile_vps = find_percentile(75);
    const auto ninetyfifth_percentile_vps = find_percentile(95);

    Report(datum{result{
        _configuration,
        _num_threads_used,
        _march_performance_data.size(),
        min_vps,
        max_vps,
        avg_vps,
        median_vps,
        fifth_percentile_vps,
        twentyfifth_percentile_vps,
        seventyfifth_percentile_vps,
        ninetyfifth_percentile_vps
    }});

    _march_performance_data.clear();
  }

  void BuildExecConfiguration() {

    //
    //  Create the job queue with our configuration's
    //  affinity, pinning, count
    //
    auto affinity = ToAffinity(_configuration.thread_affinity);
    auto pinned = _configuration.thread_pinned;
    auto max_thread_count = NumCores(affinity);

    if (_configuration.thread_affinity == ThreadAffinitySetup::OneBigCore ||
        _configuration.thread_affinity == ThreadAffinitySetup::OneLittleCore) {
      max_thread_count = 1;
    }

    auto sleep_config = _configuration.sleep_config.ToSleepConfig();

    _job_queue = std::make_unique<mc::job::MarchingCubesJobQueue>(
        affinity, pinned, max_thread_count, sleep_config);

    _num_threads_used = _job_queue->NumThreads();

    //
    //  We need ONE ITriangleConsumer per thread, so that each thread
    //  processing the volume can safely write to its dedicated store.
    //  We own the triangle consumers, so we make unowned_ptrs to hand
    //  to the _volume
    //

    std::vector<unowned_ptr<mc::ITriangleConsumer>> tcs;
    _triangle_consumers.clear();
    for (auto i = 0; i < _num_threads_used; i++) {
      auto tc = make_unique<mc::TriangleConsumer>();
      tcs.emplace_back(tc.get());
      _triangle_consumers.push_back(std::move(tc));
    }

    Log::I(TAG,
           "Using %d %s threads (%s); sleep period: %d ns dur: %d ns (%s); batching: %s",
           _num_threads_used,
           pinned ? "pinned" : "floating",
           ThreadAffinitySetupNames[static_cast<int>(_configuration.thread_affinity)],
           _configuration.sleep_config.period,
           _configuration.sleep_config.duration,
           WaitMethodNames[static_cast<int>(_configuration.sleep_config.method)],
           JobBatchingSetupNames[static_cast<int>(_configuration.job_batching_setup)]
    );

    //
    //  Build the volume and demo to render into it
    //

    _volume = make_unique<mc::OctreeVolume>(64, 1.0F, 4, _job_queue.get(), tcs);
    _model = glm::translate(mat4{1}, -vec3(_volume->GetBounds().Center()));

    _current_demo = make_unique<mc::demos::CompoundShapesDemo>(10, 10);
    _current_demo->Build(_volume.get());

    //
    //  Trigger a warmup phase for the next test pass
    //

    _warming_up = true;
    _recorded_first_step_timestamp = false;
  }

  /*
   * Helper for visualation of the octree - generates a unique
   * color for an octree node based on its depth
   */
  vec4 GetNodeColor(int atDepth) const {
    using namespace ancer::glh::color;
    static std::vector<vec4> nodeColors;

    auto depth = _volume->GetDepth();
    if (nodeColors.size() != depth) {
      nodeColors.clear();
      const float hueStep = 360.0F / depth;
      for (auto i = 0U; i <= depth; i++) {
        const hsv hC{i * hueStep, 0.6F, 1.0F};
        const auto rgbC = hsv2rgb(hC);
        nodeColors.emplace_back(rgbC.r,
                                rgbC.g,
                                rgbC.b,
                                mix<float>(0.6F,
                                           0.25F,
                                           static_cast<float>(i) / depth));
      }
    }

    return nodeColors[atDepth];
  }

  /*
   * March the volume, recording a PerfDatum
   */
  void MarchVolume() {

    _node_aabb_line_buffer.Clear();
    const auto start_time = SteadyClock::now();
    size_t num_voxels = 0;
    auto batch_size = 0;
    switch (_configuration.job_batching_setup) {
      case JobBatchingSetup::AutoBalancedNodesPerJob:
        batch_size = mc::OctreeVolume::BATCH_USING_BALANCED_LOAD;
        break;
      case JobBatchingSetup::AutoQueuedNodesPerJob:
        batch_size = mc::OctreeVolume::BATCH_USING_QUEUE;
        break;
      case JobBatchingSetup::OneNodePerJob:batch_size = 1;
        break;
      case JobBatchingSetup::ManyNodesPerJob:batch_size = 32;
        break;
    }

    _volume->March(false, batch_size,
                   [this, &num_voxels](mc::OctreeVolume::Node* node) {
                     {
                       // we add each node to our line buffer so we can visualize marched nodes
                       auto bounds = node->bounds;
                       bounds.Inset(
                           node->depth * OCTREE_NODE_VISUAL_INSET_FACTOR);
                       _node_aabb_line_buffer.Add(bounds,
                                                  GetNodeColor(node->depth));
                     }

                     // record # of voxels marched
                     num_voxels += static_cast<size_t>(node->bounds.Volume());
                   });

    const auto end_time = SteadyClock::now();

    // we don't record timestamps until we've been running for a while
    if (_warming_up) {
      if (!_recorded_first_step_timestamp) {
        Log::D(TAG, "Warm up starting");
        _first_step_timestamp = SteadyClock::now();
        _recorded_first_step_timestamp = true;
      } else {
        auto elapsed_since_first_step =
            SteadyClock::now() - _first_step_timestamp;
        if (elapsed_since_first_step >= _configuration.warm_up_time) {
          Log::D(TAG, "Warm up finished, will start recording perf timings");
          _warming_up = false;
        }
      }
    }

    if (!_warming_up) {
      _march_performance_data.emplace_back(end_time - start_time, num_voxels);
    }

  }

 private:

  struct ProgramState {
   private:
    GLuint _program = 0;
    GLint _uniform_loc_MVP = -1;
    GLint _uniform_loc_Model = -1;

   public:
    ProgramState() = default;
    ProgramState(const ProgramState& other) = delete;
    ProgramState(const ProgramState&& other) = delete;
    ~ProgramState() {
      if (_program > 0) {
        glDeleteProgram(_program);
      }
    }

    bool Build(const std::string& vert_file, const std::string& frag_file) {
      auto vert_src = LoadText(vert_file.c_str());
      auto frag_src = LoadText(frag_file.c_str());
      _program = glh::CreateProgramSrc(vert_src.c_str(), frag_src.c_str());
      if (_program == 0) {
        return false;
      }
      _uniform_loc_MVP = glGetUniformLocation(_program, "uMVP");
      _uniform_loc_Model = glGetUniformLocation(_program, "uModel");
      return true;
    }

    void Bind(const mat4& mvp, const mat4& model) {
      glUseProgram(_program);
      glUniformMatrix4fv(_uniform_loc_MVP, 1, GL_FALSE, value_ptr(mvp));
      glUniformMatrix4fv(_uniform_loc_Model, 1, GL_FALSE, value_ptr(model));
    }

  };

  struct PerfDatum {
   private:
    Duration _duration = Duration{0};
    size_t _num_voxels = 0;

   public:
    PerfDatum() = default;

    PerfDatum(Duration duration, size_t num_voxels)
        : _duration(duration), _num_voxels(num_voxels) {}

    PerfDatum(const PerfDatum&) = default;
    PerfDatum(PerfDatum&&) = default;

    double VoxelsPerSecond() const {
      auto elapsed_seconds = duration_cast<SecondsAs<double>>(_duration);
      return static_cast<double>(_num_voxels) / elapsed_seconds.count();
    }
  };

  // configuration state
  configuration _configuration;

  // render state
  ProgramState _volume_program, _line_program;
  bool _use_ortho_projection = false;
  mat4 _model{1};
  mat4 _trackball_rotation{1};
  float _dolly = 0.9F; // camera distance, ranges [0,1], remapped in MVP()
  float _aspect = 1; // apsect ratio of the framebuffer
  float _animation_time = 0;
  mc::LineSegmentBuffer _node_aabb_line_buffer;

  // marching cubes state
  unique_ptr<mc::job::MarchingCubesJobQueue> _job_queue;
  unique_ptr<mc::OctreeVolume> _volume;
  std::vector<unique_ptr<mc::ITriangleConsumer>> _triangle_consumers;
  unique_ptr<mc::demos::Demo> _current_demo;

  // perf recording state
  size_t _num_threads_used = 0;
  std::vector<PerfDatum> _march_performance_data;
  bool _warming_up = true;
  bool _recorded_first_step_timestamp = false;
  Timestamp _first_step_timestamp{};
};

EXPORT_ANCER_OPERATION(MarchingCubesGLES3Operation)