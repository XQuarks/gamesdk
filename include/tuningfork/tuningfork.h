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

#pragma once

#include <stdint.h>
#include <jni.h>

#define TUNINGFORK_MAJOR_VERSION 0
#define TUNINGFORK_MINOR_VERSION 4
#define TUNINGFORK_PACKED_VERSION ((TUNINGFORK_MAJOR_VERSION<<16)|(TUNINGFORK_MINOR_VERSION))

// Internal macros to generate a symbol to track TuningFork version, do not use directly.
#define TUNINGFORK_VERSION_CONCAT_NX(PREFIX, MAJOR, MINOR) PREFIX ## _ ## MAJOR ## _ ## MINOR
#define TUNINGFORK_VERSION_CONCAT(PREFIX, MAJOR, MINOR) TUNINGFORK_VERSION_CONCAT_NX(PREFIX, MAJOR, MINOR)
#define TUNINGFORK_VERSION_SYMBOL TUNINGFORK_VERSION_CONCAT(TuningFork_version, TUNINGFORK_MAJOR_VERSION, TUNINGFORK_MINOR_VERSION)

// Instrument keys 64000-65535 are reserved
enum {
    TFTICK_USERDEFINED_BASE = 0,
    TFTICK_SYSCPU = 64000,
    TFTICK_SYSGPU = 64001,
    TFTICK_SWAPPY_WAIT_TIME = 64002,
    TFTICK_SWAPPY_SWAP_TIME = 64003
};

struct CProtobufSerialization {
    uint8_t* bytes;
    size_t size;
    void (*dealloc)(struct CProtobufSerialization*);
};

// The instrumentation key identifies a tick point within a frame or a trace segment
typedef uint16_t TFInstrumentKey;
typedef uint64_t TFTraceHandle;
typedef uint64_t TFTimePoint;
typedef uint64_t TFDuration;

enum TFErrorCode {
    TFERROR_OK = 0, // No error
    TFERROR_NO_SETTINGS = 1, // No tuningfork_settings.bin found in assets/tuningfork.
    TFERROR_NO_SWAPPY = 2, // Not able to find Swappy.
    TFERROR_INVALID_DEFAULT_FIDELITY_PARAMS = 3, // fpDefaultFileNum is out of range.
    TFERROR_NO_FIDELITY_PARAMS = 4,
    TFERROR_TUNINGFORK_NOT_INITIALIZED = 5,
    TFERROR_INVALID_ANNOTATION = 6,
    TFERROR_INVALID_INSTRUMENT_KEY = 7,
    TFERROR_INVALID_TRACE_HANDLE = 8,
    TFERROR_TIMEOUT = 9,
    TFERROR_BAD_PARAMETER = 10,
    TFERROR_B64_ENCODE_FAILED = 11,
    TFERROR_JNI_BAD_VERSION = 12,
    TFERROR_JNI_BAD_THREAD = 13,
    TFERROR_JNI_BAD_ENV = 14,
    TFERROR_JNI_EXCEPTION = 15,
    TFERROR_JNI_BAD_JVM = 16,
    TFERROR_NO_CLEARCUT = 17,
    TFERROR_NO_FIDELITY_PARAMS_IN_APK = 18, // No dev_tuningfork_fidelityparams_#.bin found
                                           //  in assets/tuningfork.
    TFERROR_COULDNT_SAVE_OR_DELETE_FPS = 19,
    TFERROR_PREVIOUS_UPLOAD_PENDING = 20,
    TFERROR_UPLOAD_TOO_FREQUENT = 21,
    TFERROR_NO_SUCH_KEY = 22,
    TFERROR_BAD_FILE_OPERATION = 23,
    TFERROR_BAD_SETTINGS = 24
};

typedef TFErrorCode (*PFnTFCacheGet)(uint64_t key, CProtobufSerialization* value,
                                     void* user_data);
typedef TFErrorCode (*PFnTFCacheSet)(uint64_t key, const CProtobufSerialization* value,
                                     void* user_data);
typedef TFErrorCode (*PFnTFCacheRemove)(uint64_t key, void* user_data);

struct TFCache {
  void* user_data;
  PFnTFCacheSet set;
  PFnTFCacheGet get;
  PFnTFCacheRemove remove;
};

typedef void (*ProtoCallback)(const CProtobufSerialization*);
typedef void (*UploadCallback)(const char*, size_t n);
struct SwappyTracer;
typedef void (*SwappyTracerFn)(const SwappyTracer*);

/**
 * @brief Initialization settings
 *   Zero any values that are not being used.
 */
struct TFSettings {
  /**
   * Cache object to be used for upload data persistence.
   * If unset, data is persisted to /data/local/tmp/tuningfork
   */
  const TFCache* persistent_cache;
  /**
   * The address of the Swappy_injectTracers function.
   * If this is unset, you need to call TuningFork_tick yourself.
   * If it is set, telemetry for 4 instrument keys is automatically recorded.
   */
  SwappyTracerFn swappy_tracer_fn;
  /**
   * Callback
   * If set, this is called with the fidelity parameters that are downloaded.
   */
  ProtoCallback fidelity_params_callback;
};

#ifdef __cplusplus
extern "C" {
#endif

inline void CProtobufSerialization_Free(CProtobufSerialization* ser) {
    if(ser->dealloc) ser->dealloc(ser);
}

// Internal init function. Do not call directly.
TFErrorCode TuningFork_init_internal(const TFSettings *settings, JNIEnv* env, jobject context);

// Internal function to track TuningFork version bundled in a binary. Do not call directly.
// If you are getting linker errors related to TuningFork_version_x_y, you probably have a
// mismatch between the header used at compilation and the actually library used by the linker.
void TUNINGFORK_VERSION_SYMBOL();

// TuningFork_init must be called before any other functions.
// If @p settings is a null pointer, settings are extracted from the app.
// Also, if settings.aggregation_strategy.method is invalid, settings are extracted from the
//  app and merged with the passed-in settings.
// This means that if you set swappy_tracer_fn and nothing else, the app will load settings
//  from the APK settings.bin file and add the 4 Swappy instrument key tracers.
// If you also set fp_default_file_name, a fidelity parameter download thread will be
//  automatically started. See the description of TFSettings above.
// Ownership of @p settings remains with the caller.
// Returns TFERROR_OK if successful, TFERROR_NO_SETTINGS if no settings could be found.
static inline TFErrorCode TuningFork_init(const TFSettings *settings, JNIEnv* env, jobject context) {
    // This call ensures that the header and the linked library are from the same version
    // (if not, a linker error will be triggered because of an undefined symbol).
    TUNINGFORK_VERSION_SYMBOL();
    return TuningFork_init_internal(settings, env, context);
}

// The functions below will return TFERROR_TUNINGFORK_NOT_INITIALIZED if TuningFork_init
//  has not first been called.

// Blocking call to get fidelity parameters from the server.
// Note that once fidelity parameters are downloaded, any timing information is recorded
//  as being associated with those parameters.
// If you subsequently call GetFidelityParameters and a new set of parameters is downloaded,
// any data that is already collected will be submitted to the backend.
// Ownership of 'params' is transferred to the caller, so they must call params->dealloc
// when they are done with it.
// The parameter request is sent to:
//  ${url_base}+'applications/'+package_name+'/apks/'+version_number+':generateTuningParameters'.
// Returns TFERROR_TIMEOUT if there was a timeout before params could be downloaded.
// Returns TFERROR_OK on success.
TFErrorCode TuningFork_getFidelityParameters(
    const CProtobufSerialization *defaultParams,
    CProtobufSerialization *params, uint32_t timeout_ms);

// Protobuf serialization of the current annotation.
// Returns TFERROR_INVALID_ANNOTATION if annotation is inconsistent with the settings.
// Returns TFERROR_OK on success.
TFErrorCode TuningFork_setCurrentAnnotation(const CProtobufSerialization *annotation);

// Record a frame tick that will be associated with the instrumentation key and the current
//   annotation.
// NB: calling the tick or trace functions from different threads is allowed, but a given
//  instrument key should always be ticked from the same thread.
// Returns TFERROR_INVALID_INSTRUMENT_KEY if the instrument key is invalid.
// Returns TFERROR_OK on success.
TFErrorCode TuningFork_frameTick(TFInstrumentKey id);

// Record a frame tick using an external time, rather than system time.
// Returns TFERROR_INVALID_INSTRUMENT_KEY if the instrument key is invalid.
// Returns TFERROR_OK on success.
TFErrorCode TuningFork_frameDeltaTimeNanos(TFInstrumentKey id, TFDuration dt);

// Start a trace segment.
// handle is filled with a new handle on success.
// Returns TFERROR_INVALID_INSTRUMENT_KEY if the instrument key is invalid.
// Returns TFERROR_OK on success.
TFErrorCode TuningFork_startTrace(TFInstrumentKey key, TFTraceHandle* handle);

// Record a trace with the key and annotation set using startTrace.
// Returns TFERROR_INVALID_TRACE_HANDLE if the handle is invalid.
// Returns TFERROR_OK on success.
TFErrorCode TuningFork_endTrace(TFTraceHandle h);

// Force upload of the current histograms.
// Returns TFERROR_OK if the upload could be initiated.
// Returns TFERROR_PREVIOUS_UPLOAD_PENDING if there is a previous upload blocking this
//  one.
// Returns TFERROR_UPLOAD_TOO_FREQUENT if less than a minute has elapsed since the previous upload.
TFErrorCode TuningFork_flush();

// Set a callback to be called on a separate thread every time TuningFork
//  performs an upload.
TFErrorCode TuningFork_setUploadCallback(UploadCallback cbk);

#ifdef __cplusplus
}
#endif
