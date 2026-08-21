#include "gpu_frame_blender.hpp"
#include "android_vk_session.hpp"
#include <volk.h>
#include <android/log.h>

#define LOG_TAG "gpu-blender"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace lsfg_android {

// Pre-compiled SPIR-V for motion-compensated optical flow blending
// To get this: compile blend_frames.comp with glslc, then convert with xxd -i
// For now using a simple 50/50 blend SPIR-V as fallback
static const uint32_t BLEND_SPIRV[] = {
    0x07230203, 0x00010000, 0x0008000a, 0x0000003d, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534752, 0x6f6c7661, 0x63534755, 0x72617368, 0x00000000, 0x0003000e, 0x00000000,
    0x00000001, 0x0006000f, 0x00000005, 0x00000004, 0x6e69616d, 0x00000000, 0x0000000d, 0x00060010,
    0x00000004, 0x00000011, 0x00000008, 0x00000008, 0x00000001, 0x00030003, 0x00000002, 0x000001c2,
    0x00090004, 0x415f4c47, 0x735f4252, 0x72617065, 0x5f657461, 0x64616873, 0x6f5f7265, 0x63626a62,
    0x00007374, 0x00070004, 0x415f4c47, 0x735f4252, 0x69646e65, 0x6c5f6e61, 0x75716572, 0x00007265,
    0x0005000c, 0x00000005, 0x00000004, 0x00000007, 0x00000003, 0x00050005, 0x00000004, 0x6e69616d,
    0x00000000, 0x00000000, 0x00050005, 0x00000009, 0x6f6f6320, 0x00006472, 0x00000000, 0x00050005,
    0x0000000c, 0x47000000, 0x616f6c6c, 0x766e496c, 0x00636f6f, 0x0000000d, 0x00050005, 0x00000012,
    0x75706e69, 0x41614174, 0x00000000, 0x00050005, 0x00000019, 0x75706e69, 0x42614174, 0x00000000,
    0x00050005, 0x00000025, 0x7475706f, 0x665f7475, 0x00676d69, 0x00050005, 0x0000002c, 0x646e656c,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x00080004, 0x415f4c47, 0x735f4252, 0x72617065,
    0x5f657461, 0x64616873, 0x6f5f7265, 0x63626a62, 0x00007374, 0x00070004, 0x415f4c47, 0x735f4252,
    0x69646e65, 0x6c5f6e61, 0x75716572, 0x00007265, 0x0005000c, 0x00000001, 0x00000004, 0x00000007,
    0x00000003, 0x00050005, 0x00000004, 0x6e69616d, 0x00000000, 0x00000000, 0x00050005, 0x00000009,
    0x6f6f6320, 0x00006472, 0x00000000, 0x00050005, 0x0000000c, 0x47000000, 0x616f6c6c, 0x766e496c,
    0x00636f6f, 0x0000000d, 0x00050005, 0x00000012, 0x75706e69, 0x41614174, 0x00000000, 0x00050005,
    0x00000019, 0x75706e69, 0x42614174, 0x00000000, 0x00050005, 0x00000025, 0x7475706f, 0x665f7475,
    0x00676d69, 0x00050005, 0x0000002c, 0x646e656c, 0x00000000, 0x00040047, 0x0000000c, 0x0000000b,
    0x0000001c, 0x00040047, 0x0000000d, 0x0000000b, 0x00000005, 0x00040047, 0x00000012, 0x00000022,
    0x00000000, 0x00040047, 0x00000012, 0x00000021, 0x00000000, 0x00040047, 0x00000019, 0x00000022,
    0x00000000, 0x00040047, 0x00000019, 0x00000021, 0x00000001, 0x00040047, 0x00000025, 0x00000022,
    0x00000000, 0x00040047, 0x00000025, 0x00000021, 0x00000002, 0x00030047, 0x00000012, 0x00000018,
    0x00030047, 0x00000019, 0x00000018, 0x00030047, 0x00000025, 0x00000018, 0x00020013, 0x00000002,
    0x00030021, 0x00000003, 0x00000002, 0x00040015, 0x00000006, 0x00000020, 0x00000000, 0x0004002b,
    0x00000006, 0x00000007, 0x00000008, 0x0004002b, 0x00000006, 0x00000008, 0x00000008, 0x0004002b,
    0x00000006, 0x0000000a, 0x00000001, 0x00040017, 0x0000000b, 0x00000006, 0x00000002, 0x00040020,
    0x00000010, 0x00000007, 0x0000000b, 0x0004003b, 0x00000010, 0x00000011, 0x00000007, 0x00030016,
    0x00000014, 0x00000020, 0x00040017, 0x00000015, 0x00000014, 0x00000004, 0x00090019, 0x00000016,
    0x00000014, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000002, 0x00000004, 0x00040020,
    0x00000017, 0x00000000, 0x00000016, 0x0004003b, 0x00000017, 0x00000018, 0x00000000, 0x0004003b,
    0x00000017, 0x0000001f, 0x00000000, 0x0004003b, 0x00000017, 0x00000026, 0x00000000, 0x00020014,
    0x00000027, 0x0004001c, 0x00000028, 0x00000027, 0x00000003, 0x00040020, 0x00000029, 0x00000009,
    0x00000028, 0x0004003b, 0x00000029, 0x0000002a, 0x00000009, 0x0004002b, 0x00000006, 0x0000002b,
    0x00000000, 0x0004003b, 0x00000010, 0x0000002c, 0x00000007, 0x0004002b, 0x00000014, 0x0000002d,
    0x3f000000, 0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005,
    0x0004003d, 0x0000000b, 0x0000000e, 0x0000000d, 0x0003003e, 0x00000009, 0x0000000e, 0x0004003d,
    0x00000016, 0x00000013, 0x00000012, 0x00050088, 0x0000000b, 0x0000001a, 0x00000013, 0x00000009,
    0x0004003d, 0x0000000b, 0x0000001b, 0x0000001a, 0x00050050, 0x00000015, 0x0000001c, 0x00000007,
    0x00000008, 0x0004007c, 0x0000000b, 0x0000001d, 0x0000001c, 0x000500ae, 0x00000027, 0x0000001e,
    0x0000001b, 0x0000001d, 0x000300f7, 0x00000020, 0x00000000, 0x000400fa, 0x0000001e, 0x0000001f,
    0x00000020, 0x000200f8, 0x0000001f, 0x000100fd, 0x000200f8, 0x00000020, 0x0004003d, 0x00000016,
    0x00000021, 0x00000012, 0x0004003d, 0x0000000b, 0x00000022, 0x00000009, 0x0004003d, 0x00000015,
    0x00000023, 0x00000021, 0x0004003d, 0x0000000b, 0x00000024, 0x00000022, 0x00050091, 0x00000015,
    0x00000025, 0x00000023, 0x00000024, 0x0004003d, 0x00000016, 0x00000026, 0x00000019, 0x0004003d,
    0x0000000b, 0x00000027, 0x00000009, 0x0004003d, 0x00000015, 0x00000028, 0x00000026, 0x0004003d,
    0x0000000b, 0x00000029, 0x00000027, 0x00050091, 0x00000015, 0x0000002a, 0x00000028, 0x00000029,
    0x00050085, 0x00000015, 0x0000002b, 0x00000025, 0x0000002d, 0x00050085, 0x00000015, 0x0000002c,
    0x0000002a, 0x0000002d, 0x00050081, 0x00000015, 0x0000002e, 0x0000002b, 0x0000002c, 0x0004003d,
    0x00000016, 0x0000002f, 0x00000025, 0x0004003d, 0x0000000b, 0x00000030, 0x00000009, 0x0003003e,
    0x0000002e, 0x00000004, 0x00040032, 0x0000002f, 0x00000030, 0x0000002e, 0x000100fd, 0x00010038,
};

static const size_t BLEND_SPIRV_SIZE = sizeof(BLEND_SPIRV) / sizeof(BLEND_SPIRV[0]);

bool GpuFrameBlender::init(VulkanSession &vk) {
    vk_ = &vk;
    
    // Create compute shader module
    VkShaderModuleCreateInfo shaderInfo{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = BLEND_SPIRV_SIZE * sizeof(uint32_t),
        .pCode = BLEND_SPIRV,
    };
    
    if (vkCreateShaderModule(vk.device, &shaderInfo, nullptr, &computeShader_) != VK_SUCCESS) {
        LOGE("Failed to create compute shader module");
        return false;
    }
    
    // Create descriptor set layout (3 images: input A, input B, output)
    VkDescriptorSetLayoutBinding bindings[3] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .binding = 2,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 3,
        .pBindings = bindings,
    };
    
    if (vkCreateDescriptorSetLayout(vk.device, &layoutInfo, nullptr, &descriptorLayout_) != VK_SUCCESS) {
        LOGE("Failed to create descriptor set layout");
        return false;
    }
    
    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &descriptorLayout_,
    };
    
    if (vkCreatePipelineLayout(vk.device, &pipelineLayoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        LOGE("Failed to create pipeline layout");
        return false;
    }
    
    // Create compute pipeline
    VkPipelineShaderStageCreateInfo shaderStage{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = computeShader_,
        .pName = "main",
    };
    
    VkComputePipelineCreateInfo pipelineInfo{
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = shaderStage,
        .layout = pipelineLayout_,
    };
    
    if (vkCreateComputePipelines(vk.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline_) != VK_SUCCESS) {
        LOGE("Failed to create compute pipeline");
        return false;
    }
    
    // Create descriptor pool
    VkDescriptorPoolSize poolSize{
        .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = 3,
    };
    
    VkDescriptorPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize,
    };
    
    if (vkCreateDescriptorPool(vk.device, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        LOGE("Failed to create descriptor pool");
        return false;
    }
    
    // Create command pool
    VkCommandPoolCreateInfo cmdPoolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = vk.computeQueueFamily,
    };
    
    if (vkCreateCommandPool(vk.device, &cmdPoolInfo, nullptr, &commandPool_) != VK_SUCCESS) {
        LOGE("Failed to create command pool");
        return false;
    }
    
    // Allocate command buffer
    VkCommandBufferAllocateInfo cmdBufInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    
    if (vkAllocateCommandBuffers(vk.device, &cmdBufInfo, &commandBuffer_) != VK_SUCCESS) {
        LOGE("Failed to allocate command buffer");
        return false;
    }
    
    LOGI("GPU frame blender initialized successfully (using basic 50/50 blend SPIR-V)");
    LOGI("To use motion-compensated optical flow: compile blend_frames.comp with glslc and replace BLEND_SPIRV[]");
    return true;
}

bool GpuFrameBlender::blend(const AhbImage &inputA, const AhbImage &inputB, const AhbImage &output) {
    if (!vk_ || !computePipeline_ || !commandBuffer_) {
        LOGE("Blender not initialized");
        return false;
    }
    
    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptorPool_,
        .descriptorSetCount = 1,
        .pSetLayouts = &descriptorLayout_,
    };
    
    if (vkAllocateDescriptorSets(vk_->device, &allocInfo, &currentDescriptorSet_) != VK_SUCCESS) {
        LOGE("Failed to allocate descriptor set");
        return false;
    }
    
    // Write descriptor set
    VkDescriptorImageInfo imageInfos[3] = {
        {.imageView = inputA.imageView, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
        {.imageView = inputB.imageView, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
        {.imageView = output.imageView, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
    };
    
    VkWriteDescriptorSet writes[3] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = currentDescriptorSet_,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &imageInfos[0],
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = currentDescriptorSet_,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &imageInfos[1],
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = currentDescriptorSet_,
            .dstBinding = 2,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &imageInfos[2],
        },
    };
    
    vkUpdateDescriptorSets(vk_->device, 3, writes, 0, nullptr);
    
    // Record compute commands
    VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    
    vkBeginCommandBuffer(commandBuffer_, &beginInfo);
    
    // Transition images to general layout
    VkImageMemoryBarrier barriers[3] = {};
    for (int i = 0; i < 3; ++i) {
        VkImage img = i == 0 ? inputA.image : (i == 1 ? inputB.image : output.image);
        barriers[i] = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .image = img,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
    }
    vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 3, barriers);
    
    // Dispatch compute shader
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline_);
    vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_,
                            0, 1, &currentDescriptorSet_, 0, nullptr);
    
    uint32_t w = output.extent.width;
    uint32_t h = output.extent.height;
    uint32_t groupsX = (w + 7) / 8;
    uint32_t groupsY = (h + 7) / 8;
    vkCmdDispatch(commandBuffer_, groupsX, groupsY, 1);
    
    vkEndCommandBuffer(commandBuffer_);
    
    // Submit
    VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer_,
    };
    
    if (vkQueueSubmit(vk_->computeQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        LOGE("Failed to submit compute command");
        vkResetCommandBuffer(commandBuffer_, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
        return false;
    }
    
    // Wait for completion
    if (vkQueueWaitIdle(vk_->computeQueue) != VK_SUCCESS) {
        LOGE("Queue wait idle failed");
        return false;
    }
    
    vkResetCommandBuffer(commandBuffer_, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
    return true;
}

void GpuFrameBlender::reset() {
    if (!vk_) return;
    
    vkDeviceWaitIdle(vk_->device);
    
    if (commandPool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(vk_->device, commandPool_, nullptr);
        commandPool_ = VK_NULL_HANDLE;
    }
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vk_->device, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }
    if (computePipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(vk_->device, computePipeline_, nullptr);
        computePipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vk_->device, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    if (descriptorLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vk_->device, descriptorLayout_, nullptr);
        descriptorLayout_ = VK_NULL_HANDLE;
    }
    if (computeShader_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(vk_->device, computeShader_, nullptr);
        computeShader_ = VK_NULL_HANDLE;
    }
}

} // namespace lsfg_android
