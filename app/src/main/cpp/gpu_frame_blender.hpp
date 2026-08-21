#pragma once

#include <cstdint>
#include <vulkan/vulkan.h>
#include "ahb_image_bridge.hpp"

namespace lsfg_android {

struct VulkanSession;

// GPU-accelerated frame blending using Vulkan compute shader
class GpuFrameBlender {
public:
    GpuFrameBlender() = default;
    ~GpuFrameBlender() { reset(); }
    
    // Initialize with Vulkan session and compute queue. Must be called once per session.
    bool init(VulkanSession &vk);
    
    // Blend two AHB images into output using GPU compute shader.
    // Safe to call multiple times per session.
    bool blend(const AhbImage &inputA, const AhbImage &inputB, const AhbImage &output);
    
    // Clean up GPU resources. Safe to call multiple times.
    void reset();
    
private:
    VulkanSession *vk_ = nullptr;
    VkShaderModule computeShader_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline computePipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    
    // Per-blend descriptor sets (recycled)
    VkDescriptorSet currentDescriptorSet_ = VK_NULL_HANDLE;
    
    // Command pool for compute submissions
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
};

} // namespace lsfg_android
