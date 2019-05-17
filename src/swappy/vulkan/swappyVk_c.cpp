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

// API entry points

#include "swappy/swappyVk.h"

#include "SwappyVk.h"

extern "C" {

bool SwappyVk_initJNI(JNIEnv *env, jobject jactivity)
{
    TRACE_CALL();
    return swappy::SwappyVk::initJNI(env, jactivity);
}

void SwappyVk_determineDeviceExtensions(
    VkPhysicalDevice       physicalDevice,
    uint32_t               availableExtensionCount,
    VkExtensionProperties* pAvailableExtensions,
    uint32_t*              pRequiredExtensionCount,
    char**                 pRequiredExtensions)
{
    TRACE_CALL();
    swappy::SwappyVk& swappy = swappy::SwappyVk::getInstance();
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
    swappy::SwappyVk& swappy = swappy::SwappyVk::getInstance();
    swappy.SetQueueFamilyIndex(device, queue, queueFamilyIndex);
}

bool SwappyVk_initAndGetRefreshCycleDuration(
        VkPhysicalDevice physicalDevice,
        VkDevice         device,
        VkSwapchainKHR   swapchain,
        uint64_t*        pRefreshDuration)
{
    TRACE_CALL();
    swappy::SwappyVk& swappy = swappy::SwappyVk::getInstance();
    return swappy.GetRefreshCycleDuration(physicalDevice, device, swapchain, pRefreshDuration);
}

void SwappyVk_setSwapIntervalNS(
        VkDevice       device,
        VkSwapchainKHR swapchain,
        uint64_t       swap_ns)
{
    TRACE_CALL();
    swappy::SwappyVk& swappy = swappy::SwappyVk::getInstance();
    swappy.SetSwapIntervalNS(device, swapchain, swap_ns);
}

VkResult SwappyVk_queuePresent(
        VkQueue                 queue,
        const VkPresentInfoKHR* pPresentInfo)
{
    TRACE_CALL();
    swappy::SwappyVk& swappy = swappy::SwappyVk::getInstance();
    return swappy.QueuePresent(queue, pPresentInfo);
}

void SwappyVk_destroySwapchain(
        VkDevice                device,
        VkSwapchainKHR          swapchain)
{
    TRACE_CALL();
    swappy::SwappyVk& swappy = swappy::SwappyVk::getInstance();
    swappy.DestroySwapchain(device, swapchain);
}

}  // extern "C"