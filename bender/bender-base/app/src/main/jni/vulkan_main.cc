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

#include "vulkan_main.h"
#include <android_native_app_glue.h>
#include <cassert>
#include <vector>
#include <cstring>
#include <debug_marker.h>
#include "vulkan_wrapper.h"

#include "bender_kit.h"
#include "renderer.h"
#include "shader_state.h"
#include "geometry.h"

/// Global Variables ...

std::vector<VkImageView> displayViews_;
std::vector<VkFramebuffer> framebuffers_;

VkRenderPass render_pass;

struct VulkanGfxPipelineInfo {
  VkPipelineLayout layout_;
  VkPipelineCache cache_;
  VkPipeline pipeline_;
};
VulkanGfxPipelineInfo gfxPipeline;

// Android Native App pointer...
android_app *androidAppCtx = nullptr;
BenderKit::Device *device;
Geometry *geometry;
Renderer *renderer;

void createFrameBuffers(VkRenderPass &renderPass,
                        VkImageView depthView = VK_NULL_HANDLE) {
  // create image view for each swapchain image
  displayViews_.resize(device->getDisplayImagesSize());
  for (uint32_t i = 0; i < device->getDisplayImagesSize(); i++) {
    VkImageViewCreateInfo viewCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .image = device->getDisplayImage(i),
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
        .attachmentCount = 1,  // 2 if using depth
        .pAttachments = attachments,
        .width = static_cast<uint32_t>(device->getDisplaySize().width),
        .height = static_cast<uint32_t>(device->getDisplaySize().height),
    };
    fbCreateInfo.attachmentCount = (depthView == VK_NULL_HANDLE ? 1 : 2);

    CALL_VK(vkCreateFramebuffer(device->getDevice(), &fbCreateInfo, nullptr,
                                &framebuffers_[i]));
  }
}

void createGraphicsPipeline() {
  ShaderState shaderState("triangle", androidAppCtx, device->getDevice());
  shaderState.addVertexInputBinding(0, 6 * sizeof(float));
  shaderState.addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
  shaderState.addVertexAttributeDescription(0, 1, VK_FORMAT_R32G32B32_SFLOAT, 3 * sizeof(float));

  VkViewport viewport{
      .x = 0.0f,
      .y = 0.0f,
      .width = static_cast<float>(device->getDisplaySize().width),
      .height = static_cast<float>(device->getDisplaySize().height),
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
  };

  VkRect2D scissor{
      .offset = {0, 0},
      .extent = device->getDisplaySize(),
  };

  VkPipelineViewportStateCreateInfo pipelineViewportState{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .pViewports = &viewport,
      .scissorCount = 1,
      .pScissors = &scissor,
  };

  // Describes how the GPU should rasterize pixels from polygons
  VkPipelineRasterizationStateCreateInfo pipelineRasterizationState{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .lineWidth = 1.0f,
      .cullMode = VK_CULL_MODE_BACK_BIT,
      .frontFace = VK_FRONT_FACE_CLOCKWISE,
      .depthBiasEnable = VK_FALSE,
      .depthBiasConstantFactor = 0.0f,
      .depthBiasClamp = 0.0f,
      .depthBiasSlopeFactor = 0.0f,
  };

  // Multisample anti-aliasing setup
  VkPipelineMultisampleStateCreateInfo pipelineMultisampleState{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .pNext = nullptr,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
      .sampleShadingEnable = VK_FALSE,
      .minSampleShading = 0,
      .pSampleMask = nullptr,
      .alphaToCoverageEnable = VK_FALSE,
      .alphaToOneEnable = VK_FALSE,
  };

  // Describes how to blend pixels from past framebuffers to current framebuffers
  // Could be used for transparency or cool screen-space effects
  VkPipelineColorBlendAttachmentState attachmentStates{
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
      .blendEnable = VK_FALSE,
  };

  VkPipelineColorBlendStateCreateInfo colorBlendInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .pNext = nullptr,
      .logicOpEnable = VK_FALSE,
      .logicOp = VK_LOGIC_OP_COPY,
      .attachmentCount = 1,
      .pAttachments = &attachmentStates,
      .flags = 0,
  };

  // Describes the layout of things such as uniforms
  VkPipelineLayoutCreateInfo pipelineLayoutInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .pNext = nullptr,
      .setLayoutCount = 0,
      .pSetLayouts = nullptr,
      .pushConstantRangeCount = 0,
      .pPushConstantRanges = nullptr,
  };
  CALL_VK(vkCreatePipelineLayout(device->getDevice(), &pipelineLayoutInfo, nullptr,
                                 &gfxPipeline.layout_))

  VkPipelineCacheCreateInfo pipelineCacheInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
      .pNext = nullptr,
      .initialDataSize = 0,
      .pInitialData = nullptr,
      .flags = 0,  // reserved, must be 0
  };

  CALL_VK(vkCreatePipelineCache(device->getDevice(), &pipelineCacheInfo, nullptr,
                                &gfxPipeline.cache_));

  VkGraphicsPipelineCreateInfo pipelineInfo{
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .stageCount = 2,
      .pTessellationState = nullptr,
      .pViewportState = &pipelineViewportState,
      .pRasterizationState = &pipelineRasterizationState,
      .pMultisampleState = &pipelineMultisampleState,
      .pDepthStencilState = nullptr,
      .pColorBlendState = &colorBlendInfo,
      .pDynamicState = nullptr,
      .layout = gfxPipeline.layout_,
      .renderPass = render_pass,
      .subpass = 0,
      .basePipelineHandle = VK_NULL_HANDLE,
      .basePipelineIndex = 0,
  };

  shaderState.updatePipelineInfo(pipelineInfo);

  CALL_VK(vkCreateGraphicsPipelines(device->getDevice(), gfxPipeline.cache_, 1, &pipelineInfo,
                                    nullptr, &gfxPipeline.pipeline_));
  LOGI("Setup Graphics Pipeline");
  shaderState.cleanup();
}

bool InitVulkan(android_app *app) {
  androidAppCtx = app;

  device = new BenderKit::Device(app->window);
  assert(device->isInitialized());
  DebugMarker::setObjectName(device->getDevice(), (uint64_t)device->getDevice(),
      VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_EXT, "TEST NAME: VULKAN DEVICE");

  renderer = new Renderer(device);

  VkAttachmentDescription attachment_descriptions{
      .format = device->getDisplayFormat(),
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
  };

  VkAttachmentReference colour_reference = {
      .attachment = 0,
      .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
  };

  VkSubpassDescription subpass_description{
      .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .flags = 0,
      .inputAttachmentCount = 0,
      .pInputAttachments = nullptr,
      .colorAttachmentCount = 1,
      .pColorAttachments = &colour_reference,
      .pResolveAttachments = nullptr,
      .pDepthStencilAttachment = nullptr,
      .preserveAttachmentCount = 0,
      .pPreserveAttachments = nullptr,
  };
  VkRenderPassCreateInfo render_pass_createInfo{
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .pNext = nullptr,
      .attachmentCount = 1,
      .pAttachments = &attachment_descriptions,
      .subpassCount = 1,
      .pSubpasses = &subpass_description,
      .dependencyCount = 0,
      .pDependencies = nullptr,
  };
  CALL_VK(vkCreateRenderPass(device->getDevice(), &render_pass_createInfo, nullptr,
                             &render_pass));

  // ---------------------------------------------
  // Create the triangle vertex buffer with indices
  const std::vector<float> vertexData = {
      -0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f,
      0.5f, -0.5f, 0.0f, 0.0f, 1.0f, 0.0f,
      0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f,
      -0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 1.0f,
  };

  const std::vector<u_int16_t> indexData = {
      0, 1, 2, 2, 3, 0
  };

  geometry = new Geometry(device, vertexData, indexData);

  createFrameBuffers(render_pass);

  createGraphicsPipeline();

  return true;
}

// IsVulkanReady():
//    native app poll to see if we are ready to draw...
bool IsVulkanReady(void) { return device != nullptr && device->isInitialized(); }

void DeleteVulkan(void) {
  delete renderer;

  delete geometry;

  for (int i = 0; i < device->getSwapchainLength(); ++i) {
    vkDestroyImageView(device->getDevice(), displayViews_[i], nullptr);
    vkDestroyFramebuffer(device->getDevice(), framebuffers_[i], nullptr);
  }

  vkDestroyPipeline(device->getDevice(), gfxPipeline.pipeline_, nullptr);
  vkDestroyPipelineCache(device->getDevice(), gfxPipeline.cache_, nullptr);
  vkDestroyPipelineLayout(device->getDevice(), gfxPipeline.layout_, nullptr);
  vkDestroyRenderPass(device->getDevice(), render_pass, nullptr);

  delete device;
  device = nullptr;
}

bool VulkanDrawFrame(void) {
  TRACE_BEGIN_SECTION("Draw Frame");

  renderer->beginFrame();
  renderer->beginPrimaryCommandBufferRecording();

  // Now we start a renderpass. Any draw command has to be recorded in a
  // renderpass
  VkClearValue clearVals{
      .color.float32[0] = 0.0f,
      .color.float32[1] = 0.34f,
      .color.float32[2] = 0.90f,
      .color.float32[3] = 1.0f,
  };

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
      .clearValueCount = 1,
      .pClearValues = &clearVals};

  vkCmdBeginRenderPass(renderer->getCurrentCommandBuffer(), &render_pass_beginInfo,
                       VK_SUBPASS_CONTENTS_INLINE);

  float color[4] = {1.0f, 0.0f, 1.0f, 0.0f};
  DebugMarker::insert(renderer->getCurrentCommandBuffer(), "TEST MARKER: PIPELINE BINDING", color);

  vkCmdBindPipeline(renderer->getCurrentCommandBuffer(),
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    gfxPipeline.pipeline_);

  geometry->bind(renderer->getCurrentCommandBuffer());

  vkCmdDrawIndexed(renderer->getCurrentCommandBuffer(),
                   static_cast<u_int32_t>(geometry->getIndexCount()),
                   1, 0, 0, 0);

  vkCmdEndRenderPass(renderer->getCurrentCommandBuffer());

  renderer->endPrimaryCommandBufferRecording();
  renderer->endFrame();

  TRACE_END_SECTION();

  return true;
}
