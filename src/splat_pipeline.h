#pragma once
#include "base.h"

struct SplatPipeline {
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout      layout     = VK_NULL_HANDLE;
    VkPipeline            pipeline   = VK_NULL_HANDLE;
	VkPipeline            compute_pipeline = VK_NULL_HANDLE;
    VkPipeline sort_pipeline = VK_NULL_HANDLE;
	VkPipeline physics_pipeline = VK_NULL_HANDLE;
	VkPipeline voxelize_pipeline = VK_NULL_HANDLE;
	VkPipeline compact_pipeline = VK_NULL_HANDLE;
	VkPipeline voxel_debug_pipeline = VK_NULL_HANDLE;
};

struct PhysicsPush {
    uint32_t  count;
    float     dt;
    float     floor_y;
    float     damping;
    glm::vec4 blast;
    float     strength;
    uint32_t  do_blast;
    float     gravity;
    float     pad;
};

struct VoxelGrid {
    glm::vec4  origin_size;
    glm::uvec4 dims;
};

struct SortPush { uint32_t count; uint32_t n_pad; uint32_t j; uint32_t k; };


struct SplatUBO {
    glm::mat4 projection;
    glm::mat4 view;
    glm::vec2 viewport;
    float     focal_x;
    float     focal_y;
};


bool create_splat_pipeline(const Base& base, SplatPipeline& out);
void destroy_splat_pipeline(const Base& base, SplatPipeline& p);
