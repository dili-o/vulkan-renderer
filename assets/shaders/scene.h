
#ifndef HELIX_GLSL_SCENE_H
#define HELIX_GLSL_SCENE_H

// Scene common code

layout(std140, set = MATERIAL_SET, binding = 0) uniform SceneConstants {
  mat4 view_projection;
  mat4 view_projection_debug;
  mat4 inverse_view_projection;
  mat4 world_to_camera;  // view matrix
  mat4 world_to_camera_debug;
  mat4 previous_view_projection;

  vec4 eye;
  vec4 eye_debug;
  // vec4 directional_light;

  uint point_light_count;
  uint directional_shadow_map_index;
  uint dither_texture_index;
  float z_near;

  float z_far;
  float projection_00;
  float projection_11;
  uint frustum_cull_meshes;  // Bools

  uint frustum_cull_meshlets;    // Bools
  uint occlusion_cull_meshes;    // Bools
  uint occlusion_cull_meshlets;  // Bools
  uint freeze_occlusion_camera;

  vec2 resolution;
  float aspect_ratio;
  float pad0001;

  vec4 frustum_planes[6];
};

struct DirectionalLight {
  vec4 direction_intensity;
  vec4 position_texture_index;

  mat4 view;
  mat4 projection;

  vec4 frustum_planes[6];
};

float dither(vec2 screen_pixel_position, float value) {
  float dither_value =
      texelFetch(global_textures[nonuniformEXT(dither_texture_index)],
                 ivec2(int(screen_pixel_position.x) % 4,
                       int(screen_pixel_position.y) % 4),
                 0)
          .r;
  return value - dither_value;
}

float linearize_depth(float depth) {
  // NOTE(marco): Vulkan depth is [0, 1]
  return z_near * z_far / (z_far + depth * (z_near - z_far));
}

#endif  // HELIX_GLSL_SCENE_H
