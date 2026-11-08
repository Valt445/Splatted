// splat_pipeline.h
#pragma once
#include "base.h"

struct SplatPipeline {
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;   // binding 0 = SplatUBO
    VkPipelineLayout      layout     = VK_NULL_HANDLE;
    VkPipeline            pipeline   = VK_NULL_HANDLE;
	VkPipeline            compute_pipeline = VK_NULL_HANDLE;
    VkPipeline sort_pipeline = VK_NULL_HANDLE;
	VkPipeline physics_pipeline = VK_NULL_HANDLE;
	VkPipeline voxelize_pipeline = VK_NULL_HANDLE;
	VkPipeline compact_pipeline = VK_NULL_HANDLE;   
	VkPipeline voxel_debug_pipeline = VK_NULL_HANDLE;
};

struct PhysicsPush {            // must match physics.comp byte-for-byte (48 bytes)
    uint32_t  count;
    float     dt;
    float     floor_y;
    float     damping;
    glm::vec4 blast;            // xyz = blast center, w = radius
    float     strength;
    uint32_t  do_blast;         // 1 on the frame you press the key
    float     gravity;
    float     pad;
};

struct VoxelGrid {
    glm::vec4  origin_size;   // xyz = grid corner, w = cell size
    glm::uvec4 dims;          // xyz = cells per axis, w = solid threshold
};

struct SortPush { uint32_t count; uint32_t n_pad; uint32_t j; uint32_t k; };


struct SplatUBO {
    glm::mat4 projection;   // camera_proj(...)
    glm::mat4 view;         // camera_view(...)
    glm::vec2 viewport;     // window width, height in pixels
    float     focal_x;      // camera_focal(...).x
    float     focal_y;      // camera_focal(...).y
};


bool create_splat_pipeline(const Base& base, SplatPipeline& out);
void destroy_splat_pipeline(const Base& base, SplatPipeline& p);
