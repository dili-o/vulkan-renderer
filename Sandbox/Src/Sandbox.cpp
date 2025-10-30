#include "Sandbox.hpp"
#include "Core/Clock.hpp"
#include "Core/Engine.hpp"
#include "Core/Event.hpp"
#include "Core/Input.hpp"
#include "Core/Job.hpp"
#include "Core/Log.hpp"
#include "Core/Profiler.hpp"
#include "Platform/Platform.hpp"
#include "Renderer/GPUResourceTypes.hpp"
#include "Renderer/GPUResources.hpp"
#include "Renderer/ImguiFrontend.hpp"
#include "Renderer/RendererBackend.hpp"
#include "Renderer/RendererFrontEnd.hpp"
#include "Renderer/RendererTypes.hpp"
// Vendors
#include <imgui_internal.h>
#include <stb_image.h>

namespace Helix {

Platform *platform = nullptr;
Clock clock;
PipelineHandle hello_triangle;
PipelineHandle shadow_pipeline;
RenderPassHandle shadow_pass;
TextureHandle shadow_map;
BufferHandle cube_vertex;
BufferHandle cube_index;
SamplerHandle shadow_map_sampler;

struct UniformData{
  glm::mat4 view_proj;
  glm::mat4 light_view_proj;
};

Array<BufferHandle> uniforms;
BindingSetLayoutHandle scene_constants_set_layout;
Array<BindingSetHandle> scene_constants_sets;

static bool application_on_event(u16 event_code, void *sender, void *listener,
                                 EventContext context);

static bool application_on_key(u16 event_code, void *sender, void *listener,
                               EventContext context);

static bool application_on_window_resize(u16 event_code, void *sender,
                                         void *listener, EventContext context);

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
    creation.width = 1024;
    creation.height = 1024;
    creation.usage = TextureUsage::Enum(TextureUsage::Sampled | TextureUsage::Depth);
    creation.format = TextureFormat::D32;
    creation.sampler = shadow_map_sampler;

    shadow_map = rf->create_texture(creation);
  }
  {
    RenderPassCreation creation{};
    creation.add_depth_attachment(LoadOp::Clear, StoreOp::Store, shadow_map);
    shadow_pass = rf->create_render_pass(creation);
    rf->device->set_render_pass_texture(
    shadow_pass, shadow_map, true, 0);
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
    creation.cull_mode = CullMode::None;
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
    creation.cull_mode = CullMode::None;
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
			 25.f, -5.f, -25.f, 0.0f,  1.0f, 0.0f, 1.0f, 0.0f,
			-25.f, -5.f,  25.f, 0.0f,  1.0f, 0.0f, 1.0f, 1.0f,
			 25.f, -5.f,  25.f, 0.0f,  1.0f, 0.0f, 0.0f, 0.0f
    };
    std::array<uint32_t, 42> cube_indices = {
			0, 1, 2, 2, 3, 0,         // Front face
			4, 5, 6, 5, 4, 7,         // Back face
			8, 9, 10, 10, 11, 8,      // Left face
			12, 13, 14, 14, 15, 12,   // Right face
			16, 17, 18, 18, 19, 16,   // Bottom face
			20, 21, 22, 21, 20, 23,   // Top face
      0, 1, 2, 2, 1, 3          // Plane
    };
    BufferCreation creation{};
    creation.mapped = false;
    creation.memory_access_flags = MemoryAccess::GPU_ONLY;
    creation.usage_flags =
        BufferUsage::Enum(BufferUsage::TransferDst | BufferUsage::Vertex);
    creation.name = "CubeVertexBuffer";
    creation.size = sizeof(cube_vertices);
    cube_vertex = rf->create_buffer(creation);
    rf->copy_data_to_buffer(cube_vertices, cube_vertex, creation.size);

    creation.mapped = false;
    creation.memory_access_flags = MemoryAccess::GPU_ONLY;
    creation.usage_flags =
        BufferUsage::Enum(BufferUsage::TransferDst | BufferUsage::Index);
    creation.name = "CubeIndexBuffer";
    creation.size = sizeof(u32) * cube_indices.size();
    cube_index = rf->create_buffer(creation);
    rf->copy_data_to_buffer((void *)cube_indices.data(), cube_index,
                            creation.size);
  }

  CameraConfiguration cam_config{};
  camera.init(cam_config);
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
    static glm::vec3 light_pos = glm::vec3(-2.5f, 4.f, -1.f);
    static glm::vec3 light_look_at = glm::vec3(0.f);

    // Update Uniforms
    f32 near_plane = 1.0f, far_plane = 20.f;
    UniformData uniform_data{};
    uniform_data.view_proj = view_proj;
    glm::mat4 ortho = glm::ortho(-10.0f, 10.0f,-10.0f, 10.0f,
                          near_plane, far_plane);
    ortho[1][1] *= -1.f;
    uniform_data.light_view_proj =  ortho *
      glm::lookAt(light_pos, light_look_at, glm::vec3(0.f, 1.f, 0.f));
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
    u32 extents[2] = {1024, 1024};
    rf->graphics_context->bind_renderpass(shadow_pass, extents, offsets);
    rf->graphics_context->bind_pipeline(shadow_pipeline);
    rf->graphics_context->bind_set(
        scene_constants_sets[rf->current_frame_in_flight], 0);
    rf->graphics_context->bind_vertex_buffer(cube_vertex, 0, 1);
    rf->graphics_context->bind_index_buffer(cube_index, 0, false);

    rf->graphics_context->set_scissor(0.f, 0.f, 1024.f, 1024.f);
    rf->graphics_context->set_viewport(0.f, 0.f, 1024.f, 1024.f, 0.f,
                                       1.f);

    // Cube
    rf->graphics_context->draw_indexed(36, 1, 0, 0, rf->wood_texture.index);
    // Plane
    rf->graphics_context->draw_indexed(6, 1, 36, 24, rf->wood_texture.index);
    rf->graphics_context->end_current_pass();

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

    struct Constants{
      glm::vec3 light_dir;
      u32 shadow_map_index;
    };
    Constants pc{(light_look_at - light_pos), shadow_map.index};
    rf->graphics_context->push_shader_constants(sizeof(Constants), &pc);
    // Cube
    rf->graphics_context->draw_indexed(36, 1, 0, 0, rf->wood_texture.index);
    // Plane
    rf->graphics_context->draw_indexed(6, 1, 36, 24, rf->wood_texture.index);

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
      ImGui::DragFloat3("LightPos", &light_pos.x);
      ImGui::DragFloat3("LightLook", &light_look_at.x);
      ImGui::End();
    }

    imgui->render_frame();

    rf->graphics_context->end_current_pass();

    barrier.resource_type = ResourceType::Texture;
    barrier.src_state = ResourceState::RenderTarget;
    barrier.dst_state = ResourceState::Present;
    barrier.resource_handle = rf->backbuffers[rf->backbuffer_index];

    rf->graphics_context->resource_barrier(&barrier);
    rf->end_frame(nullptr);
  }
}

void Sandbox::shutdown() {
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
  rf->destroy_pipeline(shadow_pipeline);
  rf->destroy_render_pass(shadow_pass);
  rf->destroy_buffer(cube_vertex);
  rf->destroy_buffer(cube_index);
  rf->destroy_texture(shadow_map);

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

} // namespace Helix
