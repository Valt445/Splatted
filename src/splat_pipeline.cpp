#include "splat_pipeline.h"
#include "check.h"
#include <glm/glm.hpp>
#include <cstdio>

bool create_splat_pipeline(const Base& base, SplatPipeline& out)
{
    VkShaderModule vert = base_load_shader(base, "splat.vert");
    VkShaderModule frag = base_load_shader(base, "splat.frag");
    VkShaderModule compute = base_load_shader(base, "splat.comp");
    VkShaderModule bitonic = base_load_shader(base, "bitonic.comp");
    VkShaderModule physics = base_load_shader(base, "physics.comp");
	VkShaderModule voxelize = base_load_shader(base, "voxelize.comp");
	VkShaderModule compact = base_load_shader(base, "voxel_compact.comp");
    VkShaderModule dvert = base_load_shader(base, "voxel_debug.vert");
    VkShaderModule dfrag = base_load_shader(base, "voxel_debug.frag");

    if (!vert || !frag || !compute || !bitonic || !physics ||
        !voxelize || !compact || !dvert || !dfrag) {
        if (vert) vkDestroyShaderModule(base.device, vert, nullptr);
        if (frag) vkDestroyShaderModule(base.device, frag, nullptr);
        if (compute) vkDestroyShaderModule(base.device, compute, nullptr);
        if (bitonic) vkDestroyShaderModule(base.device, bitonic, nullptr);
        if (physics) vkDestroyShaderModule(base.device, physics, nullptr);
		if (voxelize) vkDestroyShaderModule(base.device, voxelize, nullptr);
		if (compact) vkDestroyShaderModule(base.device, compact, nullptr);
		if (dvert) vkDestroyShaderModule(base.device, dvert, nullptr);
		if (dfrag) vkDestroyShaderModule(base.device, dfrag, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[5] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";
    stages[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[2].stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stages[2].module = compute;
    stages[2].pName = "main";

    VkVertexInputBindingDescription bindings[5] = {
        { 0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_INSTANCE },
        { 1, sizeof(glm::vec4), VK_VERTEX_INPUT_RATE_INSTANCE },
        { 2, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_INSTANCE },
        { 3, sizeof(float),     VK_VERTEX_INPUT_RATE_INSTANCE },
        { 4, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_INSTANCE },
    };
    VkVertexInputAttributeDescription attributes[5] = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT,    0 },
        { 1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0 },
        { 2, 2, VK_FORMAT_R32G32B32_SFLOAT,    0 },
        { 3, 3, VK_FORMAT_R32_SFLOAT,          0 },
        { 4, 4, VK_FORMAT_R32G32B32_SFLOAT,    0 },
    };

    VkPipelineVertexInputStateCreateInfo vertex_input = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

    VkPipelineInputAssemblyStateCreateInfo input_assembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

    VkPipelineViewportStateCreateInfo viewport = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkDynamicState dynamic_states[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;

    VkPipelineRasterizationStateCreateInfo raster = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_FALSE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState blend_attachment = {};
    blend_attachment.blendEnable = VK_TRUE;
    blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_attachment;

    VkDescriptorSetLayoutBinding set_bindings[14] = {};
    for (uint32_t b = 0; b < 14; b++) {
        set_bindings[b].binding = b;
        set_bindings[b].descriptorType = (b == 0) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
            : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        set_bindings[b].descriptorCount = 1;
        set_bindings[b].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo set_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    set_info.bindingCount = 14;
    set_info.pBindings = set_bindings;
    VK_CHECK(vkCreateDescriptorSetLayout(base.device, &set_info, nullptr, &out.set_layout));

    VkPushConstantRange push_range = {};
    push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push_range.offset = 0;
    push_range.size = sizeof(PhysicsPush);

    VkPipelineLayoutCreateInfo layout_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &out.set_layout;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_range;
    VK_CHECK(vkCreatePipelineLayout(base.device, &layout_info, nullptr, &out.layout));

    VkPipelineRenderingCreateInfo rendering = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &base.swapchain_format;
    rendering.depthAttachmentFormat = base.depth_format;

    VkGraphicsPipelineCreateInfo info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &input_assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = out.layout;
    VK_CHECK(vkCreateGraphicsPipelines(base.device, VK_NULL_HANDLE, 1, &info, nullptr, &out.pipeline));

    stages[0].module = dvert;
    stages[1].module = dfrag;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    blend_attachment.blendEnable = VK_FALSE;
    VK_CHECK(vkCreateGraphicsPipelines(base.device, VK_NULL_HANDLE, 1, &info, nullptr,
        &out.voxel_debug_pipeline));

    VkComputePipelineCreateInfo compute_info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    compute_info.stage = stages[2];
    compute_info.layout = out.layout;
    VK_CHECK(vkCreateComputePipelines(base.device, VK_NULL_HANDLE, 1, &compute_info, nullptr, &out.compute_pipeline));

    VkComputePipelineCreateInfo sort_info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    sort_info.stage = stages[2];
    sort_info.stage.module = bitonic;
    sort_info.layout = out.layout;
    VK_CHECK(vkCreateComputePipelines(base.device, VK_NULL_HANDLE, 1, &sort_info, nullptr, &out.sort_pipeline));

    VkComputePipelineCreateInfo vox_info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    vox_info.stage = stages[2];
    vox_info.stage.module = voxelize;
    vox_info.layout = out.layout;
    VK_CHECK(vkCreateComputePipelines(base.device, VK_NULL_HANDLE, 1, &vox_info, nullptr,
        &out.voxelize_pipeline));
    vkDestroyShaderModule(base.device, voxelize, nullptr);

    VkComputePipelineCreateInfo compact_info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    compact_info.stage = stages[2];
    compact_info.stage.module = compact;
    compact_info.layout = out.layout;
    VK_CHECK(vkCreateComputePipelines(base.device, VK_NULL_HANDLE, 1, &compact_info, nullptr,
        &out.compact_pipeline));
    vkDestroyShaderModule(base.device, compact, nullptr);

    VkComputePipelineCreateInfo physics_info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    physics_info.stage = stages[2];
    physics_info.stage.module = physics;
    physics_info.layout = out.layout;
    VK_CHECK(vkCreateComputePipelines(base.device, VK_NULL_HANDLE, 1, &physics_info, nullptr, &out.physics_pipeline));

    vkDestroyShaderModule(base.device, vert, nullptr);
    vkDestroyShaderModule(base.device, frag, nullptr);
    vkDestroyShaderModule(base.device, compute, nullptr);
    vkDestroyShaderModule(base.device, bitonic, nullptr);
	vkDestroyShaderModule(base.device, physics, nullptr);
    return true;
}


void destroy_splat_pipeline(const Base& base, SplatPipeline& p)
{
    if (p.pipeline)   vkDestroyPipeline(base.device, p.pipeline, nullptr);
    if (p.layout)     vkDestroyPipelineLayout(base.device, p.layout, nullptr);
    if (p.set_layout) vkDestroyDescriptorSetLayout(base.device, p.set_layout, nullptr);
    if (p.compute_pipeline) vkDestroyPipeline(base.device, p.compute_pipeline, nullptr);
    if (p.sort_pipeline) vkDestroyPipeline(base.device, p.sort_pipeline, nullptr);
	if (p.physics_pipeline) vkDestroyPipeline(base.device, p.physics_pipeline, nullptr);
	if (p.voxelize_pipeline) vkDestroyPipeline(base.device, p.voxelize_pipeline, nullptr);
}

