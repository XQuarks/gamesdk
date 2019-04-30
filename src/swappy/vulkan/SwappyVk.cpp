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

#include "SwappyVkBase.h"
#include "SwappyVkFallback.h"
#include "SwappyVkGoogleDisplayTiming.h"

namespace swappy {

// Note: The API functions is at the botton of the file.  Those functions call methods of the
// singleton SwappyVk class.  Those methods call virtual methods of the abstract SwappyVkBase
// class, which is actually implemented by one of the derived/concrete classes:
//
// - SwappyVkGoogleDisplayTiming
// - SwappyVkFallback

/***************************************************************************************************
 *
 * Singleton class that provides the high-level implementation of the Swappy entrypoints.
 *
 ***************************************************************************************************/
/**
 * Singleton class that provides the high-level implementation of the Swappy entrypoints.
 *
 * This class determines which low-level implementation to use for each physical
 * device, and then calls that class's do-method for the entrypoint.
 */
class SwappyVk
{
public:
    static SwappyVk& getInstance() {
        static SwappyVk instance;
        return instance;
    }

    ~SwappyVk() {
        if(mLibVulkan)
            dlclose(mLibVulkan);
    }

    void swappyVkDetermineDeviceExtensions(VkPhysicalDevice       physicalDevice,
                                           uint32_t               availableExtensionCount,
                                           VkExtensionProperties* pAvailableExtensions,
                                           uint32_t*              pRequiredExtensionCount,
                                           char**                 pRequiredExtensions);
    void SetQueueFamilyIndex(VkDevice   device,
                             VkQueue    queue,
                             uint32_t   queueFamilyIndex);
    bool GetRefreshCycleDuration(VkPhysicalDevice physicalDevice,
                                 VkDevice         device,
                                 VkSwapchainKHR   swapchain,
                                 uint64_t*        pRefreshDuration);
    void SetSwapIntervalNS(VkDevice       device,
                           VkSwapchainKHR swapchain,
                           uint64_t       swap_ns);
    VkResult QueuePresent(VkQueue                 queue,
                          const VkPresentInfoKHR* pPresentInfo);
    void DestroySwapchain(VkDevice                device,
                          VkSwapchainKHR          swapchain);

private:
    std::map<VkPhysicalDevice, bool> doesPhysicalDeviceHaveGoogleDisplayTiming;
    std::map<VkDevice, std::shared_ptr<SwappyVkBase>> perDeviceImplementation;
    std::map<VkSwapchainKHR, std::shared_ptr<SwappyVkBase>> perSwapchainImplementation;

    struct QueueFamilyIndex {
        VkDevice device;
        uint32_t queueFamilyIndex;
    };
    std::map<VkQueue, QueueFamilyIndex> perQueueFamilyIndex;

    void *mLibVulkan     = nullptr;

private:
    SwappyVk() {} // Need to implement this constructor

    // Forbid copies.
    SwappyVk(SwappyVk const&) = delete;
    void operator=(SwappyVk const&) = delete;
};

/**
 * Generic/Singleton implementation of swappyVkDetermineDeviceExtensions.
 */
void SwappyVk::swappyVkDetermineDeviceExtensions(
    VkPhysicalDevice       physicalDevice,
    uint32_t               availableExtensionCount,
    VkExtensionProperties* pAvailableExtensions,
    uint32_t*              pRequiredExtensionCount,
    char**                 pRequiredExtensions)
{
    // TODO: Refactor this to be more concise:
    if (!pRequiredExtensions) {
        for (uint32_t i = 0; i < availableExtensionCount; i++) {
            if (!strcmp(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME,
                        pAvailableExtensions[i].extensionName)) {
                (*pRequiredExtensionCount)++;
            }
        }
    } else {
        doesPhysicalDeviceHaveGoogleDisplayTiming[physicalDevice] = false;
        for (uint32_t i = 0, j = 0; i < availableExtensionCount; i++) {
            if (!strcmp(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME,
                        pAvailableExtensions[i].extensionName)) {
                if (j < *pRequiredExtensionCount) {
                    strcpy(pRequiredExtensions[j++], VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);
                    doesPhysicalDeviceHaveGoogleDisplayTiming[physicalDevice] = true;
                }
            }
        }
    }
}

void SwappyVk::SetQueueFamilyIndex(VkDevice   device,
                                    VkQueue    queue,
                                    uint32_t   queueFamilyIndex)
{
    perQueueFamilyIndex[queue] = {device, queueFamilyIndex};
}


/**
 * Generic/Singleton implementation of swappyVkGetRefreshCycleDuration.
 */
bool SwappyVk::GetRefreshCycleDuration(VkPhysicalDevice physicalDevice,
                                       VkDevice         device,
                                       VkSwapchainKHR   swapchain,
                                       uint64_t*        pRefreshDuration)
{
    auto& pImplementation = perDeviceImplementation[device];
    if (!pImplementation) {
        // We have not seen this device yet.
        if (!mLibVulkan) {
            // This is the first time we've been called--initialize function pointers:
            mLibVulkan = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
            if (!mLibVulkan)
            {
                // If Vulkan doesn't exist, bail-out early:
                return false;
            }
        }

        // First, based on whether VK_GOOGLE_display_timing is available
        // (determined and cached by swappyVkDetermineDeviceExtensions),
        // determine which derived class to use to implement the rest of the API
        if (doesPhysicalDeviceHaveGoogleDisplayTiming[physicalDevice]) {
            pImplementation = std::make_shared<SwappyVkGoogleDisplayTiming>
                    (physicalDevice, device, mLibVulkan);
            ALOGV("SwappyVk initialized for VkDevice %p using VK_GOOGLE_display_timing on Android", device);
        } else {
            pImplementation = std::make_shared<SwappyVkFallback>
                    (physicalDevice, device, mLibVulkan);
            ALOGV("SwappyVk initialized for VkDevice %p using Android fallback", device);
        }

        if(!pImplementation){  // should never happen
            ALOGE("SwappyVk could not find or create correct implementation for the current environment: "
                  "%p, %p", physicalDevice, device);
            return false;
        }
    }

    // Cache the per-swapchain pointer to the derived class:
    perSwapchainImplementation[swapchain] = pImplementation;

    // Now, call that derived class to get the refresh duration to return
    return pImplementation->doGetRefreshCycleDuration(swapchain, pRefreshDuration);
}


/**
 * Generic/Singleton implementation of swappyVkSetSwapInterval.
 */
void SwappyVk::SetSwapIntervalNS(VkDevice       device,
                                 VkSwapchainKHR swapchain,
                                 uint64_t       swap_ns)
{
    auto& pImplementation = perDeviceImplementation[device];
    if (!pImplementation) {
        return;
    }
    pImplementation->doSetSwapInterval(swapchain, swap_ns);
}


/**
 * Generic/Singleton implementation of swappyVkQueuePresent.
 */
VkResult SwappyVk::QueuePresent(VkQueue                 queue,
                                const VkPresentInfoKHR* pPresentInfo)
{
    if (perQueueFamilyIndex.find(queue) == perQueueFamilyIndex.end()) {
        ALOGE("Unknown queue %p. Did you call SwappyVkSetQueueFamilyIndex ?", queue);
        return VK_INCOMPLETE;
    }

    // This command doesn't have a VkDevice.  It should have at least one VkSwapchainKHR's.  For
    // this command, all VkSwapchainKHR's will have the same VkDevice and VkQueue.
    if ((pPresentInfo->swapchainCount == 0) || (!pPresentInfo->pSwapchains)) {
        // This shouldn't happen, but if it does, something is really wrong.
        return VK_ERROR_DEVICE_LOST;
    }
    auto& pImplementation = perSwapchainImplementation[*pPresentInfo->pSwapchains];
    if (pImplementation) {
        return pImplementation->doQueuePresent(queue,
                                               perQueueFamilyIndex[queue].queueFamilyIndex,
                                               pPresentInfo);
    } else {
        // This should only happen if the API was used wrong (e.g. they never
        // called swappyVkGetRefreshCycleDuration).
        // NOTE: Technically, a Vulkan library shouldn't protect a user from
        // themselves, but we'll be friendlier
        return VK_ERROR_DEVICE_LOST;
    }
}

void SwappyVk::DestroySwapchain(VkDevice                device,
                                VkSwapchainKHR          swapchain)
{
    auto it = perQueueFamilyIndex.begin();
    while (it != perQueueFamilyIndex.end()) {
        if (it->second.device == device) {
            it = perQueueFamilyIndex.erase(it);
        } else {
            ++it;
        }
    }

    perDeviceImplementation[device] = nullptr;
    perSwapchainImplementation[swapchain] = nullptr;
}


/***************************************************************************************************
 *
 * API ENTRYPOINTS
 *
 ***************************************************************************************************/

extern "C" {

void SwappyVk_determineDeviceExtensions(
    VkPhysicalDevice       physicalDevice,
    uint32_t               availableExtensionCount,
    VkExtensionProperties* pAvailableExtensions,
    uint32_t*              pRequiredExtensionCount,
    char**                 pRequiredExtensions)
{
    TRACE_CALL();
    SwappyVk& swappy = SwappyVk::getInstance();
    swappy.swappyVkDetermineDeviceExtensions(physicalDevice,
                                             availableExtensionCount, pAvailableExtensions,
                                             pRequiredExtensionCount, pRequiredExtensions);
}

void SwappyVk_setQueueFamilyIndex(
        VkDevice    device,
        VkQueue     queue,
        uint32_t    queueFamilyIndex)
{
    TRACE_CALL();
    SwappyVk& swappy = SwappyVk::getInstance();
    swappy.SetQueueFamilyIndex(device, queue, queueFamilyIndex);
}

bool SwappyVk_initAndGetRefreshCycleDuration(
        VkPhysicalDevice physicalDevice,
        VkDevice         device,
        VkSwapchainKHR   swapchain,
        uint64_t*        pRefreshDuration)
{
    TRACE_CALL();
    SwappyVk& swappy = SwappyVk::getInstance();
    return swappy.GetRefreshCycleDuration(physicalDevice, device, swapchain, pRefreshDuration);
}

void SwappyVk_setSwapInterval(
        VkDevice       device,
        VkSwapchainKHR swapchain,
        uint64_t       swap_ns)
{
    TRACE_CALL();
    SwappyVk& swappy = SwappyVk::getInstance();
    swappy.SetSwapIntervalNS(device, swapchain, swap_ns);
}

VkResult SwappyVk_queuePresent(
        VkQueue                 queue,
        const VkPresentInfoKHR* pPresentInfo)
{
    TRACE_CALL();
    SwappyVk& swappy = SwappyVk::getInstance();
    return swappy.QueuePresent(queue, pPresentInfo);
}

void SwappyVk_destroySwapchain(
        VkDevice                device,
        VkSwapchainKHR          swapchain)
{
    TRACE_CALL();
    SwappyVk& swappy = SwappyVk::getInstance();
    swappy.DestroySwapchain(device, swapchain);
}

}  // extern "C"

}  // namespace swappy
