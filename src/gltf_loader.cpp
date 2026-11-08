
#include <base.h>
#include <cgltf.h>
#include <cstring>
#include <splat.h>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <vector>
#include <cstdio>
#define KHR_GAUSSIAN_SPLATTING_EXTENSION "KHR_gaussian_splatting"


static const cgltf_accessor* find_attribute(const cgltf_primitive& prim, const char* name)
{
    for (cgltf_size i = 0; i < prim.attributes_count; i++)
        if (std::strcmp(prim.attributes[i].name, name) == 0)   // no ';' here
            return prim.attributes[i].data;                    // .data, not .accessor
    return nullptr;
}


static float level_scene(std::vector<float>& positions, std::vector<float>& rotations, uint32_t count,
    glm::vec3 rotate_deg, glm::vec3& out_min, glm::vec3& out_max)
{
    glm::quat q = glm::quat(glm::radians(rotate_deg));

    for (uint32_t i = 0; i < count; i++) {
        glm::vec3 p(positions[3 * i], positions[3 * i + 1], positions[3 * i + 2]);
        p = q * p;
        positions[3 * i] = p.x;  positions[3 * i + 1] = p.y;  positions[3 * i + 2] = p.z;

        // file: x, y, z, w   glm::quat constructor: (w, x, y, z)
        glm::quat r(rotations[4 * i + 3], rotations[4 * i], rotations[4 * i + 1], rotations[4 * i + 2]);
        r = q * r;
        rotations[4 * i] = r.x;  rotations[4 * i + 1] = r.y;  rotations[4 * i + 2] = r.z;  rotations[4 * i + 3] = r.w;
    }

    // Robust bounds: 0.5% / 99.5% percentile per axis, so floaters don't count
    std::vector<float> axis(count);
    for (int a = 0; a < 3; a++) {
        for (uint32_t i = 0; i < count; i++) axis[i] = positions[3 * i + a];
        size_t k_lo = count / 200, k_hi = count - 1 - count / 200;
        std::nth_element(axis.begin(), axis.begin() + k_lo, axis.end());  out_min[a] = axis[k_lo];
        std::nth_element(axis.begin(), axis.begin() + k_hi, axis.end());  out_max[a] = axis[k_hi];
    }

    std::printf("[splat] rotated (%.1f %.1f %.1f) deg, bounds (%.2f %.2f %.2f)..(%.2f %.2f %.2f)\n",
        rotate_deg.x, rotate_deg.y, rotate_deg.z,
        out_min.x, out_min.y, out_min.z, out_max.x, out_max.y, out_max.z);
    return out_min.y;                       // floor = bottom of the robust bounds
}

bool has_gaussian_splats(const cgltf_data* data)
{
    // Is the extension declared at the top of the file?
    bool declared = false;
    for (cgltf_size i = 0; i < data->extensions_used_count; i++) {
        if (std::strcmp(data->extensions_used[i], "KHR_gaussian_splatting") == 0) {
            declared = true;
            break;
        }
    }
    if (!declared)
        return false;

    // Does any mesh primitive actually use it?
    for (cgltf_size m = 0; m < data->meshes_count; m++) {
        const cgltf_mesh& mesh = data->meshes[m];
        for (cgltf_size p = 0; p < mesh.primitives_count; p++) {
            const cgltf_primitive& prim = mesh.primitives[p];
            for (cgltf_size e = 0; e < prim.extensions_count; e++) {
                if (std::strcmp(prim.extensions[e].name, "KHR_gaussian_splatting") == 0)
                    return true;
            }
        }
    }
    return false;
}


// Returns an empty list if the accessor is missing.
std::vector<float> read_accessor_floats(const cgltf_accessor* accessor)
{
    if (!accessor)
        return {};

    cgltf_size components = cgltf_num_components(accessor->type);   // SCALAR 1, VEC3 3, VEC4 4
    std::vector<float> data(accessor->count * components);

    cgltf_size written = cgltf_accessor_unpack_floats(accessor, data.data(), data.size());
    if (written != data.size())
        return {};   // couldn't read it (e.g. no buffer data loaded)

    return data;
}

static bool has_splat_extension(const cgltf_primitive& prim)
{
    for (cgltf_size i = 0; i < prim.extensions_count; i++)
        if (std::strcmp(prim.extensions[i].name, "KHR_gaussian_splatting") == 0)
            return true;
    return false;
}

static BaseBuffer upload_vertex_data(Base& base, const std::vector<float>& data)
{
    VkDeviceSize size = data.size() * sizeof(float);
    BaseBuffer   buffer = base_create_buffer(base, size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
    base_upload(base, buffer, data.data(), size);
    return buffer;
}

bool splat_create_from_gltf(Base& base, const cgltf_primitive& prim, GaussianSplat& splat,
    glm::vec3 rotate_deg)
{
    if (!has_splat_extension(prim)) {
        std::fprintf(stderr, "[splat] primitive has no KHR_gaussian_splatting\n");
        return false;
    }

    // ---- Find the attributes ----
    const cgltf_accessor* pos_acc = find_attribute(prim, "POSITION");
    const cgltf_accessor* rot_acc = find_attribute(prim, "KHR_gaussian_splatting:ROTATION");
    const cgltf_accessor* scale_acc = find_attribute(prim, "KHR_gaussian_splatting:SCALE");
    const cgltf_accessor* opac_acc = find_attribute(prim, "KHR_gaussian_splatting:OPACITY");
    const cgltf_accessor* sh0_acc = find_attribute(prim, "KHR_gaussian_splatting:SH_DEGREE_0_COEF_0");
    const cgltf_accessor* col_acc = find_attribute(prim, "COLOR_0");   // fallback

    if (!pos_acc || !rot_acc || !scale_acc || !opac_acc || (!sh0_acc && !col_acc)) {
        std::fprintf(stderr, "[splat] missing a required attribute\n");
        return false;
    }

    // ---- Read everything as floats ----
    std::vector<float> positions = read_accessor_floats(pos_acc);     // 3 per splat
    std::vector<float> rotations = read_accessor_floats(rot_acc);     // 4 per splat
    std::vector<float> scales = read_accessor_floats(scale_acc);   // 3 per splat
    std::vector<float> opacities = read_accessor_floats(opac_acc);    // 1 per splat


    size_t count = pos_acc->count;
    if (positions.size() != count * 3 || rotations.size() != count * 4 ||
        scales.size() != count * 3 || opacities.size() != count) {
        std::fprintf(stderr, "[splat] attribute sizes don't match (%zu splats)\n", count);
        return false;
    }

    // ---- Level the scene (rotate positions + orientations, find bounds/floor) ----
    splat.floor_y = level_scene(positions, rotations, (uint32_t)count, rotate_deg,
        splat.bounds_min, splat.bounds_max);
    
    // ---- Color: always end up with 3 floats per splat, 0..1 ----
    std::vector<float> colors(count * 3);
    if (sh0_acc) {
        // SH degree-0 coefficient -> base color
        std::vector<float> sh0 = read_accessor_floats(sh0_acc);   // 3 per splat
        if (sh0.size() != count * 3) {
            std::fprintf(stderr, "[splat] SH_DEGREE_0_COEF_0 size mismatch\n");
            return false;
        }
        const float SH_C0 = 0.28209479f;
        for (size_t i = 0; i < count * 3; i++)
            colors[i] = 0.5f + SH_C0 * sh0[i];
    }
    else {
        // COLOR_0 can be VEC3 or VEC4: keep only r, g, b
        std::vector<float> raw = read_accessor_floats(col_acc);
        size_t comps = cgltf_num_components(col_acc->type);
        if (raw.size() != count * comps || comps < 3) {
            std::fprintf(stderr, "[splat] COLOR_0 size mismatch\n");
            return false;
        }
        for (size_t i = 0; i < count; i++) {
            colors[i * 3 + 0] = raw[i * comps + 0];
            colors[i * 3 + 1] = raw[i * comps + 1];
            colors[i * 3 + 2] = raw[i * comps + 2];
        }
    }

    // ---- Upload to the GPU ----
    splat.splat_count = (uint32_t)count;
    splat.position_buffer = upload_vertex_data(base, positions);
    splat.rotation_buffer = upload_vertex_data(base, rotations);
    splat.scale_buffer = upload_vertex_data(base, scales);
    splat.opacity_buffer = upload_vertex_data(base, opacities);
    splat.color_buffer = upload_vertex_data(base, colors);

    // ---- Extension properties ----
    // cgltf keeps these as raw JSON in prim.extensions[i].data. For M1 we keep the
    // defaults (ellipse kernel, SH degree 0, sRGB, not antialiased).

    std::printf("[splat] loaded %u splats\n", splat.splat_count);
    return true;
}

void splat_destroy(const Base& base, GaussianSplat& splat)
{
    base_destroy_buffer(base, splat.position_buffer);
    base_destroy_buffer(base, splat.rotation_buffer);
    base_destroy_buffer(base, splat.scale_buffer);
    base_destroy_buffer(base, splat.opacity_buffer);
    base_destroy_buffer(base, splat.color_buffer);

    splat = GaussianSplat{};
}
bool splat_load_file(Base& base, const char* path, GaussianSplat& out, glm::vec3 rotate_deg)
{
    cgltf_options options = {};
    cgltf_data* data = nullptr;

    // 1. Read the JSON part (works for both .glb and .gltf)
    if (cgltf_parse_file(&options, path, &data) != cgltf_result_success) {
        std::fprintf(stderr, "[splat] can't parse %s\n", path);
        return false;
    }

    // 2. Load the binary data (the actual splat numbers)
    if (cgltf_load_buffers(&options, data, path) != cgltf_result_success) {
        std::fprintf(stderr, "[splat] can't load buffers for %s\n", path);
        cgltf_free(data);
        return false;
    }

    // 3. Optional: catches broken files early
    if (cgltf_validate(data) != cgltf_result_success) {
        std::fprintf(stderr, "[splat] %s failed validation\n", path);
        cgltf_free(data);
        return false;
    }

    // 4. Is this a splat file at all?
    if (!has_gaussian_splats(data)) {
        std::fprintf(stderr, "[splat] %s has no KHR_gaussian_splatting data\n", path);
        cgltf_free(data);
        return false;
    }

    // 5. Find the first primitive that holds splats and upload it
    bool loaded = false;
    for (cgltf_size m = 0; m < data->meshes_count && !loaded; m++) {
        const cgltf_mesh& mesh = data->meshes[m];
        for (cgltf_size p = 0; p < mesh.primitives_count && !loaded; p++) {
            const cgltf_primitive& prim = mesh.primitives[p];
            if (has_splat_extension(prim))
                loaded = splat_create_from_gltf(base, prim, out, rotate_deg);
        }
    }

    // 6. The data is on the GPU now, so the CPU copy can go
    cgltf_free(data);

    if (!loaded)
        std::fprintf(stderr, "[splat] no loadable splat primitive in %s\n", path);
    return loaded;
}

void draw_gaussian_splats(VkCommandBuffer cmd, const SplatPipeline& pipe,
    VkDescriptorSet set, const GaussianSplat& splat)
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe.layout, 0, 1, &set, 0, nullptr);
    vkCmdDraw(cmd, 4, splat.splat_count, 0, 0);
}