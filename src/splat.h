#include <base.h>
#include <splat_pipeline.h>
enum SplatKernel { SPLAT_KERNEL_ELLIPSE, SPLAT_KERNEL_SPHERE };
enum SplatColorSpace { SPLAT_COLOR_SRGB, SPLAT_COLOR_LINEAR };

struct GaussianSplat {
    BaseBuffer position_buffer;   // vec3 per splat
    BaseBuffer rotation_buffer;   // vec4 per splat (x, y, z, w)
    BaseBuffer scale_buffer;      // vec3 per splat (linear)
    BaseBuffer opacity_buffer;    // float per splat (already sigmoid-applied)
    BaseBuffer color_buffer;      // vec3 per splat (final color, 0..1)
  // vec3 per splat (previous position, for velocity)
    uint32_t   splat_count = 0;

    uint32_t        sh_degree = 0;
    bool            antialiased = false;
    SplatKernel     kernel = SPLAT_KERNEL_ELLIPSE;
    SplatColorSpace color_space = SPLAT_COLOR_SRGB;
    float     floor_y = 0.0f;
    glm::vec3 bounds_min = glm::vec3(0.0f), bounds_max = glm::vec3(0.0f);
};




struct SplatAccessors {
    const cgltf_accessor* position = nullptr;
    const cgltf_accessor* rotation = nullptr;
    const cgltf_accessor* scale = nullptr;
    const cgltf_accessor* opacity = nullptr;
    const cgltf_accessor* color = nullptr;
};

bool splat_load_file(Base& base, const char* path, GaussianSplat& out,
    glm::vec3 rotate_deg = glm::vec3(0.0f));
bool splat_create_from_gltf(const Base& base, const cgltf_primitive& prim, GaussianSplat& splat,
    glm::vec3 rotate_deg);
void splat_destroy(const Base& base, GaussianSplat& splat);
bool has_gaussian_splats(const cgltf_data* data);
std::vector<float> read_accessor_floats(const cgltf_accessor* accessor);
void draw_gaussian_splats(VkCommandBuffer cmd, const SplatPipeline& pipe,
    VkDescriptorSet set, const GaussianSplat& splat);