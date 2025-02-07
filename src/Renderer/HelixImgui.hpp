#pragma once

#include <stdint.h>

#include "Core/Service.hpp"

struct ImDrawData;

namespace Helix {

struct GpuDevice;
struct CommandBuffer;
struct TextureHandle;

//
//
enum ImGuiStyles {
  Default = 0,
  GreenBlue,
  DarkRed,
  DarkGold
};  // enum ImGuiStyles

//
//
struct ImGuiServiceConfiguration {
  GpuDevice* gpu;
  void* window_handle;

};  // struct ImGuiServiceConfiguration

//
//
struct ImGuiService : public Helix::Service {
  HELIX_DECLARE_SERVICE(ImGuiService);

  void init(void* configuration) override;
  void shutdown() override;

  void new_frame();
  void render(CommandBuffer& commands, bool use_secondary);

  // Removes the Texture from the Cache and destroy the associated Descriptor
  // Set.
  void remove_cached_texture(TextureHandle& texture);

  void set_style(ImGuiStyles style);

  GpuDevice* gpu;

  static constexpr cstring k_name = "helix_imgui_service";

};  // ImGuiService

// File Dialog /////////////////////////////////////////////////////////

/*bool                                imgui_file_dialog_open( cstring
button_name, cstring path, cstring extension ); cstring
imgui_file_dialog_get_filename();

bool                                imgui_path_dialog_open( cstring button_name,
cstring path ); cstring                         imgui_path_dialog_get_path();*/

// Application Log /////////////////////////////////////////////////////

void imgui_log_init();
void imgui_log_shutdown();

void imgui_log_draw();

// FPS graph ///////////////////////////////////////////////////
void imgui_fps_init();
void imgui_fps_shutdown();
void imgui_fps_add(f32 dt);
void imgui_fps_draw();

}  // namespace Helix
