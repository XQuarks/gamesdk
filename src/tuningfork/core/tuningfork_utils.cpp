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

#include "tuningfork_utils.h"

#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <sstream>

#include "proto/protobuf_util.h"
// Unfortunately we can't use std::filesystem until we move to C++17
#include <dirent.h>

#define LOG_TAG "TuningFork"
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

#include "Log.h"
#include "jni/jni_wrap.h"

namespace tuningfork {

std::string Base16(const std::vector<unsigned char>& bytes) {
    static char hex_char[] = "0123456789abcdef";
    std::string s(bytes.size() * 2, ' ');
    auto q = s.begin();
    for (unsigned char b : bytes) {
        *q++ = hex_char[b >> 4];
        *q++ = hex_char[b & 0xf];
    }
    return s;
}

namespace apk_utils {

class NativeAsset {
    AAsset* asset;

   public:
    NativeAsset(const char* name) {
        auto java_asset_manager = gamesdk::jni::AppContext().getAssets();
        AAssetManager* mgr = AAssetManager_fromJava(
            gamesdk::jni::Env(), (jobject)java_asset_manager.obj_);
        asset = AAssetManager_open(mgr, name, AASSET_MODE_BUFFER);
        if (asset == nullptr) {
            ALOGW("Can't find %s in APK", name);
        }
    }
    NativeAsset(NativeAsset&& a) : asset(a.asset) { a.asset = nullptr; }
    NativeAsset& operator=(NativeAsset&& a) {
        asset = a.asset;
        a.asset = nullptr;
        return *this;
    }
    NativeAsset(const NativeAsset& a) = delete;
    NativeAsset& operator=(const NativeAsset& a) = delete;
    ~NativeAsset() {
        if (asset != nullptr) {
            AAsset_close(asset);
        }
    }
    bool IsValid() const { return asset != nullptr; }
    operator AAsset*() { return asset; }
};

bool GetAssetAsSerialization(const char* name, ProtobufSerialization& out) {
    NativeAsset asset(name);
    if (!asset.IsValid()) return false;
    uint64_t size = AAsset_getLength64(asset);
    out.resize(size);
    memcpy(const_cast<uint8_t*>(out.data()), AAsset_getBuffer(asset), size);
    return true;
}

// Get the app's version code. Also fills packageNameStr with the package name
//  if it is non-null.
int GetVersionCode(std::string* packageNameStr, uint32_t* gl_es_version) {
    using namespace gamesdk::jni;
    auto app_context = AppContext();
    auto pm = app_context.getPackageManager();
    CHECK_FOR_JNI_EXCEPTION_AND_RETURN(0);
    std::string package_name = app_context.getPackageName().C();
    CHECK_FOR_JNI_EXCEPTION_AND_RETURN(0);
    auto package_info = pm.getPackageInfo(package_name, 0x0);
    CHECK_FOR_JNI_EXCEPTION_AND_RETURN(0);
    if (packageNameStr != nullptr) {
        *packageNameStr = package_name;
    }
    auto code = package_info.versionCode();
    CHECK_FOR_JNI_EXCEPTION_AND_RETURN(0);
    if (gl_es_version != nullptr) {
        auto features = pm.getSystemAvailableFeatures();
        CHECK_FOR_JNI_EXCEPTION_AND_RETURN(0);
        for (auto& f : features) {
            if (f.name.empty()) {
                if (f.reqGlEsVersion != android::content::pm::FeatureInfo::
                                            GL_ES_VERSION_UNDEFINED) {
                    *gl_es_version = f.reqGlEsVersion;
                } else {
                    *gl_es_version =
                        1;  // Lack of property means OpenGL ES version 1
                }
            }
        }
        ALOGI("OpenGL version %d.%d ", ((*gl_es_version) >> 16),
              ((*gl_es_version) & 0x0000ffff));
    }
    return code;
}

std::string GetSignature() {
    using namespace gamesdk::jni;
    auto app_context = AppContext();
    auto pm = app_context.getPackageManager();
    CHECK_FOR_JNI_EXCEPTION_AND_RETURN("");
    auto package_name = app_context.getPackageName();
    CHECK_FOR_JNI_EXCEPTION_AND_RETURN("");
    auto package_info = pm.getPackageInfo(
        package_name.C(), android::content::pm::PackageManager::GET_SIGNATURES);
    CHECK_FOR_JNI_EXCEPTION_AND_RETURN("");
    if (!package_info.valid()) return "";
    auto sigs = package_info.signatures();
    CHECK_FOR_JNI_EXCEPTION_AND_RETURN("");
    if (sigs.size() == 0) return "";
    auto sig = sigs[0];
    java::security::MessageDigest md("SHA1");
    CHECK_FOR_JNI_EXCEPTION_AND_RETURN("");
    auto padded_sig = md.digest(sigs[0]);
    CHECK_FOR_JNI_EXCEPTION_AND_RETURN("");
    return Base16(padded_sig);
}

bool GetDebuggable() {
    using namespace gamesdk::jni;
    if (!gamesdk::jni::IsValid()) return false;
    auto app_context = AppContext();
    auto pm = app_context.getPackageManager();
    CHECK_FOR_JNI_EXCEPTION_AND_RETURN(false);
    auto package_name = app_context.getPackageName();
    CHECK_FOR_JNI_EXCEPTION_AND_RETURN(false);
    auto package_info = pm.getPackageInfo(package_name.C(), 0);
    CHECK_FOR_JNI_EXCEPTION_AND_RETURN(false);
    if (!package_info.valid()) return false;
    auto application_info = package_info.applicationInfo();
    CHECK_FOR_JNI_EXCEPTION_AND_RETURN(false);
    if (!application_info.valid()) return false;
    return application_info.flags() &
           android::content::pm::ApplicationInfo::FLAG_DEBUGGABLE;
}

}  // namespace apk_utils

namespace file_utils {

// Creates the directory if it does not exist. Returns true if the directory
//  already existed or could be created.
bool CheckAndCreateDir(const std::string& path) {
    struct stat sb;
    int32_t res = stat(path.c_str(), &sb);
    if (0 == res && sb.st_mode & S_IFDIR) {
        ALOGV("Directory %s already exists", path.c_str());
        return true;
    } else if (ENOENT == errno) {
        ALOGI("Creating directory %s", path.c_str());
        res = mkdir(path.c_str(), 0770);
        if (res != 0)
            ALOGW("Error creating directory %s: %d", path.c_str(), res);
        return res == 0;
    }
    return false;
}
bool FileExists(const std::string& fname) {
    struct stat buffer;
    return (stat(fname.c_str(), &buffer) == 0);
}
std::string GetAppCacheDir() {
    gamesdk::jni::String path =
        gamesdk::jni::AppContext().getCacheDir().getPath();
    return path.C();
}
bool DeleteFile(const std::string& path) {
    if (FileExists(path)) return remove(path.c_str()) == 0;
    return true;
}
bool DeleteDir(const std::string& path) {
    // TODO(willosborn): It would be much better to use
    // std::filesystem::remove_all here
    //  if we were on C++17 or experimental/filesystem was supported on Android.
    ALOGI("DeleteDir %s", path.c_str());
    struct dirent* entry;
    auto dir = opendir(path.c_str());
    if (dir == nullptr) return DeleteFile(path);
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] != '\0' && entry->d_name[0] != '.')
            DeleteDir(path + "/" + entry->d_name);
    }
    closedir(dir);
    return true;
}

bool LoadBytesFromFile(std::string file_name,
                       TuningFork_CProtobufSerialization* params) {
    ALOGV("LoadBytesFromFile:%s", file_name.c_str());
    std::ifstream f(file_name, std::ios::binary);
    if (f.good()) {
        f.seekg(0, std::ios::end);
        params->size = f.tellg();
        params->bytes = (uint8_t*)::malloc(params->size);
        params->dealloc = TuningFork_CProtobufSerialization_Dealloc;
        f.seekg(0, std::ios::beg);
        f.read((char*)params->bytes, params->size);
        return true;
    }
    return false;
}

bool SaveBytesToFile(std::string file_name,
                     const TuningFork_CProtobufSerialization* params) {
    ALOGV("SaveBytesToFile:%s", file_name.c_str());
    std::ofstream save_file(file_name, std::ios::binary);
    if (save_file.good()) {
        save_file.write((const char*)params->bytes, params->size);
        return true;
    }
    return false;
}

}  // namespace file_utils

namespace json_utils {

using namespace json11;

std::string GetResourceName(const RequestInfo& request_info) {
    std::stringstream str;
    str << "applications/" << request_info.apk_package_name << "/apks/";
    str << request_info.apk_version_code;
    return str.str();
}

Json::object DeviceSpecJson(const RequestInfo& request_info) {
    Json gles_version = Json::object{
        {"major", static_cast<int>(request_info.gl_es_version >> 16)},
        {"minor", static_cast<int>(request_info.gl_es_version & 0xffff)}};
    std::vector<double> freqs(request_info.cpu_max_freq_hz.begin(),
                              request_info.cpu_max_freq_hz.end());
    return Json::object{{"fingerprint", request_info.build_fingerprint},
                        {"total_memory_bytes",
                         static_cast<double>(request_info.total_memory_bytes)},
                        {"build_version", request_info.build_version_sdk},
                        {"gles_version", gles_version},
                        {"cpu_core_freqs_hz", freqs},
                        {"model", request_info.model},
                        {"brand", request_info.brand},
                        {"product", request_info.product},
                        {"device", request_info.device}};
}

}  // namespace json_utils

std::string UniqueId() {
    using namespace gamesdk::jni;
    return java::util::UUID::randomUUID().toString().C();
}

static const uint64_t kNanosecondsPerMillisecond = 1000 * 1000;
static const uint64_t kMillisecondsPerSecond = 1000;

Duration GetElapsedTimeSinceBoot() {
    struct timespec ts;
    int err = clock_gettime(CLOCK_BOOTTIME, &ts);
    if (err != 0) {
        // This should never happen, but just in case ...
        ALOGE("clock_gettime(CLOCK_BOOTTIME) failed: %s", strerror(errno));
        return std::chrono::milliseconds(0);
    }
    return std::chrono::milliseconds(
        uint64_t(ts.tv_sec) * kMillisecondsPerSecond +
        uint64_t(ts.tv_nsec) / kNanosecondsPerMillisecond);
}
Duration GetProcessStartTimeSinceBoot() {
    const int kProcStartTimeIndex =
        21;  // index in space-separated list from proc/*/stat
    std::stringstream filename;
    int procid = getpid();
    long ClockHz = sysconf(_SC_CLK_TCK);  // Clock tick freq in Hz
    filename << "/proc/" << procid << "/stat";
    std::ifstream f(filename.str().c_str());
    long dtime_ms = 0;
    if (f.good()) {
        std::stringstream str;
        str << f.rdbuf();
        std::string item;
        std::vector<std::string> items;
        f.seekg(0);
        while (std::getline(f, item, ' ')) {
            items.push_back(item);
        }
        if (items.size() > kProcStartTimeIndex) {
            auto stime = items[kProcStartTimeIndex];
            std::stringstream pstr(stime);
            uint64_t stime_ticks;
            pstr >> stime_ticks;
            return std::chrono::milliseconds(
                (stime_ticks * kMillisecondsPerSecond) / ClockHz);
        }
    }
    return std::chrono::milliseconds(0);
}

Duration GetTimeSinceProcessStart() {
    auto etime = GetElapsedTimeSinceBoot();
    auto ptime = GetProcessStartTimeSinceBoot();
    if (etime.count() == 0 || ptime.count() == 0)
        return std::chrono::milliseconds(0);
    else
        return etime - ptime;
}

}  // namespace tuningfork
