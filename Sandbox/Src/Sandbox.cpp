#include "Sandbox.hpp"
#include "Core/Clock.hpp"
#include "Core/Engine.hpp"
#include "Core/Event.hpp"
#include "Core/Input.hpp"
#include "Core/Job.hpp"
#include "Core/Log.hpp"
#include "Core/Profiler.hpp"
#include "Platform/File.hpp"
#include "Platform/Platform.hpp"
#include "Renderer/GPUResourceTypes.hpp"
#include "Renderer/GPUResources.hpp"
#include "Renderer/ImguiFrontend.hpp"
#include "Renderer/RendererBackend.hpp"
#include "Renderer/RendererFrontEnd.hpp"
#include "Renderer/RendererTypes.hpp"
#include "SceneGraph.hpp"
// Vendors
#include <imgui_internal.h>
#include <stb_image.h>

namespace hlx {

#define SHADOW_MAP_SIZE 4096
#define CASCADE_COUNT 4

Platform *platform = nullptr;
static Clock clock;
static PipelineHandle hello_triangle;
static PipelineHandle shadow_map_debug;
static PipelineHandle shadow_pipeline;
static PipelineHandle frustum_debug;
static RenderPassHandle shadow_pass;
static TextureHandle shadow_map;
static TextureHandle cascade_textures[CASCADE_COUNT];
struct CascadesData {
  glm::mat4 cascade_matrices[CASCADE_COUNT];
  f32 cascade_splits[CASCADE_COUNT]; 
};
static CascadesData cascades_data;
static f32 split_lambda = 0.9f;

static UnifiedBuffer<Vertex> vertex_buffer{};
static UnifiedBuffer<u32> index_buffer{};
static SamplerHandle shadow_map_sampler;
static Scene scene;
static SceneUI scene_ui;
static Array<BufferHandle> uniforms;
static BindingSetLayoutHandle scene_constants_set_layout;
static Array<BindingSetHandle> scene_constants_sets;
static Array<MeshDraw> mesh_draws;

#define MESH_COUNT 2

struct UniformData {
  glm::mat4 view_proj;
  glm::mat4 light_view_proj;
  glm::mat4 light_view_projs[CASCADE_COUNT];
  glm::vec4 light_dir_shadow_map;
  f32 cascade_splits[CASCADE_COUNT];
};

static bool application_on_event(u16 event_code, void *sender, void *listener,
                                 EventContext context);

static bool application_on_key(u16 event_code, void *sender, void *listener,
                               EventContext context);

static bool application_on_window_resize(u16 event_code, void *sender,
                                         void *listener, EventContext context);

static void draw_scene(Scene &scene, Context *ctx, const Camera& camera);
static void draw_scene_shadow(Scene &scene, Context *ctx, u32 cascade_level);

static glm::mat4 get_light_view_proj(
  Camera &camera, const glm::vec3 &light_dir, f32 near, f32 far);

static void update_cascades(
  Camera &camera, const glm::vec3 &light_dir, f32 near, f32 far);

void Sandbox::init() {
  Engine::init();
  ImGui::SetCurrentContext(ImguiFrontend::instance()->get_ImGuiContext());

  platform = Platform::instance();
  if (!platform) {
    HCRITICAL("Failed to create a platform service!");
  }

  EventService *event_service = EventService::instance();
  event_service->register_event(SDL_EVENT_QUIT, 0, application_on_event);
  event_service->register_event(SDL_EVENT_KEY_DOWN, 0, application_on_key);
  event_service->register_event(SDL_EVENT_KEY_UP, 0, application_on_key);
  event_service->register_event(SDL_EVENT_WINDOW_RESIZED, 0,
                                application_on_window_resize);

  RendererFrontEnd *rf = RendererFrontEnd::instance();
  ScopedAllocator scope_allocator(&MemoryService::instance()->stack_allocator);
  StackAllocator *stack_allocator = scope_allocator.allocator;
  
  uniforms.init(&MemoryService::instance()->system_allocator, max_frames_in_flight,
                max_frames_in_flight);
  scene_constants_sets.init(&MemoryService::instance()->system_allocator, max_frames_in_flight,
                max_frames_in_flight);
  {
    BufferCreation creation {
      BufferUsage::Uniform,
      MemoryAccess::CPU_TO_GPU,
      sizeof(UniformData),
      "UniformBuffer",
      true
    };
    for (u32 i = 0; i < max_frames_in_flight; ++i) {
      uniforms[i] = rf->create_buffer(creation);
    }
  }
  {
    BindingSetLayoutCreation creation;
    creation.name = "UniformDescriptorLayout";
    creation.is_bindless = false;
    creation.add_binding(0, 1, ShaderStage::AllStage, BindingType::UniformBuffer);
    scene_constants_set_layout = rf->create_binding_set_layout(creation);

    BindingSetCreation set_creation;
    set_creation.name = "UniformDescriptorSet";
    set_creation.layout = scene_constants_set_layout;

    for (u32 i = 0; i < max_frames_in_flight; ++i) {
      scene_constants_sets[i] = rf->create_binding_set(set_creation);
      BindingSetUpdateInfo info{};
      info.resource_type = ResourceType::Buffer;
      info.resource_handle = uniforms[i];
      info.resource_index = 0;
      info.binding = 0;
      info.buffer_info.offset = 0;
      info.buffer_info.range = sizeof(UniformData);
      rf->update_binding_set(scene_constants_sets[i], &info, 1);
    }
  }
  {
    SamplerCreation creation{};
    creation.border_color = BorderColor::FloatOpaqueWhite;
    creation.name = "ShadowMapSampler";
    creation.address_mode_u = SamplerAddressMode::ClampToBorder;
    creation.address_mode_v = SamplerAddressMode::ClampToBorder;
    creation.address_mode_w = SamplerAddressMode::ClampToBorder;

    shadow_map_sampler = rf->create_sampler(creation);
  }
  {
    TextureCreation creation{};
    creation.name = "ShadowMapTexture";
    creation.width = SHADOW_MAP_SIZE;
    creation.height = SHADOW_MAP_SIZE;
    creation.usage = TextureUsage::Enum(TextureUsage::Sampled | TextureUsage::Depth);
    creation.format = TextureFormat::D32;
    creation.sampler = shadow_map_sampler;
    creation.array_layer_count = 4;
    shadow_map = rf->create_texture(creation);

    creation.base_texture = shadow_map;
    creation.array_layer_count = 1;
    for (u32 i = 0; i < CASCADE_COUNT; ++i) {
      std::string name = "ShadowCascade_" + std::to_string(i);
      creation.name = name.c_str();
      creation.array_base_level = i;
      cascade_textures[i] = rf->create_texture(creation);
    }
  }
  {
    RenderPassCreation creation{};
    creation.add_depth_attachment(LoadOp::Clear, StoreOp::Store, shadow_map);
    shadow_pass = rf->create_render_pass(creation);
    rf->device->set_render_pass_texture(
    shadow_pass, cascade_textures[1]/* shadow_map */, true, 0);
  }
  {
    PipelineCreation creation{};
    creation.name = "ShadowPassPipeline";
    creation.shader_create_infos = (ShaderCreateInfo *)halloca(
        sizeof(ShaderCreateInfo) * 1, stack_allocator);
    creation.shader_create_infos[0] = {"ShadowPass.vert",
                                       ShaderStage::Vertex};
    creation.shader_count = 1;
    creation.pipeline_type = PipelineType::Graphics;
    creation.cull_mode = CullMode::Back;
    creation.set_layout_count = 1;
    creation.set_layouts[0] = scene_constants_set_layout;
    creation.enable_depth_write = true;
    creation.enable_depth_test = true;
    creation.compare_op = CompareOp::Less;
    creation.render_pass = shadow_pass;

    shadow_pipeline = rf->create_pipeline(creation);
  }
  {
    PipelineCreation creation;
    creation.name = "HelloTriangle";
    creation.shader_create_infos = (ShaderCreateInfo *)halloca(
        sizeof(ShaderCreateInfo) * 2, stack_allocator);
    creation.shader_create_infos[0] = {"HelloTriangle.vert",
                                       ShaderStage::Vertex};
    creation.shader_create_infos[1] = {"HelloTriangle.frag",
                                       ShaderStage::Fragment};
    creation.shader_count = 2;
    creation.pipeline_type = PipelineType::Graphics;
    creation.cull_mode = CullMode::Back;
    creation.set_layout_count = 2;
    creation.set_layouts[0] = rf->bindless_set_layout;
    creation.set_layouts[1] = scene_constants_set_layout;
    creation.enable_depth_write = true;
    creation.enable_depth_test = true;
    creation.compare_op = CompareOp::Less;
    creation.render_pass = rf->main_pass;

    hello_triangle = rf->create_pipeline(creation);
  }
  {
    PipelineCreation creation;
    creation.name = "FrustumDebugPipeline";
    creation.shader_create_infos = (ShaderCreateInfo *)halloca(
        sizeof(ShaderCreateInfo) * 2, stack_allocator);
    creation.shader_create_infos[0] = {"Frustum.vert",
                                       ShaderStage::Vertex};
    creation.shader_create_infos[1] = {"Frustum.frag",
                                       ShaderStage::Fragment};
    creation.shader_count = 2;
    creation.pipeline_type = PipelineType::Graphics;
    creation.cull_mode = CullMode::None;
    creation.set_layout_count = 1;
    creation.primitive_type = PrimitiveType::Line;
    creation.set_layouts[0] = scene_constants_set_layout;
    creation.enable_depth_write = false;
    creation.enable_depth_test = false;
    creation.compare_op = CompareOp::Always;
    creation.render_pass = rf->main_pass;

    frustum_debug = rf->create_pipeline(creation);
  }
  {
    PipelineCreation creation;
    creation.name = "ShadowMapDebug";
    creation.shader_create_infos = (ShaderCreateInfo *)halloca(
        sizeof(ShaderCreateInfo) * 2, stack_allocator);
    creation.shader_create_infos[0] = {"ShadowMapDebug.vert",
                                       ShaderStage::Vertex};
    creation.shader_create_infos[1] = {"ShadowMapDebug.frag",
                                       ShaderStage::Fragment};
    creation.shader_count = 2;
    creation.pipeline_type = PipelineType::Graphics;
    creation.cull_mode = CullMode::None;
    creation.set_layout_count = 2;
    creation.set_layouts[0] = rf->bindless_set_layout;
    creation.set_layouts[1] = scene_constants_set_layout;
    creation.enable_depth_write = true;
    creation.enable_depth_test = false;
    creation.compare_op = CompareOp::Always;
    creation.render_pass = rf->main_pass;

    shadow_map_debug = rf->create_pipeline(creation);
  }
  {
    f32 cube_vertices[] = {
		// Position          | Normals         | TexCoord
      // Front face
			-0.5f, -0.5f, 0.5f,  0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
			 0.5f, -0.5f, 0.5f,  0.0f, 0.0f, 1.0f, 1.0f, 0.0f,
			 0.5f,  0.5f, 0.5f,  0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
			-0.5f,  0.5f, 0.5f,  0.0f, 0.0f, 1.0f, 0.0f, 1.0f,

			// Back face
			-0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f,
			 0.5f,  0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f,
			 0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f,
			-0.5f,  0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f,

			// Left face
			-0.5f,  0.5f,  0.5f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
			-0.5f,  0.5f, -0.5f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
			-0.5f, -0.5f, -0.5f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
			-0.5f, -0.5f,  0.5f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f,

			// Right face
			0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
			0.5f,  0.5f, -0.5f,  1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
			0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
			0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 0.0f, 0.0f, 0.0f,

			// Bottom face
			-0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f,
			 0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,
			 0.5f, -0.5f,  0.5f, 0.0f, -1.0f, 0.0f, 1.0f, 1.0f,
			-0.5f, -0.5f,  0.5f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f,

			// Top face
			-0.5f, 0.5f, -0.5f, 0.0f,  1.0f, 0.0f, 0.0f, 1.0f,
			 0.5f, 0.5f,  0.5f, 0.0f,  1.0f, 0.0f, 1.0f, 0.0f,
			 0.5f, 0.5f, -0.5f, 0.0f,  1.0f, 0.0f, 1.0f, 1.0f,
			-0.5f, 0.5f,  0.5f, 0.0f,  1.0f, 0.0f, 0.0f, 0.0f,

      // Plane
			-25.f, -5.f, -25.f, 0.0f,  1.0f, 0.0f, 0.0f, 1.0f,
			 25.f, -5.f,  25.f, 0.0f,  1.0f, 0.0f, 1.0f, 0.0f,
			 25.f, -5.f, -25.f, 0.0f,  1.0f, 0.0f, 1.0f, 1.0f,
			-25.f, -5.f,  25.f, 0.0f,  1.0f, 0.0f, 0.0f, 0.0f,
    };
    std::array<uint32_t, 42> cube_indices = {
			0, 1, 2, 2, 3, 0,         // Front face
			4, 5, 6, 5, 4, 7,         // Back face
			8, 9, 10, 10, 11, 8,      // Left face
			12, 13, 14, 14, 15, 12,   // Right face
			16, 17, 18, 18, 19, 16,   // Bottom face
			20, 21, 22, 21, 20, 23,   // Top face
      0, 1, 2, 1, 0, 3          // Plane
    };
    BufferCreation creation{};
    creation.mapped = false;
    creation.memory_access_flags = MemoryAccess::GPU_ONLY;
    creation.usage_flags =
        BufferUsage::Enum(BufferUsage::TransferDst | BufferUsage::Vertex);
    creation.name = "CubeVertexBuffer";
    creation.size = sizeof(Vertex) * max_vertex_count;
    vertex_buffer.handle = rf->create_buffer(creation);
    vertex_buffer.current_size = 0;

    rf->copy_data_to_buffer(cube_vertices, vertex_buffer.handle, 0, sizeof(Vertex) * 28);
    vertex_buffer.current_size += 28;

    creation.mapped = false;
    creation.memory_access_flags = MemoryAccess::GPU_ONLY;
    creation.usage_flags =
        BufferUsage::Enum(BufferUsage::TransferDst | BufferUsage::Index);
    creation.name = "CubeIndexBuffer";
    creation.size = sizeof(u32) * max_index_count;
    index_buffer.handle = rf->create_buffer(creation);
    rf->copy_data_to_buffer(cube_indices.data(), index_buffer.handle, 0,
                            sizeof(u32) * cube_indices.size());
    index_buffer.current_size += cube_indices.size();
  }

  CameraConfiguration cam_config{};
  camera.init(cam_config);

  mesh_draws.init(&MemoryService::instance()->system_allocator, MESH_COUNT, MESH_COUNT);
  mesh_draws[0] = {
    36, 0, 0
  };
  mesh_draws[1] = {
    6, 36, 24
  };

  scene.init(&MemoryService::instance()->system_allocator);
  i32 root = scene.add_node(-1, 0, "Root");

  i32 node = scene.add_node(root, 1, "Cube0");
  scene.mesh_to_node[node] = 0;

  node = scene.add_node(root, 1, "Plane");
  scene.mesh_to_node[node] = 1;
}

void Sandbox::run() {
  clock.start();
  last_time = clock.get_elapsed_time_s();
  f64 target_frame_seconds_ms = 1000.0 / 60.0;

  while (!platform->requested_exit) {
    platform->handle_os_messages();

    if (!platform->is_suspended) {
      f64 current_time = clock.get_elapsed_time_s();
      delta_time = current_time - last_time;
      f64 frame_start_time_ms = platform->get_absolute_time_ms();

      InputService::instance()->update(delta_time);
      JobService::instance()->update();

      camera.update(delta_time);

      render_frame();

      HELIX_PROFILER_ZONE("Calculate remaining time and update frame count",
                          HELIX_PROFILER_COLOR_DEFAULT)
      f64 frame_end_time_ms = platform->get_absolute_time_ms();
      f64 frame_elapsed_time_ms = frame_end_time_ms - frame_start_time_ms;
      f64 remaining_time_ms = target_frame_seconds_ms - frame_elapsed_time_ms;

      if (remaining_time_ms > 0 && limit_frames) {
        // If there is time left, give it back to the OS.
        HELIX_PROFILER_ZONE("Application sleep", HELIX_PROFILER_COLOR_DEFAULT)
        platform->sleep(static_cast<u32>(remaining_time_ms - 1));
        HELIX_PROFILER_ZONE_END()
      }
      last_time = current_time;
      HELIX_PROFILER_ZONE_END()
    }
    HELIX_PROFILER_FRAME("Frame");
  }
}

void Sandbox::render_frame() {
  RendererFrontEnd *rf = RendererFrontEnd::instance();

  if (rf->begin_frame(nullptr)) {
    i32 width, height;
    Platform::instance()->get_window_size(&width, &height);
    glm::mat4 view_proj = camera.get_projection() * camera.get_view();
    static bool show_shadow_map_debug = false;
    static bool freeze_camera = false;
    static glm::mat4 last_cam_view;

    // Update Uniforms
    static glm::vec3 light_dir = glm::vec3(0.5f, -1.f, 0.f);
    glm::vec3 light_dir_norm = glm::normalize(light_dir);

    if(!freeze_camera)
      update_cascades(camera, light_dir_norm, camera.near_plane, camera.far_plane);
    UniformData uniform_data{};
    uniform_data.view_proj = view_proj;
    uniform_data.light_view_proj = get_light_view_proj(camera, light_dir_norm, camera.near_plane, camera.far_plane);
    for (u32 i = 0; i < CASCADE_COUNT; ++i) {
      uniform_data.light_view_projs[i] = 
        cascades_data.cascade_matrices[i];
      uniform_data.cascade_splits[i] = cascades_data.cascade_splits[i];
    }
    uniform_data.light_dir_shadow_map = glm::vec4(
        light_dir_norm.x, light_dir_norm.y, light_dir_norm.z, 
         /* shadow_map.index */cascade_textures[0].index);

    void *buffer_data = rf->get_buffer_map(uniforms[rf->current_frame_in_flight]);
    memcpy(buffer_data, &uniform_data, sizeof(UniformData));

    // Shadow Pass
    BarrierDescription barrier{};
    barrier.resource_type = ResourceType::Texture;
    barrier.src_state = ResourceState::Sampled;
    barrier.dst_state = ResourceState::DepthAttachment;
    barrier.resource_handle = shadow_map;
    rf->graphics_context->resource_barrier(&barrier);

    u32 offsets[2] = {0, 0};
    u32 extents[2] = {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE};
    for (u32 i = 0; i < CASCADE_COUNT; ++i) {
      rf->device->set_render_pass_texture(
        shadow_pass, cascade_textures[i], true, 0);

      rf->graphics_context->bind_renderpass(shadow_pass, extents, offsets);
      rf->graphics_context->bind_pipeline(shadow_pipeline);
      rf->graphics_context->bind_set(
          scene_constants_sets[rf->current_frame_in_flight], 0);
      rf->graphics_context->bind_vertex_buffer(vertex_buffer.handle, 0, 1);
      rf->graphics_context->bind_index_buffer(index_buffer.handle, 0, false);

      rf->graphics_context->set_scissor(0.f, 0.f, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
      rf->graphics_context->set_viewport(0.f, 0.f, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 0.f,
                                         1.f);

      draw_scene_shadow(scene, rf->graphics_context, i);
      rf->graphics_context->end_current_pass();
    }

    // Main pass
    barrier.resource_type = ResourceType::Texture;
    barrier.src_state = ResourceState::DepthAttachment;
    barrier.dst_state = ResourceState::Sampled;
    barrier.resource_handle = shadow_map;
    rf->graphics_context->resource_barrier(&barrier);

    barrier.resource_type = ResourceType::Texture;
    barrier.src_state = ResourceState::Present;
    barrier.dst_state = ResourceState::RenderTarget;
    barrier.resource_handle = rf->backbuffers[rf->backbuffer_index];
    rf->graphics_context->resource_barrier(&barrier);

    barrier.resource_type = ResourceType::Texture;
    barrier.src_state = ResourceState::DepthAttachment;
    barrier.dst_state = ResourceState::DepthAttachment;
    barrier.resource_handle = rf->depth_texture;
    rf->graphics_context->resource_barrier(&barrier);

    rf->device->set_render_pass_texture(
        rf->main_pass, rf->backbuffers[rf->backbuffer_index], false, 0);

    extents[0] = width;
    extents[1] = height;
    rf->graphics_context->bind_renderpass(rf->main_pass, extents, offsets);
    rf->graphics_context->bind_pipeline(hello_triangle);
    rf->graphics_context->bind_set(rf->bindless_set, 0);
    rf->graphics_context->bind_set(
        scene_constants_sets[rf->current_frame_in_flight], 1);
    rf->graphics_context->set_scissor(0.f, 0.f, (f32)width, (f32)height);
    rf->graphics_context->set_viewport(0.f, 0.f, (f32)width, (f32)height, 0.f,
                                       1.f);

    draw_scene(scene, rf->graphics_context, camera);

    if (freeze_camera) {
      rf->graphics_context->bind_pipeline(frustum_debug);
      rf->graphics_context->bind_set(
        scene_constants_sets[rf->current_frame_in_flight], 0);
      f32 near_plane = camera.near_plane;
      glm::vec4 colors[CASCADE_COUNT] = {
        glm::vec4(1.f, 0.f, 0.f, 1.f),
        glm::vec4(0.f, 1.f, 0.f, 1.f),
        glm::vec4(0.f, 0.f, 1.f, 1.f),
        glm::vec4(1.f, 1.f, 1.f, 1.f),
      };
      for (u32 i = 0; i < CASCADE_COUNT; ++i) {
        f32 far_plane = -cascades_data.cascade_splits[i];
        struct PC {
          glm::mat4 invViewProjSplit;
          glm::vec4 color;
        } pc;
        pc.color = colors[i];
        glm::mat4 proj = glm::perspective(
            camera.fov, camera.aspect_ratio,
            near_plane, far_plane);
        pc.invViewProjSplit = glm::inverse(proj * last_cam_view);
        rf->graphics_context->push_shader_constants(sizeof(PC), &pc);
        rf->graphics_context->draw(24, 1, 0, 0);
        near_plane = far_plane;
        // Light matrix
        pc.color.w = 0.5f;
        pc.invViewProjSplit = glm::inverse(cascades_data.cascade_matrices[i]);
        rf->graphics_context->push_shader_constants(sizeof(PC), &pc);
        rf->graphics_context->draw(24, 1, 0, 0);

      }
    } else {
      last_cam_view = camera.get_view();
    }

    // Shadow Map Debug
    if (show_shadow_map_debug) {
      rf->graphics_context->set_viewport(0.f, height / 2.f, width / 2.f, height / 2.f,
                                   0.f, 1.f);
      rf->graphics_context->set_scissor(0.f, height / 2.f, (f32)width / 2.f, (f32)height / 2.f);
      rf->graphics_context->bind_pipeline(shadow_map_debug);
      rf->graphics_context->bind_set(rf->bindless_set, 0);
      rf->graphics_context->draw(3, 1, 0, /* shadow_map.index */cascade_textures[1].index);

    }


    // Imgui
    ImguiFrontend *imgui = ImguiFrontend::instance();
    imgui->begin_frame();

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove;
    ImGui::SetNextWindowBgAlpha(0.25f);
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(256, 96), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Frame time", NULL, flags)) {
      ImGui::Text("Frame time: %.3f ms", delta_time * 1000.f);
      ImGui::Checkbox("Limit Frames", &limit_frames);
      ImGui::DragFloat3("LightDir", &light_dir.x);
      ImGui::SliderFloat("Split Lambda", &split_lambda, 0.1f, 1.f);
      ImGui::End();
    }

    ImGui::SetNextWindowPos(ImVec2(0, 96), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(256, 80), ImGuiCond_Always        );
    if (ImGui::Begin("Load Model", NULL, flags)) {
      if (ImGui::Button("Load Model")) {
        char *file_path = nullptr;
        char *file_name = nullptr;
        if (FileService::open_file_dialog(
                &file_name, &file_path,
                &MemoryService::instance()->system_allocator)) {
          if (file_path && file_name) {
            string_replace(file_path, '\\', '/');
            load_gltf_scene(scene, mesh_draws, vertex_buffer, index_buffer, file_path, file_name);

            MemoryService::instance()->system_allocator.deallocate(file_name);
            MemoryService::instance()->system_allocator.deallocate(file_path);
          }
        }
      }
      ImGui::Checkbox("Show Shadow Map Debug", &show_shadow_map_debug);
      ImGui::Checkbox("Freeze Camera", &freeze_camera);
      ImGui::End();
    }

    scene_ui.render_scene_tree_ui(scene, 0);
    scene_ui.render_node_property_ui(scene, scene_ui.selected_node);

    imgui->render_frame();

    rf->graphics_context->end_current_pass();

    barrier.resource_type = ResourceType::Texture;
    barrier.src_state = ResourceState::RenderTarget;
    barrier.dst_state = ResourceState::Present;
    barrier.resource_handle = rf->backbuffers[rf->backbuffer_index];

    rf->graphics_context->resource_barrier(&barrier);
    rf->end_frame(nullptr);
  }

	scene.update_scene_transforms();
}

void Sandbox::shutdown() {
  mesh_draws.shutdown();
  scene.shutdown();
  camera.shutdown();
  RendererFrontEnd *rf = RendererFrontEnd::instance();

  for (u32 i = 0; i < max_frames_in_flight; ++i) {
    rf->destroy_buffer(uniforms[i]);
    rf->destroy_binding_set(scene_constants_sets[i]);
  }
  uniforms.shutdown();
  scene_constants_sets.shutdown();
  rf->destroy_binding_set_layout(scene_constants_set_layout);
  rf->destroy_sampler(shadow_map_sampler);
  rf->destroy_pipeline(hello_triangle);
  rf->destroy_pipeline(shadow_map_debug);
  rf->destroy_pipeline(shadow_pipeline);
  rf->destroy_pipeline(frustum_debug);
  rf->destroy_render_pass(shadow_pass);
  rf->destroy_buffer(vertex_buffer.handle);
  rf->destroy_buffer(index_buffer.handle);
  rf->destroy_texture(shadow_map);
  for (u32 i = 0; i < CASCADE_COUNT; ++i) {
    rf->destroy_texture(cascade_textures[i]);
  }


  EventService *event_service = EventService::instance();
  event_service->unregister_event(SDL_EVENT_QUIT, 0, application_on_event);
  event_service->unregister_event(SDL_EVENT_KEY_DOWN, 0, application_on_key);
  event_service->unregister_event(SDL_EVENT_KEY_UP, 0, application_on_key);
  event_service->unregister_event(SDL_EVENT_WINDOW_RESIZED, this,
                                  application_on_window_resize);
  Engine::shutdown();
}

bool application_on_event(u16 event_code, void *sender, void *listener,
                          EventContext context) {
  switch (event_code) {
  case SDL_EVENT_QUIT: {
    Platform::instance()->requested_exit = true;
    return true;
  } break;
  }
  return false;
}

bool application_on_key(u16 event_code, void *sender, void *listener,
                        EventContext context) {
  switch (event_code) {
  case SDL_EVENT_KEY_DOWN: {
    u16 key_code = context.data.u16[0];
    if (key_code == SDL_SCANCODE_ESCAPE) {
      EventContext context{};
      EventService::instance()->fire_event(SDL_EVENT_QUIT, 0, context);
      return true;
    } else if (key_code == SDL_SCANCODE_A) {
      HWARN("Explicit A was pressed");
    } else if (key_code == SDL_SCANCODE_F) {
      Platform::instance()->toggle_fullscreen();
    } else {
      char k = (char)SDL_GetKeyFromScancode((SDL_Scancode)key_code,
                                            SDL_KMOD_NONE, false);
      HDEBUG("Key {} was pressed", k);
    }
  } break;
  case SDL_EVENT_KEY_UP: {
    u16 key_code = context.data.u16[0];
    if (key_code == SDL_SCANCODE_B) {
      HWARN("Explicit B was released");
    } else {
      char k = (char)SDL_GetKeyFromScancode((SDL_Scancode)key_code,
                                            SDL_KMOD_NONE, false);
      HDEBUG("Key {} was released", k);
    }
  } break;
  }
  return false;
}

bool application_on_window_resize(u16 event_code, void *sender, void *listener,
                                  EventContext context) {
  RendererFrontEnd *rf = RendererFrontEnd::instance();

  i32 width, height;
  Platform::instance()->get_window_size(&width, &height);
  rf->resize_texture(rf->depth_texture, width, height);

  return false;
}

static void draw_scene_shadow(Scene &scene, Context *ctx, u32 cascade_level) {
  for (i32 i = 0; i < scene.hierarchy.size; ++i) {
    if (scene.mesh_to_node.count(i)) {
      const i32 p_mesh_id = scene.mesh_to_node[i];
      const MeshDraw &draw = mesh_draws[p_mesh_id];
      struct PC {
        glm::mat4 model;
        u32 cascade_level;
      } pc;
      pc.cascade_level = cascade_level;
      pc.model = scene.global_transforms[i];
      ctx->push_shader_constants(sizeof(PC), &pc);
      ctx->draw_indexed(draw.primitive_count, 1, draw.index_buffer_offset,
          draw.vertex_buffer_offset, RendererFrontEnd::instance()->wood_texture.index);
    }
  }
}

static void draw_scene(Scene &scene, Context *ctx, const Camera& camera) {
  glm::mat4 view = camera.get_view();
  for (i32 i = 0; i < scene.hierarchy.size; ++i) {
    if (scene.mesh_to_node.count(i)) {
      const i32 p_mesh_id = scene.mesh_to_node[i];
      const MeshDraw &draw = mesh_draws[p_mesh_id];
      struct PC {
        glm::mat4 model;
        glm::mat4 view;
      } pc;
      pc.model = scene.global_transforms[i];
      pc.view = view;
      ctx->push_shader_constants(sizeof(PC), &pc);
      ctx->draw_indexed(draw.primitive_count, 1, draw.index_buffer_offset,
          draw.vertex_buffer_offset, RendererFrontEnd::instance()->wood_texture.index);
    }
  }
}

static glm::mat4 get_light_view_proj(Camera &camera,
    const glm::vec3 &light_dir,
    f32 near, f32 far) {
  // Get frustum corners
  ScopedAllocator scope_allocator(&MemoryService::instance()->stack_allocator);
  StackAllocator *stack_allocator = scope_allocator.allocator;
  Array<glm::vec4> frustum_corners{};
  frustum_corners.init(stack_allocator, 8);

  const glm::mat4 split_proj = glm::perspective(glm::radians(camera.fov),
                                                camera.aspect_ratio, near, far);
  glm::mat4 inv_view_proj = glm::inverse(split_proj * camera.get_view());

  for (i32 x = 0; x < 2; ++x) {
    for (i32 y = 0; y < 2; ++y) {
      for (i32 z = 0; z < 2; ++z) {
        glm::vec4 p = glm::vec4(2.f * x - 1.f,
                                2.f * y - 1.f,
                                1.f * z,
                                1.f);
        p = inv_view_proj * p;
        frustum_corners.push(p / p.w);
      }
    }
  }

  glm::vec3 frustum_center = glm::vec3(0, 0, 0);
  for (const glm::vec4& v : frustum_corners) {
      frustum_center += glm::vec3(v);
  }
  frustum_center /= frustum_corners.size;

  glm::mat4 light_view = glm::lookAt(
    frustum_center - light_dir,
    frustum_center,
    glm::vec3(0.0f, 1.0f, 0.0f));

  f32 min_x = std::numeric_limits<f32>::max();
  f32 max_x = std::numeric_limits<f32>::lowest();
  f32 min_y = std::numeric_limits<f32>::max();
  f32 max_y = std::numeric_limits<f32>::lowest();
  f32 min_z = std::numeric_limits<f32>::max();
  f32 max_z = std::numeric_limits<f32>::lowest();
  for (const glm::vec4& v : frustum_corners) {
    const glm::vec4 trf = light_view * v;
    min_x = std::min(min_x, trf.x);
    max_x = std::max(max_x, trf.x);
    min_y = std::min(min_y, trf.y);
    max_y = std::max(max_y, trf.y);
    min_z = std::min(min_z, trf.z);
    max_z = std::max(max_z, trf.z);
  }

  glm::mat4 light_proj = glm::ortho(min_x, max_x, min_y, max_y, min_z, max_z);

  return light_proj * light_view;
}

static void update_cascades(
  Camera &camera, const glm::vec3 &light_dir, f32 near, f32 far) {
  // PSSM split algorithm (gpugems3)
  f32 cascade_splits[CASCADE_COUNT];

  // We split our camera's frustum (in world space) into separate
  // cascades.
  // We then convert the cascade split into the depth range [0, 1]
  // NOTE: C_0 would always be the near plane and
  //       C_m would always be the far plane
  for (u32 i = 0; i < CASCADE_COUNT; ++i) {
    // NOTE: We skip C_0
    f32 pwr = ((f32)i + 1) / CASCADE_COUNT; 
    f32 split_log = near * std::pow(far / near, pwr);
    f32 split_uniform = near + (far - near) * pwr;
    f32 c_i = split_lambda * (split_log - split_uniform) + split_uniform;
    cascades_data.cascade_splits[i] = -c_i;
    cascade_splits[i] = (c_i - near) / (far - near);
  }

  f32 near_split = 0.f;
  // Calculate the light matrix for each cascade
  for (u32 i = 0; i < CASCADE_COUNT; ++i) {
    f32 far_split = cascade_splits[i];
    glm::vec3 frustum_corners[8] = {
      glm::vec3(-1.0f,  1.0f, 0.0f),
      glm::vec3( 1.0f,  1.0f, 0.0f),
      glm::vec3( 1.0f, -1.0f, 0.0f),
      glm::vec3(-1.0f, -1.0f, 0.0f),
      glm::vec3(-1.0f,  1.0f,  1.0f),
      glm::vec3( 1.0f,  1.0f,  1.0f),
      glm::vec3( 1.0f, -1.0f,  1.0f),
      glm::vec3(-1.0f, -1.0f,  1.0f),
    };
    
    glm::mat4 inv_view_proj = glm::inverse(camera.get_projection() * camera.get_view());
    for (u32 j = 0; j < 8; ++j) {
      glm::vec4 world_corner = inv_view_proj * glm::vec4(frustum_corners[j], 1.f);
      frustum_corners[j] = world_corner / world_corner.w;
    }

    // Adjust the frustum to the cascade's scale
    for (u32 j = 0; j < 4; ++j) {
      glm::vec3 dist = frustum_corners[j + 4] - frustum_corners[j];
      frustum_corners[j + 4] = frustum_corners[j] + (dist * far_split);
      frustum_corners[j] = frustum_corners[j] + (dist * near_split);
    }
    
    // Create light space view matrix based on the center of the frustum
    glm::vec3 frustum_center = glm::vec3(0.f);
    for (u32 j = 0; j < 8; ++j) {
      frustum_center += frustum_corners[j];
    }
    frustum_center /= 8.f;

    float radius = 0.0f;
    for (uint32_t j = 0; j < 8; j++) {
      float distance = glm::length(frustum_corners[j] - frustum_center);
      radius = glm::max(radius, distance);
    }
    radius = std::ceil(radius * 16.0f) / 16.0f;

    glm::vec3 max_extents = glm::vec3(radius);
    glm::vec3 min_extents = -max_extents;

    glm::mat4 light_view = glm::lookAt(frustum_center - light_dir * -min_extents.z, frustum_center, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 light_proj = glm::ortho(min_extents.x, max_extents.x, min_extents.y, max_extents.y, -10.f, max_extents.z - min_extents.z);
    light_proj[1][1] *= -1.f;
    cascades_data.cascade_matrices[i] = light_proj * light_view;
    
    near_split = far_split;
  }
}


} // namespace hlx
