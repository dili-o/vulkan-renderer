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
#include "Renderer/ImguiFrontend.hpp"
#include "Renderer/RendererBackend.hpp"
#include "Renderer/RendererFrontEnd.hpp"
#include "Renderer/RendererTypes.hpp"
// Vendors
#include <imgui_internal.h>

namespace Helix {

Platform *platform = nullptr;
Clock clock;
PipelineHandle hello_triangle;
BufferHandle cubeVertex;
BufferHandle cubeIndex;

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
    creation.set_layout_count = 0;
    creation.enable_depth_write = true;
    creation.enable_depth_test = true;
    creation.compare_op = CompareOp::Less;
    creation.render_pass = rf->main_pass;

    hello_triangle = rf->create_pipeline(creation);
  }
  {
    std::array<glm::vec4, 24> cubeVertices = {
        // +X face
        glm::vec4(+0.5f, -0.5f, -0.5f, 1.0f),
        glm::vec4(+0.5f, +0.5f, -0.5f, 1.0f),
        glm::vec4(+0.5f, +0.5f, +0.5f, 1.0f),
        glm::vec4(+0.5f, -0.5f, +0.5f, 1.0f),

        // -X face
        glm::vec4(-0.5f, -0.5f, +0.5f, 1.0f),
        glm::vec4(-0.5f, +0.5f, +0.5f, 1.0f),
        glm::vec4(-0.5f, +0.5f, -0.5f, 1.0f),
        glm::vec4(-0.5f, -0.5f, -0.5f, 1.0f),

        // +Y face
        glm::vec4(-0.5f, +0.5f, -0.5f, 1.0f),
        glm::vec4(-0.5f, +0.5f, +0.5f, 1.0f),
        glm::vec4(+0.5f, +0.5f, +0.5f, 1.0f),
        glm::vec4(+0.5f, +0.5f, -0.5f, 1.0f),

        // -Y face
        glm::vec4(-0.5f, -0.5f, +0.5f, 1.0f),
        glm::vec4(-0.5f, -0.5f, -0.5f, 1.0f),
        glm::vec4(+0.5f, -0.5f, -0.5f, 1.0f),
        glm::vec4(+0.5f, -0.5f, +0.5f, 1.0f),

        // +Z face
        glm::vec4(-0.5f, -0.5f, +0.5f, 1.0f),
        glm::vec4(+0.5f, -0.5f, +0.5f, 1.0f),
        glm::vec4(+0.5f, +0.5f, +0.5f, 1.0f),
        glm::vec4(-0.5f, +0.5f, +0.5f, 1.0f),

        // -Z face
        glm::vec4(+0.5f, -0.5f, -0.5f, 1.0f),
        glm::vec4(-0.5f, -0.5f, -0.5f, 1.0f),
        glm::vec4(-0.5f, +0.5f, -0.5f, 1.0f),
        glm::vec4(+0.5f, +0.5f, -0.5f, 1.0f),
    };
    std::array<uint32_t, 36> cubeIndices = {// +X face
                                            0, 1, 2, 0, 2, 3,

                                            // -X face
                                            4, 5, 6, 4, 6, 7,

                                            // +Y face
                                            8, 9, 10, 8, 10, 11,

                                            // -Y face
                                            12, 13, 14, 12, 14, 15,

                                            // +Z face
                                            16, 17, 18, 16, 18, 19,

                                            // -Z face
                                            20, 21, 22, 20, 22, 23};
    BufferCreation creation{};
    creation.mapped = false;
    creation.memory_access_flags = MemoryAccess::GPU_ONLY;
    creation.usage_flags =
        BufferUsage::Enum(BufferUsage::TransferDst | BufferUsage::Vertex);
    creation.name = "CubeVertexBuffer";
    creation.size = sizeof(glm::vec4) * 24;
    cubeVertex = rf->create_buffer(creation);
    rf->copy_data_to_buffer(&cubeVertices[0], cubeVertex, creation.size);

    creation.mapped = false;
    creation.memory_access_flags = MemoryAccess::GPU_ONLY;
    creation.usage_flags =
        BufferUsage::Enum(BufferUsage::TransferDst | BufferUsage::Index);
    creation.name = "CubeIndexBuffer";
    creation.size = sizeof(u32) * 36;
    cubeIndex = rf->create_buffer(creation);
    rf->copy_data_to_buffer((void *)cubeIndices.data(), cubeIndex,
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

    BarrierDescription barrier{};
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

    rf->graphics_context->bind_renderpass(rf->main_pass);
    rf->graphics_context->bind_pipeline(hello_triangle);
    rf->graphics_context->bind_vertex_buffer(cubeVertex, 0, 1);
    rf->graphics_context->bind_index_buffer(cubeIndex, 0, false);

    rf->graphics_context->set_scissor(0.f, 0.f, (f32)width, (f32)height);
    rf->graphics_context->set_viewport(0.f, 0.f, (f32)width, (f32)height, 0.f,
                                       1.f);
    rf->graphics_context->push_shader_constants(sizeof(glm::mat4), &view_proj);
    rf->graphics_context->draw_indexed(36, 1, 0, 0, 0);

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
      // ImGui::Checkbox("Freeze Camera", &freeze_camera);
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
  rf->destroy_pipeline(hello_triangle);
  rf->destroy_buffer(cubeVertex);
  rf->destroy_buffer(cubeIndex);

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
