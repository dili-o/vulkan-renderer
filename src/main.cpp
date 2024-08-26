
#include "Application/Window.hpp"
#include "Application/Input.hpp"
#include "Application/Keys.hpp"


#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <vendor/glm/glm/glm.hpp>
#include <vendor/glm/glm/gtc/matrix_transform.hpp>
#include <vendor/glm/glm/gtx/hash.hpp>



#include "vendor/imgui/imgui.h"

#include "vendor/tracy/tracy/Tracy.hpp"

#include "Renderer/GPUDevice.hpp"
#include "Renderer/CommandBuffer.hpp"
#include "Renderer/Renderer.hpp"
#include "Renderer/HelixImgui.hpp"
#include "Renderer/GPUProfiler.hpp"
#include "Renderer/AsynchronousLoader.hpp"
#include "Renderer/Scene.hpp"

#include "Core/File.hpp"
#include "Core/Gltf.hpp"
#include "Core/Numerics.hpp"
#include "Core/ResourceManager.hpp"
#include "Core/Time.hpp"

#include <stdlib.h> // for exit()



f32 rx, ry;

enum MaterialFeatures {
    MaterialFeatures_ColorTexture = 1 << 0,
    MaterialFeatures_NormalTexture = 1 << 1,
    MaterialFeatures_RoughnessTexture = 1 << 2,
    MaterialFeatures_OcclusionTexture = 1 << 3,
    MaterialFeatures_EmissiveTexture = 1 << 4,

    MaterialFeatures_TangentVertexAttribute = 1 << 5,
    MaterialFeatures_TexcoordVertexAttribute = 1 << 6,
};

static void input_os_messages_callback(void* os_event, void* user_data) {
    Helix::InputService* input = (Helix::InputService*)user_data;
    input->on_event(os_event);
}

// [TAG: MULTITHREADING]
// IOTasks ////////////////////////////////////////////////////////////////
//
//
struct RunPinnedTaskLoopTask : enki::IPinnedTask {

    void Execute() override {
        while (task_scheduler->GetIsRunning() && execute) {
            task_scheduler->WaitForNewPinnedTasks(); // this thread will 'sleep' until there are new pinned tasks
            task_scheduler->RunPinnedTasks();
        }
    }

    enki::TaskScheduler*    task_scheduler;
    bool                    execute = true;
}; // struct RunPinnedTaskLoopTask

struct AsynchronousLoadTask : enki::IPinnedTask {

    void Execute() override {
        // Do file IO
        while (execute) {
            async_loader->update(nullptr);
        }
    }

    Helix::AsynchronousLoader*     async_loader;
    enki::TaskScheduler*    task_scheduler;
    bool                    execute = true;
}; // struct AsynchronousLoadTask

int main(int argc, char** argv)
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
	if (argc < 2) {
		printf("No default model was given, changing [path to glTF model]\n");
		InjectDefault3DModel();
	}

	using namespace Helix;
	// Init services
	LogService log{};
	log.init();
    Time::service_init();

    MemoryServiceConfiguration memory_configuration;
    memory_configuration.maximum_dynamic_size = hmega(500);

	MemoryService::instance()->init(&memory_configuration);
	Allocator* allocator = &MemoryService::instance()->system_allocator;

	StackAllocator scratch_allocator;
	scratch_allocator.init(hmega(8));

    // [TAG: MULTITHREADING]
    enki::TaskSchedulerConfig config;
    config.numTaskThreadsToCreate = 4;
    enki::TaskScheduler task_scheduler;
    task_scheduler.Initialize(config);

	// window
	WindowConfiguration wconf{ 1280, 720, "Engine Test", allocator };
	Window window;
	window.init(&wconf);

	InputService input_handler;
	input_handler.init(allocator);

	// Callback register
	window.register_os_messages_callback(input_os_messages_callback, &input_handler);

	// graphics
	DeviceCreation dc;
	dc.set_window(window.width, window.height, window.platform_handle)
        .set_allocator(allocator)
        .set_num_threads(task_scheduler.GetNumTaskThreads())
        .set_linear_allocator(&scratch_allocator);
	GpuDevice gpu;
	gpu.init(dc);

	ResourceManager rm;
	rm.init(allocator, nullptr);

	GPUProfiler gpu_profiler;
	gpu_profiler.init(allocator, 100);

	Renderer renderer;
	renderer.init({ &gpu, allocator });
	//renderer.set_loaders(&rm);

	ImGuiService* imgui = ImGuiService::instance();
	ImGuiServiceConfiguration imgui_config{ &gpu, window.platform_handle };
	imgui->init(&imgui_config);

    // [TAG: Multithreading]
    AsynchronousLoader async_loader;
    async_loader.init(&renderer, &task_scheduler, allocator);

	Directory cwd{ };
	directory_current(&cwd);

	char gltf_base_path[512]{ };
	memcpy(gltf_base_path, argv[1], strlen(argv[1]));
	file_directory_from_path(gltf_base_path);

	directory_change(gltf_base_path);

	char gltf_file[512]{ };
	memcpy(gltf_file, argv[1], strlen(argv[1]));
	file_name_from_path(gltf_file);

    // TODO: Implement obj loading.
    glTFScene* scene = nullptr;
    scene = new glTFScene;

    scene->load(gltf_file, gltf_base_path, allocator, &scratch_allocator, &async_loader);

    directory_change(cwd.path);

    scene->prepare_draws(&renderer, &scratch_allocator);

    directory_change(cwd.path);

    // [TAG: MULTITHREADING]
    // Create IO threads at the end
    RunPinnedTaskLoopTask run_pinned_task;
    run_pinned_task.threadNum = task_scheduler.GetNumTaskThreads() - 1;
    run_pinned_task.task_scheduler = &task_scheduler;
    task_scheduler.AddPinnedTask(&run_pinned_task);

    // Send async load task to external thread FILE_IO
    AsynchronousLoadTask async_load_task;
    async_load_task.threadNum = run_pinned_task.threadNum;
    async_load_task.task_scheduler = &task_scheduler;
    async_load_task.async_loader = &async_loader;
    task_scheduler.AddPinnedTask(&async_load_task);

    i64 begin_frame_tick = Time::now();
    i64 absolute_begin_frame_tick = begin_frame_tick;

    glm::vec3 eye = { 0.0f, 2.5f, 2.0f };
    glm::vec3 look = { 0.0f, 0.0, -1.0f };
    glm::vec3 right = { 1.0f, 0.0, 0.0f };
    glm::vec3 up = { 0.0f, 1.0f, 0.0f };

    f32 yaw = 0.0f;
    f32 pitch = 0.0f;

    float model_scale = 1.0f;
    float light_range = 20.0f;
    float light_intensity = 1000.f;

    int frame_count = 0;
    double last_time = 0.0;

    float aspect_ratio = gpu.swapchain_width * 1.0f / gpu.swapchain_height;

    while (!window.requested_exit) {
        ZoneScoped;
        window.handle_os_messages();
        input_handler.new_frame();

        const i64 current_tick = Time::now();
        

        // New frame
        if (!window.minimized) {
            gpu.new_frame();
            static bool checksz = true;
            if (async_loader.file_load_requests.size == 0 && checksz) {
                checksz = false;
                HINFO("Finished uploading textures in {} seconds", Time::from_seconds(absolute_begin_frame_tick));
            }
            if (window.resized) {
                gpu.resize(window.width, window.height);
                window.resized = false;
            }
            // This MUST be AFTER os messages!
            imgui->new_frame();

            f32 delta_time = (f32)Time::delta_seconds(begin_frame_tick, current_tick);
            begin_frame_tick = current_tick;

            input_handler.update(delta_time);

            {
                ZoneScopedN("ImGui Recording");

                ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());
              /*  if (ImGui::Begin("Scene")) {
                    ImGui::InputFloat("Model scale", &model_scale, 0.001f);
                    ImGui::SliderFloat3("Light position", light_position.raw, -30.f, 30.f);
                    ImGui::InputFloat3("Camera position", eye.raw);
                    ImGui::InputFloat("Light range", &light_range);
                    ImGui::InputFloat("Light intensity", &light_intensity);
                }
                ImGui::End();*/

                if (ImGui::Begin("Viewport")) {
                    ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
                    ImGui::Image((ImTextureID)&gpu.fullscreen_texture_handle, { viewportPanelSize.x, viewportPanelSize.y });
                    aspect_ratio = viewportPanelSize.x * 1.0f / viewportPanelSize.y;
                }
                ImGui::End();

                if (ImGui::Begin("GPU")) {
                    renderer.imgui_draw();

                    ImGui::Separator();
                    gpu_profiler.imgui_draw();
                }
                ImGui::End();

                scene->imgui_draw_hierarchy();

                renderer.imgui_resources_draw();

                scene->imgui_draw_node_property(scene->current_node);

                //MemoryService::instance()->imgui_draw();
            }

            {
                MapBufferParameters cb_map = { scene->scene_buffer, 0, 0 };
                float* cb_data = (float*)gpu.map_buffer(cb_map);
                MapBufferParameters light_cb_map = { scene->light_cb, 0, 0 };
                float* light_cb_data = (float*)gpu.map_buffer(light_cb_map);
                if (cb_data) {
                    if (input_handler.is_mouse_down(MouseButtons::MOUSE_BUTTONS_RIGHT)) {
                        pitch += (input_handler.mouse_position.y - input_handler.previous_mouse_position.y) * 0.1f;
                        yaw += (input_handler.mouse_position.x - input_handler.previous_mouse_position.x) * 0.3f;

                        pitch = clamp(pitch, -60.0f, 60.0f);

                        if (yaw > 360.0f) {
                            yaw -= 360.0f;
                        }

                        glm::mat3 rxm = glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(-pitch), { 1.0f, 0.0f, 0.0f }));
                        glm::mat3 rym = glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(-yaw), { 0.0f, 1.0f, 0.0f }));

                        look = rxm * glm::vec3( 0.0f, 0.0f, -1.0f );
                        look = rym * look;

                        right = glm::cross(look, glm::vec3( 0.0f, 1.0f, 0.0f ));
                    }

                    if (input_handler.is_key_down(Keys::KEY_W)) {
                        eye += look * (5.0f * delta_time);
                    }
                    else if (input_handler.is_key_down(Keys::KEY_S)) {
                        eye -= look * (5.0f * delta_time);
                    }

                    if (input_handler.is_key_down(Keys::KEY_D)) {
                        eye += right * (5.0f * delta_time);
                    }
                    else if (input_handler.is_key_down(Keys::KEY_A)) {
                        eye -= right * (5.0f * delta_time);
                    }
                    if (input_handler.is_key_down(Keys::KEY_E)) {
                        eye += up * (5.0f * delta_time);
                    }
                    else if (input_handler.is_key_down(Keys::KEY_Q)) {
                        eye -= up * (5.0f * delta_time);
                    }

                    glm::mat4 view = glm::lookAt(eye, (eye + look), glm::vec3( 0.0f, 1.0f, 0.0f ));
                    glm::mat4 projection = glm::perspective(glm::radians(60.0f), aspect_ratio, 0.01f, 1000.0f);

                    // Calculate view projection matrix
                    glm::mat4 view_projection = projection * view;

                    // Rotate cube:
                    rx += 1.0f * delta_time;
                    ry += 2.0f * delta_time;

                    glm::mat4 rxm = glm::rotate(glm::mat4(1.0f), rx, glm::vec3(1.0f, 0.0f, 0.0f));
                    glm::mat4 rym = glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), glm::vec3( 0.0f, 1.0f, 0.0f ));

                    glm::mat4 sm = glm::scale(glm::mat4(1.0f), glm::vec3( model_scale));

                    UniformData uniform_data{ };
                    uniform_data.view_projection = view_projection;
                    uniform_data.camera_position = glm::vec4(eye.x, eye.y, eye.z, 1.0f);
                    uniform_data.light_intensity = light_intensity;
                    uniform_data.light_range = light_range;

                    LightNode* light_node = (LightNode*)scene->node_pool.access_node(scene->node_pool.root_nodes[0]);

                    uniform_data.light_position = glm::vec4(light_node->world_transform.translation.x, light_node->world_transform.translation.y, light_node->world_transform.translation.z, 1.0f);

                    memcpy(cb_data, &uniform_data, sizeof(UniformData));

                    gpu.unmap_buffer(cb_map);

                    LightUniform light_uniform_data{ };
                    light_uniform_data.view_projection = view_projection;
                    light_uniform_data.camera_position = glm::vec4(eye.x, eye.y, eye.z, 1.0f);
                    light_uniform_data.texture_index = scene->light_texture.handle.index;

                    glm::mat4 model = glm::mat4(1.0f);
                    model = glm::translate(model, light_node->world_transform.translation);
                    light_uniform_data.model = model;

                    memcpy(light_cb_data, &light_uniform_data, sizeof(LightUniform));

                    gpu.unmap_buffer(light_cb_map);
                }
                scene->upload_materials(model_scale);
            }
            scene->submit_draw_task(imgui, &gpu_profiler, &task_scheduler);

            gpu.present();

            f64 current_time = Time::from_seconds(absolute_begin_frame_tick);
            frame_count++;
            if (current_time - last_time >= 1.0) {
                renderer.fps = (f64)frame_count / (current_time - last_time);

                // Reset for the next second
                last_time = current_time;
                frame_count = 0;
            }
        }
    }

    run_pinned_task.execute = false;
    async_load_task.execute = false;

    task_scheduler.WaitforAllAndShutdown();

    vkDeviceWaitIdle(gpu.vulkan_device);

    async_loader.shutdown();

    gpu.destroy_buffer(scene->scene_buffer);
    gpu.destroy_buffer(scene->light_cb);

    imgui->shutdown();

    gpu_profiler.shutdown();

    scene->free_gpu_resources(&renderer);

    rm.shutdown();
    renderer.shutdown();

    scene->unload(&renderer);
    delete scene;

    input_handler.shutdown();
    window.unregister_os_messages_callback(input_os_messages_callback);
    window.shutdown();

    scratch_allocator.shutdown();
    MemoryService::instance()->shutdown();

    return 0;
}

