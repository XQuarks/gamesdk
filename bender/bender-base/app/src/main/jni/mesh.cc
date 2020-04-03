//
// Created by mattkw on 10/31/2019.
//

#include <glm/gtc/matrix_transform.hpp>

#include "mesh.h"
#include "material.h"
#include "shader_bindings.h"

Mesh::Mesh(Renderer &renderer,
           std::shared_ptr<Material> material,
           std::shared_ptr<Geometry> geometry) :
    renderer_(renderer), material_(material), geometry_(geometry), bounding_box_dirty_(true) {
  position_ = glm::vec3(0.0f, 0.0f, 0.0f);
  rotation_ = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  scale_ = glm::vec3(1.0f, 1.0f, 1.0f);

  mesh_buffer_ = std::make_unique<UniformBufferObject<ModelViewProjection>>(renderer_.GetDevice());
  CreateMeshDescriptorSetLayout();
  CreateMeshDescriptors();
  ComputeBoundingBoxWorldSpace();
}

Mesh::Mesh(Renderer &renderer,
           std::shared_ptr<Material> material,
           const std::vector<MeshVertex> &vertex_data,
           const std::vector<uint16_t> &index_data) :
        Mesh(renderer,
             material,
             std::make_shared<Geometry>(renderer.GetDevice(), vertex_data, index_data)) {
  ComputeBoundingBoxWorldSpace();
}

Mesh::Mesh(const Mesh &other, std::shared_ptr<Material> material) :
    renderer_(other.renderer_),
    material_(material),
    geometry_(other.geometry_),
    position_(other.position_),
    rotation_(other.rotation_),
    scale_(other.scale_),
    bounding_box_dirty_(true) {
  mesh_buffer_ = std::make_unique<UniformBufferObject<ModelViewProjection>>(renderer_.GetDevice());
  CreateMeshDescriptorSetLayout();
  CreateMeshDescriptors();
  ComputeBoundingBoxWorldSpace();
}

Mesh::Mesh(const Mesh &other, std::shared_ptr<Geometry> geometry) :
    renderer_(other.renderer_),
    material_(other.material_),
    geometry_(geometry),
    position_(other.position_),
    rotation_(other.rotation_),
    scale_(other.scale_),
    bounding_box_dirty_(true) {
  mesh_buffer_ = std::make_unique<UniformBufferObject<ModelViewProjection>>(renderer_.GetDevice());
  CreateMeshDescriptorSetLayout();
  CreateMeshDescriptors();
  ComputeBoundingBoxWorldSpace();
}

Mesh::~Mesh() {
  vkDeviceWaitIdle(renderer_.GetVulkanDevice());
  vkDestroyPipeline(renderer_.GetVulkanDevice(), pipeline_, nullptr);
  vkDestroyPipelineLayout(renderer_.GetVulkanDevice(), layout_, nullptr);
  vkDestroyDescriptorSetLayout(renderer_.GetVulkanDevice(), mesh_descriptors_layout_, nullptr);
  mesh_buffer_.reset();
  pipeline_ = VK_NULL_HANDLE;
  layout_ = VK_NULL_HANDLE;
}

void Mesh::CreateMeshDescriptors() {
  std::vector<VkDescriptorSetLayout> layouts(renderer_.GetDevice().GetDisplayImages().size(),
                                             mesh_descriptors_layout_);

  VkDescriptorSetAllocateInfo alloc_info = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
          .descriptorPool = renderer_.GetDescriptorPool(),
          .descriptorSetCount = static_cast<uint32_t>(renderer_.GetDevice().GetDisplayImages().size()),
          .pSetLayouts = layouts.data(),
  };

  mesh_descriptor_sets_.resize(renderer_.GetDevice().GetDisplayImages().size());
  CALL_VK(vkAllocateDescriptorSets(renderer_.GetVulkanDevice(),
                                   &alloc_info,
                                   mesh_descriptor_sets_.data()));

  for (size_t i = 0; i < renderer_.GetDevice().GetDisplayImages().size(); i++) {
    VkDescriptorBufferInfo buffer_info = {
            .buffer = mesh_buffer_->GetBuffer(i),
            .offset = 0,
            .range = sizeof(ModelViewProjection),
    };

    std::array<VkWriteDescriptorSet, 1> descriptor_writes = {};

    descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_writes[0].dstSet = mesh_descriptor_sets_[i];
    descriptor_writes[0].dstBinding = VERTEX_BINDING_MODEL_VIEW_PROJECTION;
    descriptor_writes[0].dstArrayElement = 0;
    descriptor_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptor_writes[0].descriptorCount = 1;
    descriptor_writes[0].pBufferInfo = &buffer_info;

    vkUpdateDescriptorSets(renderer_.GetVulkanDevice(),
                           descriptor_writes.size(),
                           descriptor_writes.data(),
                           0,
                           nullptr);

  }
}

void Mesh::CreateMeshPipeline(VkRenderPass render_pass) {
  std::array<VkDescriptorSetLayout, 3> layouts;

  layouts[BINDING_SET_MESH] = mesh_descriptors_layout_;
  layouts[BINDING_SET_MATERIAL] = material_->GetMaterialDescriptorSetLayout();
  layouts[BINDING_SET_LIGHTS] = renderer_.GetLightsDescriptorSetLayout();

  VkPipelineLayoutCreateInfo pipeline_layout_info {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .pNext = nullptr,
      .setLayoutCount = layouts.size(),
      .pSetLayouts = layouts.data(),
      .pushConstantRangeCount = 0,
      .pPushConstantRanges = nullptr,
  };

  CALL_VK(vkCreatePipelineLayout(renderer_.GetVulkanDevice(), &pipeline_layout_info, nullptr,
                                 &layout_))

  VkGraphicsPipelineCreateInfo pipeline_info = renderer_.GetDefaultPipelineInfo(
      layout_,
      render_pass);

  material_->FillPipelineInfo(&pipeline_info);

  CALL_VK(vkCreateGraphicsPipelines(
      renderer_.GetVulkanDevice(),
      renderer_.GetPipelineCache(), 1,
      &pipeline_info,
      nullptr, &pipeline_));
}

void Mesh::CreateMeshDescriptorSetLayout() {
  VkDescriptorSetLayoutBinding ubo_layout_binding = {
          .binding = VERTEX_BINDING_MODEL_VIEW_PROJECTION,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .pImmutableSamplers = nullptr,
          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
  };

  std::array<VkDescriptorSetLayoutBinding, 1> bindings = {ubo_layout_binding};
  VkDescriptorSetLayoutCreateInfo layout_info = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
          .bindingCount = bindings.size(),
          .pBindings = bindings.data(),
  };

  CALL_VK(vkCreateDescriptorSetLayout(renderer_.GetVulkanDevice(), &layout_info, nullptr,
                                      &mesh_descriptors_layout_));
}

void Mesh::UpdatePipeline(VkRenderPass render_pass) {
  if (pipeline_ != VK_NULL_HANDLE)
    return;

  CreateMeshPipeline(render_pass);
}

void Mesh::Update(uint_t frame_index, Camera &camera) {
  glm::mat4 model = GetTransform(true);
  glm::mat4 mvp = camera.proj * camera.view * model;

  mesh_buffer_->Update(frame_index, [&mvp, &model, &camera](auto &ubo) {
    ubo.mvp = camera.prerotation * mvp;
    ubo.model = model;
    ubo.inv_transpose = glm::transpose(glm::inverse(model));
  });
}

void Mesh::SubmitDraw(VkCommandBuffer cmd_buffer, uint_t frame_index) const {
  vkCmdBindPipeline(cmd_buffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline_);

  geometry_->Bind(cmd_buffer);

  vkCmdBindDescriptorSets(cmd_buffer,
                          VK_PIPELINE_BIND_POINT_GRAPHICS,
                          layout_,
                          BINDING_SET_MESH,
                          1,
                          &mesh_descriptor_sets_[frame_index],
                          0,
                          nullptr);

  VkDescriptorSet lights_descriptor_set = renderer_.GetLightsDescriptorSet(frame_index);

  vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          layout_, BINDING_SET_LIGHTS, 1, &lights_descriptor_set, 0, nullptr);

  VkDescriptorSet material_descriptor_set = material_->GetMaterialDescriptorSet(frame_index);

  vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          layout_, BINDING_SET_MATERIAL, 1, &material_descriptor_set, 0, nullptr);

  vkCmdDrawIndexed(cmd_buffer,
                   geometry_->GetIndexCount(),
                   1, 0, 0, 0);
}

void Mesh::Translate(glm::vec3 offset) {
  position_ += offset;
  bounding_box_dirty_ = true;
}

void Mesh::Rotate(glm::vec3 axis, float angle) {
  rotation_ *= glm::angleAxis(glm::radians(angle), axis);
  rotation_ = glm::normalize(rotation_);
  bounding_box_dirty_ = true;

}

void Mesh::Scale(glm::vec3 scaling) {
  scale_ *= scaling;
  bounding_box_dirty_ = true;
}

void Mesh::SetPosition(glm::vec3 position) {
  position_ = position;
  bounding_box_dirty_ = true;
}

void Mesh::SetRotation(glm::vec3 axis, float angle) {
  rotation_ = glm::angleAxis(glm::radians(angle), axis);
  bounding_box_dirty_ = true;
}

void Mesh::SetScale(glm::vec3 scale) {
  scale_ = scale;
  bounding_box_dirty_ = true;
}

glm::vec3 Mesh::GetPosition() const {
  return position_;
}

glm::quat Mesh::GetRotation() const {
  return rotation_;
}

glm::vec3 Mesh::GetScale() const {
  return scale_;
}

glm::mat4 Mesh::GetTransform(bool for_mvp /* = false */) const {
  glm::mat4 position = glm::translate(glm::mat4(1.0f), position_);
  glm::mat4 scale = glm::scale(glm::mat4(1.0f), for_mvp ? scale_ * geometry_->GetScaleFactor() : scale_);
  return position * glm::mat4(rotation_) * scale;
}

void Mesh::ComputeBoundingBoxWorldSpace() {
  glm::mat4 finalTransform = GetTransform();
  BoundingBox originalBox = geometry_->GetBoundingBox();

  glm::vec3 xMin = finalTransform[0] * (originalBox.min.x);
  glm::vec3 xMax = finalTransform[0] * (originalBox.max.x);

  glm::vec3 yMin = finalTransform[1] * (originalBox.min.y);
  glm::vec3 yMax = finalTransform[1] * (originalBox.max.y);

  glm::vec3 zMin = finalTransform[2] * (originalBox.min.z);
  glm::vec3 zMax = finalTransform[2] * (originalBox.max.z);

  world_space_box_.min = glm::min(xMin, xMax) + glm::min(yMin ,yMax) + glm::min(zMin, zMax) + glm::vec3(finalTransform[3]);
  world_space_box_.max = glm::max(xMin, xMax) + glm::max(yMin ,yMax) + glm::max(zMin, zMax) + glm::vec3(finalTransform[3]);
  world_space_box_.center = (world_space_box_.min + world_space_box_.max) * .5f;
  bounding_box_dirty_ = false;
}

BoundingBox Mesh::GetBoundingBoxWorldSpace() {
  if (bounding_box_dirty_) {
    ComputeBoundingBoxWorldSpace();
  }
  return world_space_box_;
}

int Mesh::GetTrianglesCount() const {
  return geometry_->GetIndexCount() / 3;
}