#pragma once

#include "Core/Defines.hpp"

namespace Helix {
struct Scene;

bool load_obj_mesh(Scene *scene, cstring path, cstring model);

bool load_gltf_mesh(Scene *scene, cstring path, cstring model);
} // namespace Helix
