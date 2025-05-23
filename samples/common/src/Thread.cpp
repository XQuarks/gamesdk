/*
 * Copyright 2018 The Android Open Source Project
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

#include "Thread.h"

#include <sched.h>
#include <unistd.h>

namespace samples {

int32_t getNumCpus() {
  static int32_t sNumCpus = []() {
    pid_t pid = gettid();
    cpu_set_t cpuSet;
    CPU_ZERO(&cpuSet);
    sched_getaffinity(pid, sizeof(cpuSet), &cpuSet);

    int32_t numCpus = 0;
    while (CPU_ISSET(numCpus, &cpuSet)) {
      ++numCpus;
    }

    return numCpus;
  }();

  return sNumCpus;
}

void setAffinity(int32_t cpu) {
  cpu_set_t cpuSet;
  CPU_ZERO(&cpuSet);
  CPU_SET(cpu, &cpuSet);
  sched_setaffinity(gettid(), sizeof(cpuSet), &cpuSet);
}

void setAffinity(Affinity affinity) {
  const int32_t numCpus = getNumCpus();

  cpu_set_t cpuSet;
  CPU_ZERO(&cpuSet);
  for (int32_t cpu = 0; cpu < numCpus; ++cpu) {
    switch (affinity) {
      case Affinity::None:
        CPU_SET(cpu, &cpuSet);
        break;
      case Affinity::Even:
        if (cpu % 2 == 0) CPU_SET(cpu, &cpuSet);
        break;
      case Affinity::Odd:
        if (cpu % 2 == 1) CPU_SET(cpu, &cpuSet);
        break;
    }
  }

  sched_setaffinity(gettid(), sizeof(cpuSet), &cpuSet);
}

ThreadManager& ThreadManager::Instance() {
  static ThreadManager t;
  return t;
}

int ThreadManager::Start(SwappyThreadId* thread_id, void* (*thread_func)(void*),
                         void* user_data) {
  std::lock_guard<std::mutex> lock(threadMapMutex);
  pthread_t thread;
  int result = pthread_create(&thread, NULL, thread_func, user_data);
  if (result == 0) {
    auto id = nextId++;
    threads.insert({id, thread});
    *thread_id = id;
    return 0;
  }
  return -1;
}

void ThreadManager::Join(SwappyThreadId thread_id) {
  std::lock_guard<std::mutex> lock(threadMapMutex);
  auto it = threads.find(thread_id);
  if (it != threads.end()) {
    void* retval;
    pthread_join(it->second, &retval);
    threads.erase(it);
  }
}

bool ThreadManager::Joinable(SwappyThreadId thread_id) {
  std::lock_guard<std::mutex> lock(threadMapMutex);
  auto it = threads.find(thread_id);
  return it != threads.end();
}

std::atomic<SwappyThreadId> ThreadManager::nextId(0);

}  // namespace samples
