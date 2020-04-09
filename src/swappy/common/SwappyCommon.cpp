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

#include "SwappyCommon.h"

#include <cmath>
#include <cstdlib>

#include "Settings.h"
#include "Thread.h"
#include "Log.h"
#include "Trace.h"

#define LOG_TAG "SwappyCommon"

namespace swappy {

using std::chrono::milliseconds;
using std::chrono::nanoseconds;

// NB These are only needed for C++14
constexpr nanoseconds SwappyCommon::FrameDuration::MAX_DURATION;
constexpr nanoseconds SwappyCommon::FRAME_MARGIN;
constexpr nanoseconds SwappyCommon::REFRESH_RATE_MARGIN;
constexpr int SwappyCommon::NON_PIPELINE_PERCENT;
constexpr int SwappyCommon::FRAME_DROP_THRESHOLD;
constexpr std::chrono::nanoseconds SwappyCommon::FrameDurations::FRAME_DURATION_SAMPLE_SECONDS;

bool SwappyCommonSettings::getFromApp(JNIEnv *env, jobject jactivity,
                                      SwappyCommonSettings* out) {

    if (out == nullptr) return false;

    ALOGI("Swappy version %d.%d", SWAPPY_MAJOR_VERSION, SWAPPY_MINOR_VERSION);

    out->sdkVersion = getSDKVersion(env);

    jclass activityClass = env->FindClass("android/app/NativeActivity");
    jclass windowManagerClass = env->FindClass("android/view/WindowManager");
    jclass displayClass = env->FindClass("android/view/Display");

    jmethodID getWindowManager = env->GetMethodID(
            activityClass,
            "getWindowManager",
            "()Landroid/view/WindowManager;");

    jmethodID getDefaultDisplay = env->GetMethodID(
            windowManagerClass,
            "getDefaultDisplay",
            "()Landroid/view/Display;");

    jobject wm = env->CallObjectMethod(jactivity, getWindowManager);
    jobject display = env->CallObjectMethod(wm, getDefaultDisplay);

    jmethodID getRefreshRate = env->GetMethodID(
            displayClass,
            "getRefreshRate",
            "()F");

    const float refreshRateHz = env->CallFloatMethod(display, getRefreshRate);

    jmethodID getAppVsyncOffsetNanos = env->GetMethodID(
            displayClass,
            "getAppVsyncOffsetNanos", "()J");

    // getAppVsyncOffsetNanos was only added in API 21.
    // Return gracefully if this device doesn't support it.
    if (getAppVsyncOffsetNanos == 0 || env->ExceptionOccurred()) {
        ALOGE("Error while getting method: getAppVsyncOffsetNanos");
        env->ExceptionClear();
        return false;
    }
    const long appVsyncOffsetNanos = env->CallLongMethod(display, getAppVsyncOffsetNanos);

    jmethodID getPresentationDeadlineNanos = env->GetMethodID(
        displayClass,
        "getPresentationDeadlineNanos",
        "()J");


    if (getPresentationDeadlineNanos == 0 || env->ExceptionOccurred()) {
        ALOGE("Error while getting method: getPresentationDeadlineNanos");
        return false;
    }

    const long vsyncPresentationDeadlineNanos = env->CallLongMethod(
        display, getPresentationDeadlineNanos);

    const long ONE_MS_IN_NS = 1000 * 1000;
    const long ONE_S_IN_NS = ONE_MS_IN_NS * 1000;

    const long vsyncPeriodNanos = static_cast<long>(ONE_S_IN_NS / refreshRateHz);
    const long sfVsyncOffsetNanos =
        vsyncPeriodNanos - (vsyncPresentationDeadlineNanos - ONE_MS_IN_NS);

    using std::chrono::nanoseconds;
    out->refreshPeriod = nanoseconds(vsyncPeriodNanos);
    out->appVsyncOffset = nanoseconds(appVsyncOffsetNanos);
    out->sfVsyncOffset  = nanoseconds(sfVsyncOffsetNanos);

    return true;
}

SwappyCommon::SwappyCommon(JNIEnv *env, jobject jactivity)
        : mJactivity(env->NewGlobalRef(jactivity)),
          mSwapDuration(nanoseconds(0)),
          mAutoSwapInterval(1),
          mValid(false) {
    if (!SwappyCommonSettings::getFromApp(env, mJactivity, &mCommonSettings))
        return;

    env->GetJavaVM(&mJVM);

    if (isDeviceBlacklisted()) {
        ALOGE("Device is blacklisted");
        return;
    }

    mChoreographerFilter = std::make_unique<ChoreographerFilter>(mCommonSettings.refreshPeriod,
                                     mCommonSettings.sfVsyncOffset - mCommonSettings.appVsyncOffset,
                                                                [this]() { return wakeClient(); });

    mChoreographerThread = ChoreographerThread::createChoreographerThread(
                                ChoreographerThread::Type::Swappy,
                                mJVM,
                                jactivity,
                                [this]{ mChoreographerFilter->onChoreographer(); },
                                [this]{ onRefreshRateChanged(); },
                                mCommonSettings.sdkVersion);
    if (!mChoreographerThread->isInitialized()) {
        ALOGE("failed to initialize ChoreographerThread");
        return;
    }
    if (USE_DISPLAY_MANAGER && mCommonSettings.sdkVersion >= SwappyDisplayManager::MIN_SDK_VERSION) {
        mDisplayManager = std::make_unique<SwappyDisplayManager>(mJVM, jactivity);

        if (!mDisplayManager->isInitialized()) {
            mDisplayManager = nullptr;
            ALOGE("failed to initialize DisplayManager");
            return;
        }
    }

    Settings::getInstance()->addListener([this]() { onSettingsChanged(); });
    Settings::getInstance()->setDisplayTimings({mCommonSettings.refreshPeriod,
                                                mCommonSettings.appVsyncOffset,
                                                mCommonSettings.sfVsyncOffset});

    ALOGI("Initialized Swappy with vsyncPeriod=%lld, appOffset=%lld, sfOffset=%lld",
        (long long)mCommonSettings.refreshPeriod.count(),
        (long long)mCommonSettings.appVsyncOffset.count(),
        (long long)mCommonSettings.sfVsyncOffset.count()
    );
    mValid = true;
}

// Used by tests
SwappyCommon::SwappyCommon(const SwappyCommonSettings& settings) :
          mJactivity(nullptr),
          mSwapDuration(nanoseconds(0)),
          mAutoSwapInterval(1),
          mValid(true), mCommonSettings(settings) {

    mChoreographerFilter = std::make_unique<ChoreographerFilter>(mCommonSettings.refreshPeriod,
                                     mCommonSettings.sfVsyncOffset - mCommonSettings.appVsyncOffset,
                                                                [this]() { return wakeClient(); });
    mUsingExternalChoreographer = true;
    mChoreographerThread =
            ChoreographerThread::createChoreographerThread(
                    ChoreographerThread::Type::App,
                    nullptr,
                    nullptr,
                    [this] { mChoreographerFilter->onChoreographer(); },
                    [this]{ },
                    mCommonSettings.sdkVersion);

    Settings::getInstance()->addListener([this]() { onSettingsChanged(); });
    Settings::getInstance()->setDisplayTimings({mCommonSettings.refreshPeriod,
                                                mCommonSettings.appVsyncOffset,
                                                mCommonSettings.sfVsyncOffset});

    ALOGI("Initialized Swappy with vsyncPeriod=%lld, appOffset=%lld, sfOffset=%lld",
        (long long)mCommonSettings.refreshPeriod.count(),
        (long long)mCommonSettings.appVsyncOffset.count(),
        (long long)mCommonSettings.sfVsyncOffset.count()
    );

}

SwappyCommon::~SwappyCommon() {
    // destroy all threads first before the other members of this class
    mChoreographerThread.reset();
    mChoreographerFilter.reset();

    Settings::reset();

    if (mJactivity != nullptr) {
        JNIEnv *env;
        mJVM->AttachCurrentThread(&env, nullptr);

        env->DeleteGlobalRef(mJactivity);
    }
}

void SwappyCommon::onRefreshRateChanged() {
    JNIEnv *env;
    mJVM->AttachCurrentThread(&env, nullptr);

    ALOGV("onRefreshRateChanged");

    SwappyCommonSettings settings;
    if (!SwappyCommonSettings::getFromApp(env, mJactivity, &settings)) {
        ALOGE("failed to query display timings");
        return;
    }

    Settings::getInstance()->setDisplayTimings({settings.refreshPeriod,
                                                settings.appVsyncOffset,
                                                settings.sfVsyncOffset});
}

nanoseconds SwappyCommon::wakeClient() {
    std::lock_guard<std::mutex> lock(mWaitingMutex);
    ++mCurrentFrame;

    // We're attempting to align with SurfaceFlinger's vsync, but it's always better to be a little
    // late than a little early (since a little early could cause our frame to be picked up
    // prematurely), so we pad by an additional millisecond.
    mCurrentFrameTimestamp = std::chrono::steady_clock::now() + mSwapDuration.load() + 1ms;
    mWaitingCondition.notify_all();
    return mSwapDuration;
}

void SwappyCommon::onChoreographer(int64_t frameTimeNanos) {
    TRACE_CALL();

    if (!mUsingExternalChoreographer) {
        mUsingExternalChoreographer = true;
        mChoreographerThread =
                ChoreographerThread::createChoreographerThread(
                        ChoreographerThread::Type::App,
                        nullptr,
                        nullptr,
                        [this] { mChoreographerFilter->onChoreographer(); },
                        [this]{ onRefreshRateChanged(); },
                        mCommonSettings.sdkVersion);
    }

    mChoreographerThread->postFrameCallbacks();
}

bool SwappyCommon::waitForNextFrame(const SwapHandlers& h) {
    int lateFrames = 0;
    bool presentationTimeIsNeeded;

    const nanoseconds cpuTime = (mStartFrameTime.time_since_epoch().count()==0)
        ? 0ns : std::chrono::steady_clock::now() - mStartFrameTime;
    mCPUTracer.endTrace();

    preWaitCallbacks();

    // if we are running slower than the threshold there is no point to sleep, just let the
    // app run as fast as it can
    if (mCommonSettings.refreshPeriod * mAutoSwapInterval <= mAutoSwapIntervalThresholdNS.load()) {
        waitUntilTargetFrame();

        // wait for the previous frame to be rendered
        while (!h.lastFrameIsComplete()) {
            lateFrames++;
            waitOneFrame();
        }

        mPresentationTime += lateFrames * mCommonSettings.refreshPeriod;
        presentationTimeIsNeeded = true;
    } else {
        presentationTimeIsNeeded = false;
    }

    const nanoseconds gpuTime = h.getPrevFrameGpuTime();
    addFrameDuration({cpuTime, gpuTime, mCurrentFrame > mTargetFrame});

    postWaitCallbacks(cpuTime, gpuTime);

    return presentationTimeIsNeeded;
}

void SwappyCommon::updateDisplayTimings() {
    // grab a pointer to the latest supported refresh rates
    if (mDisplayManager) {
        mSupportedRefreshRates = mDisplayManager->getSupportedRefreshRates();
    }

    std::lock_guard<std::mutex> lock(mFrameDurationsMutex);
    if (!mTimingSettingsNeedUpdate) {
        return;
    }

    mTimingSettingsNeedUpdate = false;

    if (mCommonSettings.refreshPeriod == mNextTimingSettings.refreshPeriod &&
        mSwapIntervalNS == mNextTimingSettings.swapIntervalNS) {
        return;
    }

    mAutoSwapInterval = mSwapIntervalForNewRefresh;
    mPipelineMode = PipelineMode::On;
    mSwapIntervalForNewRefresh = 0;

    const bool swapIntervalValid = mNextTimingSettings.refreshPeriod * mAutoSwapInterval >=
                                   mNextTimingSettings.swapIntervalNS;
    const bool swapIntervalChangedBySettings = mSwapIntervalNS !=
                                               mNextTimingSettings.swapIntervalNS;

    mCommonSettings.refreshPeriod = mNextTimingSettings.refreshPeriod;
    mSwapIntervalNS = mNextTimingSettings.swapIntervalNS;
    if (!mAutoSwapIntervalEnabled || swapIntervalChangedBySettings ||
            mAutoSwapInterval == 0 || !swapIntervalValid) {
        mAutoSwapInterval = calculateSwapInterval(mSwapIntervalNS, mCommonSettings.refreshPeriod);
        mPipelineMode = PipelineMode::On;
        setPreferredRefreshRate(mSwapIntervalNS);
    }

    if (mNextModeId == -1) {
        setPreferredRefreshRate(mSwapIntervalNS);
    }

    mFrameDurations.clear();

    TRACE_INT("mSwapIntervalNS", int(mSwapIntervalNS.count()));
    TRACE_INT("mAutoSwapInterval", mAutoSwapInterval);
    TRACE_INT("mCommonSettings.refreshPeriod", mCommonSettings.refreshPeriod.count());
    TRACE_INT("mPipelineMode", static_cast<int>(mPipelineMode));
}

void SwappyCommon::onPreSwap(const SwapHandlers& h) {
    if (!mUsingExternalChoreographer) {
        mChoreographerThread->postFrameCallbacks();
    }

    // for non pipeline mode where both cpu and gpu work is done at the same stage
    // wait for next frame will happen after swap
    if (mPipelineMode == PipelineMode::On) {
        mPresentationTimeNeeded = waitForNextFrame(h);
    } else {
        mPresentationTimeNeeded =
                (mCommonSettings.refreshPeriod * mAutoSwapInterval <= mAutoSwapIntervalThresholdNS.load());
    }

    mSwapTime = std::chrono::steady_clock::now();
    preSwapBuffersCallbacks();
}

void SwappyCommon::onPostSwap(const SwapHandlers& h) {
    postSwapBuffersCallbacks();


    updateSwapDuration(std::chrono::steady_clock::now() - mSwapTime);

    if (mPipelineMode == PipelineMode::Off) {
        waitForNextFrame(h);
    }

    if (updateSwapInterval()) {
        swapIntervalChangedCallbacks();
        TRACE_INT("mPipelineMode", static_cast<int>(mPipelineMode));
        TRACE_INT("mAutoSwapInterval", mAutoSwapInterval);
    }

    updateDisplayTimings();

    startFrame();
}

void SwappyCommon::updateSwapDuration(nanoseconds duration) {
    // TODO: The exponential smoothing factor here is arbitrary
    mSwapDuration = (mSwapDuration.load() * 4 / 5) + duration / 5;

    // Clamp the swap duration to half the refresh period
    //
    // We do this since the swap duration can be a bit noisy during periods such as app startup,
    // which can cause some stuttering as the smoothing catches up with the actual duration. By
    // clamping, we reduce the maximum error which reduces the calibration time.
    if (mSwapDuration.load() > (mCommonSettings.refreshPeriod / 2)) mSwapDuration = mCommonSettings.refreshPeriod / 2;
}

uint64_t SwappyCommon::getSwapIntervalNS() {
    std::lock_guard<std::mutex> lock(mFrameDurationsMutex);
    return mAutoSwapInterval * mCommonSettings.refreshPeriod.count();
};

void SwappyCommon::FrameDurations::add(FrameDuration frameDuration) {
    const auto now = std::chrono::steady_clock::now();
    mFrames.push_back({now, frameDuration});
    mFrameDurationsSum += frameDuration;
    if (frameDuration.frameMiss()) {
        mMissedFrameCount++;
    }

    while (mFrames.size() >= 2 && now - (mFrames.begin() + 1)->first > FRAME_DURATION_SAMPLE_SECONDS) {
        mFrameDurationsSum -= mFrames.front().second;
        if (mFrames.front().second.frameMiss()) {
            mMissedFrameCount--;
        }
        mFrames.pop_front();
    }

}

bool SwappyCommon::FrameDurations::hasEnoughSamples() const {
    return !mFrames.empty() &&
           mFrames.back().first - mFrames.front().first > FRAME_DURATION_SAMPLE_SECONDS;
}

SwappyCommon::FrameDuration SwappyCommon::FrameDurations::getAverageFrameTime() const {
    return mFrameDurationsSum / mFrames.size();
}

int SwappyCommon::FrameDurations::getMissedFramePercent() const {
    return round(mMissedFrameCount * 100.0f / mFrames.size());
}

void SwappyCommon::FrameDurations::clear() {
    mFrames.clear();
    mFrameDurationsSum = {};
    mMissedFrameCount = 0;
}


void SwappyCommon::addFrameDuration(FrameDuration duration) {
    ALOGV("cpuTime = %.2f", duration.getCpuTime().count() / 1e6f);
    ALOGV("gpuTime = %.2f", duration.getGpuTime().count() / 1e6f);
    ALOGV("frame %s", duration.frameMiss() ? "MISS" : "on time");

    std::lock_guard<std::mutex> lock(mFrameDurationsMutex);
    mFrameDurations.add(duration);
}

bool SwappyCommon::swapSlower(const FrameDuration& averageFrameTime,
                        const nanoseconds& upperBound,
                        int newSwapInterval) {
    bool swappedSlower = false;
    ALOGV("Rendering takes too much time for the given config");

    // Check if turning on pipeline is not enough
    if ((mPipelineMode == PipelineMode::On ||
            averageFrameTime.getTime(PipelineMode::On) + FRAME_MARGIN > upperBound) &&
            mCommonSettings.refreshPeriod*mAutoSwapInterval <= mAutoSwapIntervalThresholdNS.load()
                                                                   + FRAME_MARGIN) {
        int mOriginalAutoSwapInterval = mAutoSwapInterval;
        if (newSwapInterval > mAutoSwapInterval) {
            mAutoSwapInterval = newSwapInterval;
        } else {
            mAutoSwapInterval++;
        }
        if (mAutoSwapInterval!=mOriginalAutoSwapInterval) {
            ALOGV("Changing Swap interval to %d from %d", mAutoSwapInterval,
                  mOriginalAutoSwapInterval);
            swappedSlower = true;
        }
    }

    if (mPipelineMode == PipelineMode::Off) {
        ALOGV("turning on pipelining");
        mPipelineMode = PipelineMode::On;
    }

    return swappedSlower;
}

bool SwappyCommon::swapFaster(int newSwapInterval) {
    bool swappedFaster = false;
    int mOriginalAutoSwapInterval = mAutoSwapInterval;
    while (newSwapInterval < mAutoSwapInterval && mSwapIntervalNS <= mCommonSettings.refreshPeriod * (mAutoSwapInterval - 1)) {
        mAutoSwapInterval--;
    }

    if (mAutoSwapInterval!=mOriginalAutoSwapInterval) {
        ALOGV("Rendering is much shorter for the given config");
        ALOGV("Changing Swap interval to %d from %d", mAutoSwapInterval, mOriginalAutoSwapInterval);
        // since we changed the swap interval, we may need to turn on pipeline mode
        ALOGV("Turning on pipelining");
        mPipelineMode = PipelineMode::On;
        swappedFaster = true;
    }

    return swappedFaster;
}

bool SwappyCommon::isSameDuration(std::chrono::nanoseconds period1, int interval1,
                                  std::chrono::nanoseconds period2, int interval2) {
    static constexpr std::chrono::nanoseconds MARGIN = 1ms;

    auto duration1 = period1 * interval1;
    auto duration2 = period2 * interval2;

    if (std::max(duration1, duration2) - std::min(duration1, duration2) < MARGIN) {
        return true;
    }
    return false;
}

bool SwappyCommon::updateSwapInterval() {
    std::lock_guard<std::mutex> lock(mFrameDurationsMutex);
    if (!mAutoSwapIntervalEnabled)
        return false;

    if (!mFrameDurations.hasEnoughSamples())
        return false;

    const auto averageFrameTime = mFrameDurations.getAverageFrameTime();
    const auto pipelineFrameTime = averageFrameTime.getTime(PipelineMode::On) + FRAME_MARGIN;
    const auto nonPipelineFrameTime = averageFrameTime.getTime(PipelineMode::Off) + FRAME_MARGIN;

    // calculate the new swap interval based on average frame time assume we are in pipeline mode
    // (prefer higher swap interval rather than turning off pipeline mode)
    const int newSwapInterval = calculateSwapInterval(pipelineFrameTime,
                                                      mCommonSettings.refreshPeriod);

    // Define upper and lower bounds based on the swap duration
    const nanoseconds upperBoundForThisRefresh = mCommonSettings.refreshPeriod * mAutoSwapInterval;
    const nanoseconds lowerBoundForThisRefresh = mCommonSettings.refreshPeriod * (mAutoSwapInterval - 1)
            - FRAME_MARGIN;


    const int missedFramesPercent = mFrameDurations.getMissedFramePercent();

    ALOGV("mPipelineMode = %d", static_cast<int>(mPipelineMode));
    ALOGV("Average cpu frame time = %.2f", (averageFrameTime.getCpuTime().count()) / 1e6f);
    ALOGV("Average gpu frame time = %.2f", (averageFrameTime.getGpuTime().count()) / 1e6f);
    ALOGV("upperBound = %.2f", upperBoundForThisRefresh.count() / 1e6f);
    ALOGV("lowerBound = %.2f", lowerBoundForThisRefresh.count() / 1e6f);
    ALOGV("frame missed = %d%%", missedFramesPercent);

    bool configChanged = false;
    ALOGV("pipelineFrameTime = %.2f", pipelineFrameTime.count() / 1e6f);

    // Make sure the frame time fits in the current config to avoid missing frames
    if (missedFramesPercent > FRAME_DROP_THRESHOLD) {
        if (swapSlower(averageFrameTime, upperBoundForThisRefresh, newSwapInterval))
            configChanged = true;
    }

    // So we shouldn't miss any frames with this config but maybe we can go faster ?
    // we check the pipeline frame time here as we prefer lower swap interval than no pipelining
    else if (missedFramesPercent == 0 &&
             mSwapIntervalNS <= mCommonSettings.refreshPeriod * (mAutoSwapInterval - 1) &&
             pipelineFrameTime < lowerBoundForThisRefresh) {
        if (swapFaster(newSwapInterval))
            configChanged = true;
    }

    // If we reached to this condition it means that we fit into the boundaries.
    // However we might be in pipeline mode and we could turn it off if we still fit.
    // To be very conservative, switch to non-pipeline if frame time * 50% fits
    else if (mPipelineModeAutoMode && mPipelineMode == PipelineMode::On &&
            nonPipelineFrameTime * ((100.f + NON_PIPELINE_PERCENT) / 100.f) < upperBoundForThisRefresh) {
        ALOGV("Rendering time fits the current swap interval without pipelining");
        mPipelineMode = PipelineMode::Off;
        configChanged = true;
    }

    if (configChanged) {
        mFrameDurations.clear();
    }

    // Loop across all supported refresh rate to see if we can find a better refresh rate.
    // Better refresh rate means:
    //      Shorter swap period that can still accommodate the frame time can be achieved
    //      Or,
    //      Same swap period can be achieved with a lower refresh rate to optimize power
    //      consumption.
    nanoseconds minSwapPeriod = mCommonSettings.refreshPeriod * mAutoSwapInterval;
    bool betterRefreshFound = false;
    std::pair<std::chrono::nanoseconds, int> betterRefreshConfig;
    int betterRefreshSwapInterval = 0;
    if (mSupportedRefreshRates) {
        for (auto i : *mSupportedRefreshRates) {
            const auto period = i.first;
            const int swapIntervalForPeriod = calculateSwapInterval(pipelineFrameTime, period);
            const nanoseconds duration = period * swapIntervalForPeriod;
            const nanoseconds lowerBound = duration;
            if (pipelineFrameTime < lowerBound && duration < minSwapPeriod && duration >= mSwapIntervalNS) {
                minSwapPeriod = duration;
                betterRefreshConfig = i;
                betterRefreshSwapInterval = swapIntervalForPeriod;
                betterRefreshFound = true;
                ALOGV("Found better refresh %.2f", 1e9f / period.count());
            }
        }

        if (!betterRefreshFound) {
            for (auto i : *mSupportedRefreshRates) {
                const auto period = i.first;
                const int swapIntervalForPeriod =
                        calculateSwapInterval(pipelineFrameTime, period);
                const nanoseconds duration = period * swapIntervalForPeriod;
                if (isSameDuration(period, swapIntervalForPeriod,
                                   mCommonSettings.refreshPeriod, mAutoSwapInterval)
                    && period > mCommonSettings.refreshPeriod) {
                    betterRefreshFound = true;
                    betterRefreshConfig = i;
                    betterRefreshSwapInterval = swapIntervalForPeriod;
                    ALOGV("Found better refresh %.2f", 1e9f / period.count());
                }
            }
        }
    }

    // Check if we there is a potential better refresh rate
    if (betterRefreshFound) {
        TRACE_INT("preferredRefreshPeriod", betterRefreshConfig.first.count());
        setPreferredRefreshRate(betterRefreshConfig.second);
        mSwapIntervalForNewRefresh = betterRefreshSwapInterval;

        nanoseconds upperBoundForNewRefresh = betterRefreshConfig.first * betterRefreshSwapInterval;
    }

    return configChanged;
}

template<typename Tracers, typename Func> void addToTracers(Tracers& tracers, Func func,
                                                            void *userData) {
    if (func != nullptr) {
        tracers.push_back([func, userData](auto... params) {
            func(userData, params...);
        });
    }
}

void SwappyCommon::addTracerCallbacks(SwappyTracer tracer) {
    addToTracers(mInjectedTracers.preWait, tracer.preWait, tracer.userData);
    addToTracers(mInjectedTracers.postWait, tracer.postWait, tracer.userData);
    addToTracers(mInjectedTracers.preSwapBuffers, tracer.preSwapBuffers, tracer.userData);
    addToTracers(mInjectedTracers.postSwapBuffers, tracer.postSwapBuffers, tracer.userData);
    addToTracers(mInjectedTracers.startFrame, tracer.startFrame, tracer.userData);
    addToTracers(mInjectedTracers.swapIntervalChanged, tracer.swapIntervalChanged, tracer.userData);
}

template<typename T, typename ...Args> void executeTracers(T& tracers, Args... args) {
    for (const auto& tracer : tracers) {
        tracer(std::forward<Args>(args)...);
    }
}

void SwappyCommon::preSwapBuffersCallbacks() {
    executeTracers(mInjectedTracers.preSwapBuffers);
}

void SwappyCommon::postSwapBuffersCallbacks() {
    executeTracers(mInjectedTracers.postSwapBuffers,
                   (long) mPresentationTime.time_since_epoch().count());
}

void SwappyCommon::preWaitCallbacks() {
    executeTracers(mInjectedTracers.preWait);
}

void SwappyCommon::postWaitCallbacks(nanoseconds cpuTime, nanoseconds gpuTime) {
    executeTracers(mInjectedTracers.postWait, cpuTime.count(), gpuTime.count());
}

void SwappyCommon::startFrameCallbacks() {
    executeTracers(mInjectedTracers.startFrame,
                   mCurrentFrame,
                   (long) mPresentationTime.time_since_epoch().count());
}

void SwappyCommon::swapIntervalChangedCallbacks() {
    executeTracers(mInjectedTracers.swapIntervalChanged);
}

void SwappyCommon::setAutoSwapInterval(bool enabled) {
    std::lock_guard<std::mutex> lock(mFrameDurationsMutex);
    mAutoSwapIntervalEnabled = enabled;

    // non pipeline mode is not supported when auto mode is disabled
    if (!enabled) {
        mPipelineMode = PipelineMode::On;
        TRACE_INT("mPipelineMode", static_cast<int>(mPipelineMode));
    }
}

void SwappyCommon::setAutoPipelineMode(bool enabled) {
    std::lock_guard<std::mutex> lock(mFrameDurationsMutex);
    mPipelineModeAutoMode = enabled;
    TRACE_INT("mPipelineModeAutoMode", mPipelineModeAutoMode);
    if (!enabled) {
        mPipelineMode = PipelineMode::On;
        TRACE_INT("mPipelineMode", static_cast<int>(mPipelineMode));
    }
}

void SwappyCommon::setPreferredRefreshRate(int modeId) {
    if (!mDisplayManager || modeId < 0 || mNextModeId == modeId) {
        return;
    }

    mNextModeId = modeId;
    mDisplayManager->setPreferredRefreshRate(modeId);
}

int SwappyCommon::calculateSwapInterval(nanoseconds frameTime, nanoseconds refreshPeriod) {

    if (frameTime < refreshPeriod) {
        return 1;
    }

    auto div_result = div(frameTime.count(), refreshPeriod.count());
    auto framesPerRefresh = div_result.quot;
    auto framesPerRefreshRemainder = div_result.rem;

    return (framesPerRefresh + (framesPerRefreshRemainder > REFRESH_RATE_MARGIN.count() ? 1 : 0));
}

void SwappyCommon::setPreferredRefreshRate(nanoseconds frameTime) {
    if (!mDisplayManager) {
        return;
    }

    int bestModeId = -1;
    nanoseconds bestPeriod = 0ns;
    nanoseconds swapIntervalNSMin = 100ms;
    for (auto i = mSupportedRefreshRates->crbegin(); i != mSupportedRefreshRates->crend(); ++i) {
        const auto period = i->first;
        const int modeId = i->second;

        // Make sure we don't cross the swap interval set by the app
        if (frameTime < mSwapIntervalNS) {
            frameTime = mSwapIntervalNS;
        }

        int swapIntervalForPeriod = calculateSwapInterval(frameTime, period);
        const auto swapIntervalNS = (period * swapIntervalForPeriod);
        if (swapIntervalNS < swapIntervalNSMin) {
            swapIntervalNSMin = swapIntervalNS;
            bestModeId = modeId;
            bestPeriod = period;
        }
    }

    TRACE_INT("preferredRefreshPeriod", bestPeriod.count());
    setPreferredRefreshRate(bestModeId);
}

void SwappyCommon::onSettingsChanged() {
    std::lock_guard<std::mutex> lock(mFrameDurationsMutex);

    TimingSettings timingSettings = TimingSettings::from(*Settings::getInstance());

    // If display timings has changed, cache the update and apply them on the next frame
    if (timingSettings != mNextTimingSettings) {
        mNextTimingSettings = timingSettings;
        mTimingSettingsNeedUpdate = true;
    }

}

void SwappyCommon::startFrame() {
    TRACE_CALL();

    int32_t currentFrame;
    std::chrono::steady_clock::time_point currentFrameTimestamp;
    {
        std::unique_lock<std::mutex> lock(mWaitingMutex);
        currentFrame = mCurrentFrame;
        currentFrameTimestamp = mCurrentFrameTimestamp;
    }

    mTargetFrame = currentFrame + mAutoSwapInterval;

    const int intervals = (mPipelineMode == PipelineMode::On) ? 2 : 1;

    // We compute the target time as now
    //   + the time the buffer will be on the GPU and in the queue to the compositor (1 swap period)
    mPresentationTime = currentFrameTimestamp + (mAutoSwapInterval * intervals)
                        * mCommonSettings.refreshPeriod;

    mStartFrameTime = std::chrono::steady_clock::now();
    mCPUTracer.startTrace();

    startFrameCallbacks();
}

void SwappyCommon::waitUntil(int32_t target) {
    TRACE_CALL();
    std::unique_lock<std::mutex> lock(mWaitingMutex);
    mWaitingCondition.wait(lock, [&]() {
        if (mCurrentFrame < target) {
            if (!mUsingExternalChoreographer) {
                mChoreographerThread->postFrameCallbacks();
            }
            return false;
        }
        return true;
    });
}

void SwappyCommon::waitUntilTargetFrame() {
    waitUntil(mTargetFrame);
}

void SwappyCommon::waitOneFrame() {
    waitUntil(mCurrentFrame + 1);
}

int SwappyCommonSettings::getSDKVersion(JNIEnv *env) {
    const jclass buildClass = env->FindClass("android/os/Build$VERSION");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        ALOGE("Failed to get Build.VERSION class");
        return 0;
    }

    const jfieldID sdk_int = env->GetStaticFieldID(buildClass, "SDK_INT", "I");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        ALOGE("Failed to get Build.VERSION.SDK_INT field");
        return 0;
    }

    const jint sdk = env->GetStaticIntField(buildClass, sdk_int);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        ALOGE("Failed to get SDK version");
        return 0;
    }

    ALOGI("SDK version = %d", sdk);
    return sdk;
}

namespace {

static std::string GetStaticStringField(JNIEnv* env, jclass clz, const char* name) {
    const jfieldID fieldId = env->GetStaticFieldID(clz, name,
							"Ljava/lang/String;");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        ALOGE("Failed to get string field %s", name);
        return "";
    }

    const jstring jstr = (jstring)env->GetStaticObjectField(clz, fieldId);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        ALOGE("Failed to get string %s", name);
        return "";
    }
    auto cstr = env->GetStringUTFChars(jstr, nullptr);
    auto length = env->GetStringUTFLength(jstr);
    std::string retValue(cstr, length);
    env->ReleaseStringUTFChars(jstr, cstr);
    env->DeleteLocalRef(jstr);
    return retValue;
}

struct DeviceIdentifier {
    std::string manufacturer;
    std::string model;
    std::string display;
    // Empty fields match against any value and we match the beginning of the input, e.g.
    //  A37 matches A37f, A37fw, etc.
    bool match(const std::string& manufacturer_in, const std::string& model_in,
	       const std::string& display_in) {
        if (! matchStartOfString(manufacturer, manufacturer_in) ) return false;
        if (! matchStartOfString(model, model_in) ) return false;
        if (! matchStartOfString(display, display_in) ) return false;
        return true;
    }
    bool matchStartOfString(const std::string& start, const std::string& sample) {
        return start.empty() || start==sample.substr(0, start.length());
    }
};

} // anonymous namespace

bool SwappyCommon::isDeviceBlacklisted() {
    JNIEnv *env;
    mJVM->AttachCurrentThread(&env, nullptr);

    // List of blacklisted models
    static std::vector<DeviceIdentifier> blacklistedDevices = {{"OPPO", "A37", ""}};

    const jclass buildClass = env->FindClass("android/os/Build");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        ALOGE("Failed to get Build class");
        return false;
    }

    auto manufacturer = GetStaticStringField(env, buildClass, "MANUFACTURER");
    if (manufacturer.empty()) return false;

    auto model = GetStaticStringField(env, buildClass, "MODEL");
    if (model.empty()) return false;

    auto display = GetStaticStringField(env, buildClass, "DISPLAY");
    if (display.empty()) return false;

    for(auto& device: blacklistedDevices) {
        if (device.match(manufacturer, model, display))
            return true;
    }

    return false;
}

} // namespace swappy
