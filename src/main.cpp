
#include "base.h"
#include "camera.h"
#include "check.h"
#include "splat.h"
#include "splat_pipeline.h"
#include <GLFW/glfw3.h>
#include <cstdlib>
#include <cstring>

static void record_depth_pass(VkCommandBuffer cmd, const SplatPipeline& pipe, VkDescriptorSet set,
    uint32_t count, uint32_t n_pad, const PhysicsPush& phys,
    bool build_voxels, VkBuffer voxel_density, uint32_t voxel_total, VkBuffer voxel_indirect)
{
    base_memory_barrier(cmd,
        VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe.layout, 0, 1, &set, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe.physics_pipeline);
    vkCmdPushConstants(cmd, pipe.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(phys), &phys);
    vkCmdDispatch(cmd, (count + 255) / 256, 1, 1);

    base_memory_barrier(cmd,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

    if (build_voxels) {
        vkCmdFillBuffer(cmd, voxel_density, 0, VK_WHOLE_SIZE, 0);
        VkDrawIndirectCommand reset = { 36, 0, 0, 0 };
        vkCmdUpdateBuffer(cmd, voxel_indirect, 0, sizeof(reset), &reset);

        base_memory_barrier(cmd,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

        SortPush vp = { count, 0, 0, 0 };
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe.voxelize_pipeline);
        vkCmdPushConstants(cmd, pipe.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(vp), &vp);
        vkCmdDispatch(cmd, (count + 255) / 256, 1, 1);

        base_memory_barrier(cmd,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe.compact_pipeline);
        vkCmdDispatch(cmd, (voxel_total + 255) / 256, 1, 1);

        base_memory_barrier(cmd,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
            VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe.compute_pipeline);
    SortPush pc = { count, n_pad, 0, 0 };
    vkCmdPushConstants(cmd, pipe.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
    vkCmdDispatch(cmd, (n_pad + 255) / 256, 1, 1);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe.sort_pipeline);
    for (uint32_t k = 2; k <= n_pad; k <<= 1) {
        for (uint32_t j = k >> 1; j > 0; j >>= 1) {
            base_memory_barrier(cmd,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

            pc.j = j;
            pc.k = k;
            vkCmdPushConstants(cmd, pipe.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (n_pad + 255) / 256, 1, 1);
        }
    }

    base_memory_barrier(cmd,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
}

int main(int argc, char** argv)
{
    std::printf("[main] build check: rotate %.1f\n", 23.4f);
    std::printf("[main] build check v2\n");
    BaseConfig cfg;
    uint64_t   max_frames = 0;
    glm::vec3 rotate_deg(23.4f, 0.0f, 0.0f);
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--bench") == 0)                 cfg.present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        if (std::strcmp(argv[i], "--no-validation") == 0)         cfg.validation = false;
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) max_frames = std::strtoull(argv[++i], nullptr, 10);
        if (std::strcmp(argv[i], "--rot") == 0 && i + 3 < argc) {
            rotate_deg.x = std::strtof(argv[++i], nullptr);
            rotate_deg.y = std::strtof(argv[++i], nullptr);
            rotate_deg.z = std::strtof(argv[++i], nullptr);
        }
    }

    Base base;
    if (!base_init(base, cfg))
        return 1;

    Camera camera;
    camera.speed = 5.0f;

    GaussianSplat splat;
    if (!splat_load_file(base, ASSET_DIR "/playroom.glb", splat, rotate_deg))
        return 1;

    uint32_t n_pad = 1;
    while (n_pad < splat.splat_count)
        n_pad <<= 1;

    BaseBuffer sort_keys = base_create_buffer(base, n_pad * sizeof(float),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
    BaseBuffer sort_indices = base_create_buffer(base, n_pad * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);

    BaseBuffer prev_positions = base_create_buffer(base, splat.splat_count * 3 * sizeof(float),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
    BaseBuffer awake = base_create_buffer(base, splat.splat_count * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);

    std::vector<uint32_t> zeros(splat.splat_count, 0);
    base_upload(base, awake, zeros.data(), zeros.size() * sizeof(uint32_t), 0);
    SplatPipeline pipe;
    if (!create_splat_pipeline(base, pipe))
        return 1;



    BaseBuffer ubo[BASE_FRAMES_IN_FLIGHT];
    for (uint32_t i = 0; i < BASE_FRAMES_IN_FLIGHT; i++)
        ubo[i] = base_create_buffer(base, sizeof(SplatUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);

    VkDescriptorPoolSize pool_sizes[2] = {
     { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BASE_FRAMES_IN_FLIGHT },
     { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BASE_FRAMES_IN_FLIGHT * 13 },
    };
    VkDescriptorPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    pool_info.maxSets = BASE_FRAMES_IN_FLIGHT;
    pool_info.poolSizeCount = 2;
    pool_info.pPoolSizes = pool_sizes;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorPool(base.device, &pool_info, nullptr, &descriptor_pool));

    VkDescriptorSetLayout layouts[BASE_FRAMES_IN_FLIGHT];
    for (uint32_t i = 0; i < BASE_FRAMES_IN_FLIGHT; i++)
        layouts[i] = pipe.set_layout;

    VkDescriptorSetAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    alloc_info.descriptorPool = descriptor_pool;
    alloc_info.descriptorSetCount = BASE_FRAMES_IN_FLIGHT;
    alloc_info.pSetLayouts = layouts;
    VkDescriptorSet sets[BASE_FRAMES_IN_FLIGHT];
    VK_CHECK(vkAllocateDescriptorSets(base.device, &alloc_info, sets));

    glm::vec3 extent = splat.bounds_max - splat.bounds_min;
    float     vsize = glm::max(extent.x, glm::max(extent.y, extent.z)) / 128.0f;

    VoxelGrid grid;
    grid.origin_size = glm::vec4(splat.bounds_min - glm::vec3(vsize), vsize);
    grid.dims = glm::uvec4(glm::uvec3(glm::ceil(extent / vsize)) + glm::uvec3(2), 500u);
    uint32_t voxel_total = grid.dims.x * grid.dims.y * grid.dims.z;
    std::printf("[voxel] %u x %u x %u = %u cells, size %.4f\n",
        grid.dims.x, grid.dims.y, grid.dims.z, voxel_total, vsize);

    BaseBuffer voxel_density = base_create_buffer(base, voxel_total * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
    BaseBuffer voxel_solid = base_create_buffer(base, voxel_total * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
    BaseBuffer voxel_indirect = base_create_buffer(base, sizeof(VkDrawIndirectCommand),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, false);
    BaseBuffer voxel_grid = base_create_buffer(base, sizeof(VoxelGrid),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
    base_upload(base, voxel_grid, &grid, sizeof(grid), 0);

    VkBuffer storage[13] = {
    splat.position_buffer.buffer,
    splat.rotation_buffer.buffer,
    splat.scale_buffer.buffer,
    splat.opacity_buffer.buffer,
    splat.color_buffer.buffer,
    sort_keys.buffer,
    sort_indices.buffer,
    prev_positions.buffer,
    awake.buffer,
    voxel_density.buffer,
    voxel_solid.buffer,
    voxel_indirect.buffer,
    voxel_grid.buffer,

    };


    for (uint32_t i = 0; i < BASE_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo infos[14] = {};
        infos[0] = { ubo[i].buffer, 0, sizeof(SplatUBO) };
        for (uint32_t b = 0; b < 13; b++)
            infos[b + 1] = { storage[b], 0, VK_WHOLE_SIZE };

        VkWriteDescriptorSet writes[14] = {};
        for (uint32_t b = 0; b < 14; b++) {
            writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[b].dstSet = sets[i];
            writes[b].dstBinding = b;
            writes[b].descriptorCount = 1;
            writes[b].descriptorType = (b == 0) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[b].pBufferInfo = &infos[b];
        }
        vkUpdateDescriptorSets(base.device, 14, writes, 0, nullptr);
    }

    while (base_poll(base)) {
        camera_update(camera, base.window, base.dt);

        static glm::vec3 tune_deg(0.0f);
        float step = 20.0f * base.dt;
        if (glfwGetKey(base.window, GLFW_KEY_I) == GLFW_PRESS) tune_deg.x += step;
        if (glfwGetKey(base.window, GLFW_KEY_K) == GLFW_PRESS) tune_deg.x -= step;
        if (glfwGetKey(base.window, GLFW_KEY_J) == GLFW_PRESS) tune_deg.z += step;
        if (glfwGetKey(base.window, GLFW_KEY_L) == GLFW_PRESS) tune_deg.z -= step;
        if (glfwGetKey(base.window, GLFW_KEY_P) == GLFW_PRESS)
            std::printf("--rot %.1f 0 %.1f\n", tune_deg.x, tune_deg.z);

        glm::mat4 model = glm::mat4_cast(glm::quat(glm::radians(tune_deg)));

        BaseFrame frame;
        if (!base_begin_frame(base, frame))
            continue;

        SplatUBO u;
        u.projection = camera_proj(camera, frame.extent);
        u.view = camera_view(camera);
        u.viewport = { (float)frame.extent.width, (float)frame.extent.height };
        glm::vec2 focal = camera_focal(camera, frame.extent);
        u.focal_x = focal.x;
        u.focal_y = focal.y;
        std::memcpy(ubo[frame.frame_index].mapped, &u, sizeof(u));

        static bool b_was_down = false;
        bool b_down = glfwGetKey(base.window, GLFW_KEY_B) == GLFW_PRESS;
        bool blast = b_down && !b_was_down;
        b_was_down = b_down;

        glm::vec3 center = camera.position + camera_forward(camera) * 2.0f;

        PhysicsPush phys = {};
        phys.count = splat.splat_count;
        phys.dt = glm::min(base.dt, 1.0f / 30.0f);
        phys.floor_y = splat.floor_y;
        phys.damping = 0.999f;
        phys.blast = glm::vec4(center, 0.8f);
        phys.strength = 6.0f;
        phys.do_blast = blast ? 1u : 0u;
        phys.gravity = 9.81f;

        static bool voxels_built = false;
        bool build = !voxels_built || blast;
        voxels_built = true;

        record_depth_pass(frame.cmd, pipe, sets[frame.frame_index], splat.splat_count, n_pad, phys,
            build, voxel_density.buffer, voxel_total, voxel_indirect.buffer);

        base_begin_rendering(base, frame, 0.02f, 0.02f, 0.025f);
        draw_gaussian_splats(frame.cmd, pipe, sets[frame.frame_index], splat);

        static bool show_voxels = true, v_was_down = false;
        bool v_down = glfwGetKey(base.window, GLFW_KEY_V) == GLFW_PRESS;
        if (v_down && !v_was_down) show_voxels = !show_voxels;
        v_was_down = v_down;

        if (show_voxels) {
            vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe.voxel_debug_pipeline);
            vkCmdBindDescriptorSets(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe.layout, 0, 1,
                &sets[frame.frame_index], 0, nullptr);
            vkCmdDrawIndirect(frame.cmd, voxel_indirect.buffer, 0, 1, sizeof(VkDrawIndirectCommand));
        }

        static bool show_splats = true, h_was_down = false;
        bool h_down = glfwGetKey(base.window, GLFW_KEY_H) == GLFW_PRESS;
        if (h_down && !h_was_down) show_splats = !show_splats;
        h_was_down = h_down;



        base_end_rendering(frame);

        base_end_frame(base, frame);

        if (max_frames && base.frame_number >= max_frames)
            break;
    }

    vkDeviceWaitIdle(base.device);

    vkDestroyDescriptorPool(base.device, descriptor_pool, nullptr);
    for (uint32_t i = 0; i < BASE_FRAMES_IN_FLIGHT; i++)
        base_destroy_buffer(base, ubo[i]);
    base_destroy_buffer(base, sort_indices);
    base_destroy_buffer(base, sort_keys);
	base_destroy_buffer(base, prev_positions);
	base_destroy_buffer(base, awake);
    destroy_splat_pipeline(base, pipe);
    base_destroy_buffer(base, voxel_grid);
    base_destroy_buffer(base, voxel_indirect);
    base_destroy_buffer(base, voxel_solid);
    base_destroy_buffer(base, voxel_density);
    splat_destroy(base, splat);

    base_shutdown(base);
    return 0;
}