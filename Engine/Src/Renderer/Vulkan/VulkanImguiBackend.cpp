#include "VulkanImguiBackend.hpp"
#include "Core/Memory.hpp"
#include "Core/Profiler.hpp"
#include "Renderer/GPUResourceTypes.hpp"
#include "Renderer/GPUResources.hpp"
#include "Renderer/RendererFrontEnd.hpp"
#include "Renderer/Vulkan/CommandBuffer.hpp"
#include "Renderer/Vulkan/VulkanBackend.hpp"
#include "Renderer/Vulkan/VulkanTypes.hpp"
// Vendor
#define IMGUI_IMPL_VULKAN_USE_VOLK
#include <imgui/backends/imgui_impl_sdl3.h>
#include <imgui/backends/imgui_impl_vulkan.h>

namespace Helix {
static uint32_t s_vb_size = 665536, s_ib_size = 665536;

void VulkanImguiBackend::init(void *configuration) {
  ImguiLayerConfiguration *config = (ImguiLayerConfiguration *)configuration;
  frontend = config->frontend;
  backend = (VulkanBackend *)frontend->backend;

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();

  ImGui_ImplSDL3_InitForVulkan((SDL_Window *)config->window_handle);

  ImGuiIO &io = ImGui::GetIO();
  io.BackendRendererName = "Helix";
  io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
  // io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Enable Docking

  // Load font texture atlas //////////////////////////////////////////////////
  unsigned char *pixels;
  int width, height;
  // Load as RGBA 32-bits (75% of the memory is wasted, but default font is so
  // small) because it is more likely to be compatible with user's existing
  // shaders. If your ImTextureId represent a higher-level concept than just a
  // GL texture id, consider calling GetTexDataAsAlpha8() instead to save on GPU
  // memory.
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

  TextureCreation texture_creation{};
  texture_creation.initial_data = pixels;
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
  texture_creation.name = "ImGui_Font";

  font_texture = backend->create_texture(texture_creation);

  io.Fonts->TexID = (ImTextureID)&font_texture;

  // Create vertex and index buffers //////////////////////////////////////////
  // u32 buffer_size = 665536;

  for (u32 i = 0; i < FRAMES_IN_FLIGHT; ++i) {

    BufferCreation creation{};
    creation.reset();
    creation.usage_flags = (BufferUsage::Enum)(BufferUsage::Vertex);
    creation.memory_state_flags = MemoryState::Persistent;
    creation.memory_access_flags = MemoryAccess::CPU_TO_GPU;
    creation.size = s_vb_size;
    creation.name = "ImGui_Vertex_Buffer";

    vertex_buffers[i] = backend->create_buffer(creation);

    creation.reset();
    creation.usage_flags = (BufferUsage::Enum)(BufferUsage::Index);
    creation.memory_state_flags = MemoryState::Persistent;
    creation.memory_access_flags = MemoryAccess::CPU_TO_GPU;
    creation.size = s_ib_size;
    creation.name = "ImGui_Index_Buffer";

    index_buffers[i] = backend->create_buffer(creation);
  }

  // Create Pipeline /////////////////////////////////////////////////////////
  StackAllocator *stack_allocator = &MemoryService::instance()->stack_allocator;
  PipelineCreation pipeline_creation;
  pipeline_creation.name = "ImGui_Pipeline";
  pipeline_creation.shader_create_infos = (ShaderCreateInfo *)halloca(
      sizeof(ShaderCreateInfo) * 2, stack_allocator);
  pipeline_creation.shader_create_infos[0] = {"imgui_bindless.vert",
                                              ShaderStage::Vertex};
  pipeline_creation.shader_create_infos[1] = {"imgui_bindless.frag",
                                              ShaderStage::Fragment};
  pipeline_creation.shader_count = 2;
  pipeline_creation.pipeline_type = PipelineType::Graphics;
  pipeline_creation.cull_mode = CullMode::None;
  pipeline_creation.set_layouts[0] =
      RendererFrontEnd::instance()->bindless_set_layout;
  pipeline_creation.set_layout_count = 1;

  pipeline = backend->create_pipeline(pipeline_creation);

  RendererFrontEnd::instance()->set_pipeline_binding_set(
      pipeline, RendererFrontEnd::instance()->bindless_set, 0);
}

void VulkanImguiBackend::shutdown() {
  for (u32 i = 0; i < FRAMES_IN_FLIGHT; ++i) {
    backend->destroy_buffer(vertex_buffers[i]);
    backend->destroy_buffer(index_buffers[i]);
  }
  backend->destroy_pipeline(pipeline);
  backend->destroy_texture(font_texture);

  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
}

void VulkanImguiBackend::begin_frame() {
  HELIX_PROFILER_FUNCTION();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
}

void VulkanImguiBackend::render_frame(RenderPacket *packet) {
  HELIX_PROFILER_FUNCTION();
  ImGui::Render();
  ImDrawData *draw_data = ImGui::GetDrawData();

  // Avoid rendering when minimized, scale coordinates for retina displays
  // (screen coordinates != framebuffer coordinates)
  int fb_width =
      (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
  int fb_height =
      (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
  if (fb_width <= 0 || fb_height <= 0)
    return;

  size_t vertex_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
  size_t index_size = draw_data->TotalIdxCount * sizeof(ImDrawIdx);

  if (vertex_size >= s_vb_size || index_size >= s_ib_size) {
    HERROR("ImGui Backend Error: vertex/index overflow!");
    return;
  }

  // TODO: Make a funtion to get command buffers from the backend
  // NOTE: Command buffer should already be recording
  VulkanCommandBuffer *command_buffer =
      backend->command_buffer_manager.get_command_buffer(packet->current_frame,
                                                         0, false);

  if (vertex_size == 0 && index_size == 0) {
    return;
  }

  // Upload vertex and index data
  ImDrawVert *vtx_dst = NULL;
  ImDrawIdx *idx_dst = NULL;

  VulkanBuffer *vtx_buf =
      backend->access_buffer(vertex_buffers[packet->current_frame]);
  vtx_dst = (ImDrawVert *)vtx_buf->mapped_data;

  if (vtx_dst) {
    for (int n = 0; n < draw_data->CmdListsCount; n++) {
      const ImDrawList *cmd_list = draw_data->CmdLists[n];
      memcpy(vtx_dst, cmd_list->VtxBuffer.Data,
             cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
      vtx_dst += cmd_list->VtxBuffer.Size;
    }
  }

  VulkanBuffer *idx_buf =
      backend->access_buffer(index_buffers[packet->current_frame]);
  idx_dst = (ImDrawIdx *)idx_buf->mapped_data;

  if (idx_dst) {
    for (int n = 0; n < draw_data->CmdListsCount; n++) {
      const ImDrawList *cmd_list = draw_data->CmdLists[n];
      memcpy(idx_dst, cmd_list->IdxBuffer.Data,
             cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
      idx_dst += cmd_list->IdxBuffer.Size;
    }
  }

  command_buffer->push_marker("ImGui");

  command_buffer->bind_pipeline(pipeline);
  command_buffer->bind_vertex_buffer(vertex_buffers[packet->current_frame], 0,
                                     1);
  command_buffer->bind_index_buffer(index_buffers[packet->current_frame], 0,
                                    VK_INDEX_TYPE_UINT16);

  VkExtent2D extents{(u32)fb_width, (u32)fb_height};

  command_buffer->bind_viewport(extents);

  VkDescriptorSet vk_bindless_descriptor_set =
      backend->access_descriptor_set(RendererFrontEnd::instance()->bindless_set)
          ->vk_handle;
  command_buffer->bind_descriptor_sets(pipeline, vk_bindless_descriptor_set, 0);

  // Setup push constants
  VulkanPipeline *vulkan_pipeline = backend->access_pipeline(pipeline);

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

  command_buffer->push_constants(
      vulkan_pipeline->vk_layout, VK_SHADER_STAGE_ALL, 0,
      sizeof(f32) * 4, // TODO: Fix All shader stage flag
      (void *)uniform);

  // Will project scissor/clipping rectangles into framebuffer space
  ImVec2 clip_off = draw_data->DisplayPos; // (0,0) unless using multi-viewports
  ImVec2 clip_scale =
      draw_data->FramebufferScale; // (1,1) unless using retina display which
                                   // are often (2,2)

  // Render command lists
  int counts = draw_data->CmdListsCount;

  u32 vtx_buffer_offset = 0, index_buffer_offset = 0;
  for (int n = 0; n < counts; n++) {
    const ImDrawList *cmd_list = draw_data->CmdLists[n];

    for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
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
          VkRect2D rect{};
          rect.offset = {(i32)clip_rect.x, (i32)clip_rect.y};
          rect.extent = {(u32)(clip_rect.z - clip_rect.x),
                         (u32)(clip_rect.w - clip_rect.y)};
          command_buffer->bind_scissors(rect);

          // Retrieve
          TextureHandle new_texture = *(TextureHandle *)(pcmd->TextureId);

          command_buffer->draw_indexed(
              pcmd->ElemCount, 1, index_buffer_offset + pcmd->IdxOffset,
              vtx_buffer_offset + pcmd->VtxOffset, new_texture.index);
        }
      }
    }
    index_buffer_offset += cmd_list->IdxBuffer.Size;
    vtx_buffer_offset += cmd_list->VtxBuffer.Size;
  }

  command_buffer->pop_marker();
}

} // namespace Helix
