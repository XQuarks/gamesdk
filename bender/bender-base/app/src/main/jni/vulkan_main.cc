// Copyright 2016 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES

#include "vulkan_main.h"

#include <android_native_app_glue.h>
#include <cassert>
#include <vector>
#include <array>
#include <cstring>
#include <debug_marker.h>
#include <chrono>
#include "timing.h"

#include "vulkan_wrapper.h"
#include "bender_kit.h"
#include "bender_helpers.h"
#include "renderer.h"
#include "shader_state.h"
#include "polyhedron.h"
#include "mesh.h"
#include "texture.h"
#include "font.h"
#include "uniform_buffer.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include "shader_bindings.h"

#include <button.h>

using namespace BenderKit;
using namespace BenderHelpers;

/// Global Variables ...

std::vector<VkImageView> displayViews_;
std::vector<VkFramebuffer> framebuffers_;

struct Camera {
  glm::vec3 position = glm::vec3(0.0f, 0.0f, 3.0f);
  glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
} camera;

VkRenderPass render_pass;

struct AttachmentBuffer {
  VkFormat format;
  VkImage image;
  VkDeviceMemory device_memory;
  VkImageView image_view;
};
AttachmentBuffer depthBuffer;

// Android Native App pointer...
android_app *androidAppCtx = nullptr;
Device *device;
Renderer *renderer;

const glm::mat4 identity_mat4 = glm::mat4(1.0f);
float aspect_ratio;
float fov;
glm::mat4 view;
glm::mat4 proj;

std::shared_ptr<ShaderState> shaders;
std::vector<Mesh *> meshes;
Font *font;

auto lastTime = std::chrono::high_resolution_clock::now();
auto currentTime = lastTime;
float frameTime;
float totalTime;

std::vector<const char *> texFiles;
std::vector<std::shared_ptr<Texture>> textures;
std::vector<std::shared_ptr<Material>> materials;

bool windowResized = false;

void moveForward(){
  glm::vec3 forward = glm::normalize(camera.rotation * glm::vec3(0.0f, 0.0f, -1.0f));
  camera.position += forward * 2.0f * frameTime;
}
void moveBackward(){
  glm::vec3 forward = glm::normalize(camera.rotation * glm::vec3(0.0f, 0.0f, -1.0f));
  camera.position -= forward * 2.0f * frameTime;
}
void strafeLeft(){
  glm::vec3 right = glm::normalize(camera.rotation * glm::vec3(1.0f, 0.0f, 0.0f));
  camera.position -= right * (20.0f / device->getDisplaySize().width);
}
void strafeRight(){
  glm::vec3 right = glm::normalize(camera.rotation * glm::vec3(1.0f, 0.0f, 0.0f));
  camera.position += right * (20.0f / device->getDisplaySize().width);
}
void strafeUp(){
  glm::vec3 up = glm::normalize(camera.rotation * glm::vec3(0.0f, 1.0f, 0.0f));
  camera.position += up * (20.0f / device->getDisplaySize().height);
}
void strafeDown(){
  glm::vec3 up = glm::normalize(camera.rotation * glm::vec3(0.0f, 1.0f, 0.0f));
  camera.position -= up * (20.0f / device->getDisplaySize().height);
}
void createInstance(){
  meshes.push_back(createPolyhedron(*renderer, materials[0], 20));
  meshes[meshes.size() - 1]->translate(glm::vec3(rand() % 3, rand() % 3, rand() % 3));
}
void deleteInstance(){
  if (meshes.size() > 0) {
    meshes.pop_back();
  }
}

void createButtons(){
  Button::setScreenResolution(device->getDisplaySizeOriented());

  Button b0(-.7, .2, .6, .2, "<--");
  b0.onHold = strafeLeft;
  Button b1(-.2, .2, .6, .2, "-->");
  b1.onHold = strafeRight;
  Button b2(-.47, .2, .5, .2, "^");
  b2.onHold = strafeUp;
  Button b3(-.47, .2, .75, .2, "O");
  b3.onHold = strafeDown;
  Button b4(.43, .2, .6, .2, "Forward");
  b4.onHold = moveForward;
  Button b5(.43, .2, .8, .2, "Backward");
  b5.onHold = moveBackward;
  Button b6(.2, .2, .4, .2, "+1 Mesh");
  b6.onDown = createInstance;
  Button b7(.7, .2, .4, .2, "-1 Mesh");
  b7.onUp = deleteInstance;

  Input::buttons.push_back(b0);
  Input::buttons.push_back(b1);
  Input::buttons.push_back(b2);
  Input::buttons.push_back(b3);
  Input::buttons.push_back(b4);
  Input::buttons.push_back(b5);
  Input::buttons.push_back(b6);
  Input::buttons.push_back(b7);
}

void createTextures() {
  Timing::timer.time("Texture Creation", Timing::OTHER, [](){
    assert(androidAppCtx != nullptr);
    assert(device != nullptr);

    for (uint32_t i = 0; i < texFiles.size(); ++i) {
      textures.push_back(std::make_shared<Texture>(*device, *androidAppCtx, texFiles[i], VK_FORMAT_R8G8B8A8_SRGB));
    }
  });
}

void createMaterials() {
  Timing::timer.time("Materials Creation", Timing::OTHER, [](){
    for (uint32_t i = 0; i < textures.size(); ++i) {
      materials.push_back(std::make_shared<Material>(*renderer, shaders, textures[i]));
    }
  });
}

void createFrameBuffers(VkRenderPass &renderPass,
                        VkImageView depthView = VK_NULL_HANDLE) {
  displayViews_.resize(device->getDisplayImages().size());
  for (uint32_t i = 0; i < device->getDisplayImages().size(); i++) {
    VkImageViewCreateInfo viewCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .image = device->getDisplayImages()[i],
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = device->getDisplayFormat(),
        .components =
            {
                .r = VK_COMPONENT_SWIZZLE_R,
                .g = VK_COMPONENT_SWIZZLE_G,
                .b = VK_COMPONENT_SWIZZLE_B,
                .a = VK_COMPONENT_SWIZZLE_A,
            },
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        .flags = 0,
    };
    CALL_VK(vkCreateImageView(device->getDevice(), &viewCreateInfo, nullptr,
                              &displayViews_[i]));
  }

  framebuffers_.resize(device->getSwapchainLength());
  for (uint32_t i = 0; i < device->getSwapchainLength(); i++) {
    VkImageView attachments[2] = {
        displayViews_[i], depthView,
    };
    VkFramebufferCreateInfo fbCreateInfo{
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext = nullptr,
        .renderPass = renderPass,
        .layers = 1,
        .attachmentCount = 2,  // 2 if using depth
        .pAttachments = attachments,
        .width = static_cast<uint32_t>(device->getDisplaySize().width),
        .height = static_cast<uint32_t>(device->getDisplaySize().height),
    };

    CALL_VK(vkCreateFramebuffer(device->getDevice(), &fbCreateInfo, nullptr,
                                &framebuffers_[i]));
  }
}

void updateCamera(Input::Data *inputData) {
  if ((inputData->lastButton != nullptr && inputData->lastInputCount > 1) || inputData->lastButton == nullptr){
    camera.rotation =
        glm::quat(glm::vec3(0.0f, inputData->deltaX / device->getDisplaySize().width, 0.0f))
            * camera.rotation;
    camera.rotation *=
        glm::quat(glm::vec3(inputData->deltaY / device->getDisplaySize().height, 0.0f, 0.0f));
    camera.rotation = glm::normalize(camera.rotation);
  }

  glm::mat4 pre_rotate_mat = identity_mat4;
  glm::vec3 rotation_axis = glm::vec3(0.0f, 0.0f, -1.0f);
  if (device->getPretransformFlag() & VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR) {
    pre_rotate_mat = glm::rotate(pre_rotate_mat, glm::half_pi<float>(), rotation_axis);
  }
  else if (device->getPretransformFlag() & VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR) {
    pre_rotate_mat = glm::rotate(pre_rotate_mat, glm::three_over_two_pi<float>(), rotation_axis);
  }
  else if (device->getPretransformFlag() & VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR) {
    pre_rotate_mat = glm::rotate(pre_rotate_mat, glm::pi<float>(), rotation_axis);
  }

  view = pre_rotate_mat
      * glm::inverse(glm::translate(glm::mat4(1.0f), camera.position) * glm::mat4(camera.rotation));
  proj = glm::perspective(fov, aspect_ratio, 0.1f, 100.0f);
  proj[1][1] *= -1;
}

void updateInstances(Input::Data *inputData) {
  for (int x = 0; x < meshes.size(); x++) {
    meshes[x]->rotate(glm::vec3(0.0f, 1.0f, 1.0f), 90 * frameTime);
    meshes[x]->translate(.02f * glm::vec3(std::sin(2 * totalTime),
                                          std::sin(x * totalTime),
                                          std::cos(2 * totalTime)));

    meshes[x]->update(renderer->getCurrentFrame(), camera.position, view, proj);
  }
  renderer->updateLights(camera.position);
}

void handleInput(Input::Data *inputData){
  updateCamera(inputData);
  updateInstances(inputData);
}

void createShaderState() {
  VertexFormat vertex_format { {
        VertexElement::float3,
        VertexElement::float3,
        VertexElement::float2,
      },
  };

  shaders = std::make_shared<ShaderState>("mesh", vertex_format, *androidAppCtx, device->getDevice());
}

void createDepthBuffer() {
  depthBuffer.format = BenderHelpers::findDepthFormat(device);
  VkImageCreateInfo imageInfo = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .extent.width = device->getDisplaySize().width,
      .extent.height = device->getDisplaySize().height,
      .extent.depth = 1,
      .mipLevels = 1,
      .arrayLayers = 1,
      .format = depthBuffer.format,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };

  CALL_VK(vkCreateImage(device->getDevice(), &imageInfo, nullptr, &depthBuffer.image));

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(device->getDevice(), depthBuffer.image, &memRequirements);

  VkMemoryAllocateInfo allocInfo = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = memRequirements.size,
      .memoryTypeIndex = BenderHelpers::findMemoryType(memRequirements.memoryTypeBits,
                                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                       device->getPhysicalDevice()),
  };

  CALL_VK(vkAllocateMemory(device->getDevice(), &allocInfo, nullptr, &depthBuffer.device_memory))

  vkBindImageMemory(device->getDevice(), depthBuffer.image, depthBuffer.device_memory, 0);

  VkImageViewCreateInfo viewInfo = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .pNext = nullptr,
      .image = depthBuffer.image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = depthBuffer.format,
      .subresourceRange =
          {
              .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
              .baseMipLevel = 0,
              .levelCount = 1,
              .baseArrayLayer = 0,
              .layerCount = 1,
          },
      .flags = 0,
  };

  CALL_VK(vkCreateImageView(device->getDevice(), &viewInfo, nullptr, &depthBuffer.image_view));

}

bool InitVulkan(android_app *app) {
  Timing::timer.time("Initialization", Timing::OTHER, [app](){
    androidAppCtx = app;

    device = new Device(app->window);
    assert(device->isInitialized());
    device->setObjectName(reinterpret_cast<uint64_t>(device->getDevice()),
                          VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_EXT, "TEST NAME: VULKAN DEVICE");

  aspect_ratio = device->getDisplaySize().width / (float) device->getDisplaySize().height;
  auto horizontal_fov = glm::radians(60.0f);
  auto vertical_fov =
          static_cast<float>(2 * atan((0.5 * device->getDisplaySize().height)
                                      / (0.5 * device->getDisplaySize().width
                                         / tan(horizontal_fov / 2))));
  fov = (aspect_ratio > 1.0f) ? horizontal_fov : vertical_fov;

    VkAttachmentDescription color_description{
        .format = device->getDisplayFormat(),
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };

    VkAttachmentDescription depth_description{
        .format = BenderHelpers::findDepthFormat(device),
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkAttachmentReference color_attachment_reference = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };

    VkAttachmentReference depth_attachment_reference = {
        .attachment = 1,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    };

    VkSubpassDescription subpass_description{
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .flags = 0,
        .inputAttachmentCount = 0,
        .pInputAttachments = nullptr,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment_reference,
        .pResolveAttachments = nullptr,
        .pDepthStencilAttachment = &depth_attachment_reference,
        .preserveAttachmentCount = 0,
        .pPreserveAttachments = nullptr,
    };

    std::array<VkAttachmentDescription, 2> attachment_descriptions =
        {color_description, depth_description};

    VkRenderPassCreateInfo render_pass_createInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext = nullptr,
        .attachmentCount = static_cast<uint32_t>(attachment_descriptions.size()),
        .pAttachments = attachment_descriptions.data(),
        .subpassCount = 1,
        .pSubpasses = &subpass_description,
        .dependencyCount = 0,
        .pDependencies = nullptr,
    };
    CALL_VK(vkCreateRenderPass(device->getDevice(), &render_pass_createInfo, nullptr,
                               &render_pass));

    createShaderState();

    renderer = new Renderer(*device);

    Timing::timer.time("Mesh Creation", Timing::OTHER, [](){
      texFiles.push_back("textures/sample_texture.png");
      texFiles.push_back("textures/sample_texture2.png");

      createTextures();

      createMaterials();

      Timing::timer.time("Create Polyhedron", Timing::OTHER, [](){
        meshes.push_back(createPolyhedron(*renderer, materials[0], 20));
      });
    });

    createDepthBuffer();

    createFrameBuffers(render_pass, depthBuffer.image_view);

    font = new Font(*renderer, *androidAppCtx, FONT_SDF_PATH, FONT_INFO_PATH);

    createButtons();

  });

  Timing::printEvent(*Timing::timer.getLastMajorEvent());
  return true;
}

bool IsVulkanReady(void) { return device != nullptr && device->isInitialized(); }

void DeleteVulkan(void) {
  delete renderer;
  for (int x = 0; x < meshes.size(); x++) {
    delete meshes[x];
  }
  delete font;

  shaders->cleanup();
  shaders.reset();

  for (int i = 0; i < device->getSwapchainLength(); ++i) {
    vkDestroyImageView(device->getDevice(), displayViews_[i], nullptr);
    vkDestroyFramebuffer(device->getDevice(), framebuffers_[i], nullptr);
  }

  vkDestroyImageView(device->getDevice(), depthBuffer.image_view, nullptr);
  vkDestroyImage(device->getDevice(), depthBuffer.image, nullptr);
  vkFreeMemory(device->getDevice(), depthBuffer.device_memory, nullptr);

  vkDestroyRenderPass(device->getDevice(), render_pass, nullptr);

  delete device;
  device = nullptr;
}

bool VulkanDrawFrame(Input::Data *inputData) {
  if (windowResized) {
    OnOrientationChange();
  }
  currentTime = std::chrono::high_resolution_clock::now();
  frameTime = std::chrono::duration<float>(currentTime - lastTime).count();
  lastTime = currentTime;
  totalTime += frameTime;

  Timing::timer.time("Handle Input", Timing::OTHER, [inputData]() {
    handleInput(inputData);
  });

  renderer->beginFrame();
  Timing::timer.time("Start Frame", Timing::START_FRAME, []() {
    Timing::timer.time("PrimaryCommandBufferRecording", Timing::OTHER, []() {
      renderer->beginPrimaryCommandBufferRecording();

      // Now we start a renderpass. Any draw command has to be recorded in a
      // renderpass
      std::array<VkClearValue, 2> clear_values = {};
      clear_values[0].color = {{0.0f, 0.34f, 0.90f, 1.0}};
      clear_values[1].depthStencil = {1.0f, 0};

      VkRenderPassBeginInfo render_pass_beginInfo{
          .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
          .pNext = nullptr,
          .renderPass = render_pass,
          .framebuffer = framebuffers_[renderer->getCurrentFrame()],
          .renderArea = {.offset =
              {
                  .x = 0, .y = 0,
              },
              .extent = device->getDisplaySize()},
          .clearValueCount = static_cast<uint32_t>(clear_values.size()),
          .pClearValues = clear_values.data(),
      };

      Timing::timer.time("Render Pass", Timing::OTHER, [render_pass_beginInfo]() {
        vkCmdBeginRenderPass(renderer->getCurrentCommandBuffer(), &render_pass_beginInfo,
                             VK_SUBPASS_CONTENTS_INLINE);

        device->insertDebugMarker(renderer->getCurrentCommandBuffer(),
                                  "TEST MARKER: PIPELINE BINDING",
                                  {1.0f, 0.0f, 1.0f, 0.0f});

        int total_triangles = 0;
        for (int x = 0; x < meshes.size(); x++) {
          meshes[x]->updatePipeline(render_pass);
          meshes[x]->submitDraw(renderer->getCurrentCommandBuffer(), renderer->getCurrentFrame());
          total_triangles += meshes[x]->getTrianglesCount();
        }

        char output_string[50];
        if (meshes.size() > 1) {
          sprintf(output_string, "%d meshes", (int) meshes.size());
        } else {
          sprintf(output_string, "%d mesh", (int) meshes.size());
        }
        if (total_triangles > 1) {
          sprintf(output_string + strlen(output_string), " %d triangles", total_triangles);
        } else {
          sprintf(output_string + strlen(output_string), " %d triangle", total_triangles);
        }
        font->drawString(output_string,
                         1.0f,
                         -.98f,
                         -.98f,
                         renderer->getCurrentCommandBuffer(),
                         render_pass,
                         renderer->getCurrentFrame());

        int fps;
        float frametime;
        char fpsString[50];
        Timing::timer.getFramerate(500,
                                   Timing::timer.getLastMajorEvent()->number,
                                   &fps,
                                   &frametime);
        sprintf(fpsString, "%2.d FPS  %.3f ms", fps, frametime);
        font->drawString(fpsString,
                         1.0f,
                         -0.98f,
                         -0.88f,
                         renderer->getCurrentCommandBuffer(),
                         render_pass,
                         renderer->getCurrentFrame());

        for (Button button : Input::buttons) {
          button.drawButton(render_pass, font, renderer);
        }

        vkCmdEndRenderPass(renderer->getCurrentCommandBuffer());
      });
      renderer->endPrimaryCommandBufferRecording();
    });
    Timing::timer.time("End Frame", Timing::OTHER, []() {
      renderer->endFrame();
    });
  });
  return true;
}

void ResizeCallback(ANativeActivity *activity, ANativeWindow *window){
  windowResized = true;
}

void OnOrientationChange() {
  vkDeviceWaitIdle(device->getDevice());

  for (int i = 0; i < device->getSwapchainLength(); ++i) {
    vkDestroyImageView(device->getDevice(), displayViews_[i], nullptr);
    vkDestroyFramebuffer(device->getDevice(), framebuffers_[i], nullptr);
  }
  vkDestroyImageView(device->getDevice(), depthBuffer.image_view, nullptr);
  vkDestroyImage(device->getDevice(), depthBuffer.image, nullptr);
  vkFreeMemory(device->getDevice(), depthBuffer.device_memory, nullptr);

  device->createSwapChain(device->getSwapchain());
  createDepthBuffer();
  createFrameBuffers(render_pass, depthBuffer.image_view);
  Button::setScreenResolution(device->getDisplaySizeOriented());
  font->setResolutionRatios();
  windowResized = false;
}