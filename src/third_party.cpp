// The one translation unit that compiles third-party implementations.
// Built with warnings off (see CMakeLists.txt) since it's not our code.

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
