#include "ImguiFrontend.hpp"
#include "Core/Memory.hpp"
#include "Core/Profiler.hpp"
#include "Renderer/GPUResources.hpp"
#include "Renderer/RendererBackend.hpp"
#include "Renderer/RendererFrontEnd.hpp"
// Vendor
#include <imgui_internal.h>

namespace hlx {

static ImguiFrontend *s_imgui_service{nullptr};
ImguiFrontend *ImguiFrontend ::instance() { return s_imgui_service; }

void ImguiFrontend::init(void *config_) {
  if (s_imgui_service) {
    HELIX_SERVICE_RECREATE_MSG(ImguiFrontend);
    return;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  ImGuiIO &io = ImGui::GetIO();
  io.BackendRendererName = "Helix";
  io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

  // Create font texture atlas
  u8 *pixels;
  i32 width, height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

  RendererFrontEnd *rf = RendererFrontEnd::instance();
  TextureCreation texture_creation{};
  texture_creation.width = width;
  texture_creation.height = height;
  texture_creation.depth = 1;
  texture_creation.array_layer_count = 1;
  texture_creation.array_base_level = 0;
  texture_creation.mip_level_count = 1;
  texture_creation.mip_base_level = 0;
  texture_creation.usage =
      (TextureUsage::Enum)(TextureUsage::Sampled | TextureUsage::TransferDest);
  texture_creation.format = TextureFormat::R8G8B8A8_UNORM;
  texture_creation.type = TextureType::Texture2D;
  texture_creation.name = "ImGuiFontTexture";
  texture_creation.sampler = rf->default_sampler;

  font_texture = rf->create_texture(texture_creation);
  io.Fonts->TexID = (ImTextureID)&font_texture;

  rf->copy_data_to_image(pixels, font_texture, width * height * sizeof(u32));

  // Create Index and Vertex buffer
  ImguiLayerConfiguration *config = (ImguiLayerConfiguration *)config_;
  HeapAllocator *allocator = &MemoryService::instance()->system_allocator;
  vertex_buffers.init(allocator, config->max_frame_in_flight,
                      config->max_frame_in_flight);
  index_buffers.init(allocator, config->max_frame_in_flight,
                     config->max_frame_in_flight);

  for (u32 i = 0; i < config->max_frame_in_flight; ++i) {
    BufferCreation creation{};
    creation.usage_flags = BufferUsage::Vertex;
    creation.mapped = true;
    creation.memory_access_flags = MemoryAccess::CPU_TO_GPU;
    creation.size = s_vb_size;
    creation.name = "ImGuiVertexBuffer";

    vertex_buffers[i] = rf->create_buffer(creation);

    creation.usage_flags = BufferUsage::Index;
    creation.mapped = true;
    creation.memory_access_flags = MemoryAccess::CPU_TO_GPU;
    creation.size = s_ib_size;
    creation.name = "ImGuiIndexBuffer";

    index_buffers[i] = rf->create_buffer(creation);
  }

  // Create Pipeline
  ScopedAllocator scope_allocator(&MemoryService::instance()->stack_allocator);
  StackAllocator *stack_allocator = scope_allocator.allocator;
  PipelineCreation pipeline_creation;
  pipeline_creation.name = IMGUI_PIPELINE_NAME;
  pipeline_creation.shader_create_infos = (ShaderCreateInfo *)halloca(
      sizeof(ShaderCreateInfo) * 2, stack_allocator);
  pipeline_creation.shader_create_infos[0] = {"ImguiBindless.vert",
                                              ShaderStage::Vertex};
  pipeline_creation.shader_create_infos[1] = {"ImguiBindless.frag",
                                              ShaderStage::Fragment};
  pipeline_creation.shader_count = 2;
  pipeline_creation.pipeline_type = PipelineType::Graphics;
  pipeline_creation.cull_mode = CullMode::None;
  pipeline_creation.set_layouts[0] =
      RendererFrontEnd::instance()->bindless_set_layout;
  pipeline_creation.set_layout_count = 1;
  pipeline_creation.render_pass = rf->main_pass;
  pipeline_creation.enable_depth_write = false;
  pipeline_creation.enable_depth_test = false;

  pipeline = RendererFrontEnd::instance()->create_pipeline(pipeline_creation);

  platform_init(config_);

  s_imgui_service = this;
  HELIX_SERVICE_INIT_MSG(ImguiFrontend);
}

void ImguiFrontend::shutdown() {
  RendererFrontEnd *rf = RendererFrontEnd::instance();
  rf->destroy_texture(font_texture);
  for (u32 i = 0; i < vertex_buffers.size; ++i) {
    rf->destroy_buffer(vertex_buffers[i]);
    rf->destroy_buffer(index_buffers[i]);
  }
  rf->destroy_pipeline(pipeline);

  vertex_buffers.shutdown();
  index_buffers.shutdown();

  s_imgui_service = nullptr;
  HELIX_SERVICE_SHUTDOWN_MSG(ImguiFrontend);
}

ImGuiContext *ImguiFrontend::get_ImGuiContext() {
  return ImGui::GetCurrentContext();
}

void ImguiFrontend::render_frame() {
  HELIX_PROFILER_FUNCTION();
  ImGui::Render();

  ImDrawData *draw_data = ImGui::GetDrawData();

  // Avoid rendering when minimized, scale coordinates for retina displays
  // (screen coordinates != framebuffer coordinates)
  i32 fb_width =
      (i32)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
  i32 fb_height =
      (i32)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
  if (fb_width <= 0 || fb_height <= 0)
    return;

  size_t vertex_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
  size_t index_size = draw_data->TotalIdxCount * sizeof(ImDrawIdx);

  if (vertex_size >= s_vb_size || index_size >= s_ib_size) {
    HERROR("ImGui Backend Error: vertex/index overflow!");
    return;
  }

  if (vertex_size == 0 && index_size == 0) {
    return;
  }

  // NOTE: Command buffer should already be recording
  RendererFrontEnd *rf = RendererFrontEnd::instance();

  u32 current_frame = rf->current_frame_in_flight;
  // Upload vertex and index data
  ImDrawVert *vtx_dst =
      (ImDrawVert *)rf->get_buffer_map(vertex_buffers[current_frame]);
  if (vtx_dst) {
    for (i32 n = 0; n < draw_data->CmdListsCount; n++) {
      const ImDrawList *cmd_list = draw_data->CmdLists[n];
      memcpy(vtx_dst, cmd_list->VtxBuffer.Data,
             cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
      vtx_dst += cmd_list->VtxBuffer.Size;
    }
  }

  ImDrawIdx *idx_dst =
      (ImDrawIdx *)rf->get_buffer_map(index_buffers[current_frame]);
  if (idx_dst) {
    for (i32 n = 0; n < draw_data->CmdListsCount; n++) {
      const ImDrawList *cmd_list = draw_data->CmdLists[n];
      memcpy(idx_dst, cmd_list->IdxBuffer.Data,
             cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
      idx_dst += cmd_list->IdxBuffer.Size;
    }
  }

  rf->graphics_context->bind_pipeline(pipeline);
  rf->graphics_context->bind_vertex_buffer(vertex_buffers[current_frame], 0, 1);
  rf->graphics_context->bind_index_buffer(index_buffers[current_frame], 0,
                                          true);
  rf->graphics_context->set_viewport(0.f, 0.f, (f32)fb_width, (f32)fb_height,
                                     0.f, 1.f);
  rf->graphics_context->bind_set(rf->bindless_set, 0);

  // Setup push constants
  float scale[2];
  scale[0] = 2.0f / draw_data->DisplaySize.x;
  scale[1] = 2.0f / draw_data->DisplaySize.y;
  float translate[2];
  translate[0] = -1.0f - draw_data->DisplayPos.x * scale[0];
  translate[1] = -1.0f - draw_data->DisplayPos.y * scale[1];

  float uniform[4];
  uniform[0] = scale[0];
  uniform[1] = scale[1];
  uniform[2] = translate[0];
  uniform[3] = translate[1];

  rf->graphics_context->push_shader_constants(sizeof(f32) * 4, (void *)uniform);

  // Will project scissor/clipping rectangles into framebuffer space
  ImVec2 clip_off = draw_data->DisplayPos; // (0,0) unless using multi-viewports
  ImVec2 clip_scale =
      draw_data->FramebufferScale; // (1,1) unless using retina display which
                                   // are often (2,2)

  // Render command lists
  i32 counts = draw_data->CmdListsCount;

  u32 vtx_buffer_offset = 0, index_buffer_offset = 0;
  for (i32 n = 0; n < counts; n++) {
    const ImDrawList *cmd_list = draw_data->CmdLists[n];

    for (i32 cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
      const ImDrawCmd *pcmd = &cmd_list->CmdBuffer[cmd_i];
      if (pcmd->UserCallback) {
        // User callback (registered via ImDrawList::AddCallback)
        pcmd->UserCallback(cmd_list, pcmd);
      } else {
        // Project scissor/clipping rectangles into framebuffer space
        ImVec4 clip_rect;
        clip_rect.x = (pcmd->ClipRect.x - clip_off.x) * clip_scale.x;
        clip_rect.y = (pcmd->ClipRect.y - clip_off.y) * clip_scale.y;
        clip_rect.z = (pcmd->ClipRect.z - clip_off.x) * clip_scale.x;
        clip_rect.w = (pcmd->ClipRect.w - clip_off.y) * clip_scale.y;

        if (clip_rect.x < fb_width && clip_rect.y < fb_height &&
            clip_rect.z >= 0.0f && clip_rect.w >= 0.0f) {
          // Apply scissor/clipping rectangle
          f32 offsets[2] = {clip_rect.x, clip_rect.y};
          f32 extents[2] = {(clip_rect.z - clip_rect.x),
                            (clip_rect.w - clip_rect.y)};
          rf->graphics_context->set_scissor(offsets[0], offsets[1], extents[0],
                                            extents[1]);

          // Retrieve
          TextureHandle new_texture = *(TextureHandle *)(pcmd->TextureId);
          rf->graphics_context->draw_indexed(
              pcmd->ElemCount, 1, index_buffer_offset + pcmd->IdxOffset,
              vtx_buffer_offset + pcmd->VtxOffset, new_texture.index);
        }
      }
    }
    index_buffer_offset += cmd_list->IdxBuffer.Size;
    vtx_buffer_offset += cmd_list->VtxBuffer.Size;
  }
}

} // namespace hlx
