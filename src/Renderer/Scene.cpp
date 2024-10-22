#include "Scene.hpp"

#include <vendor/imgui/stb_image.h>

#include "Core/Gltf.hpp"
#include "Core/Time.hpp"
#include "Core/File.hpp"
#include <vendor/tracy/tracy/Tracy.hpp>
#include <imgui/imgui.h>

namespace Helix {

    // Node
    static void update_transform(Node* node, NodePool* node_pool) {

        if (node->parent.index != k_invalid_index) {
            Node* parent_node = (Node*)node_pool->access_node(node->parent);
            glm::mat4 combined_matrix = parent_node->world_transform.calculate_matrix() * node->local_transform.calculate_matrix();
            node->world_transform.set_transform(combined_matrix);
        }
        else {
            node->world_transform.set_transform(node->local_transform.calculate_matrix());
        }

        for (u32 i = 0; i < node->children.size; i++) {
            Node* child_node = (Node*)node_pool->access_node(node->children[i]);
            child_node->update_transform(node_pool);
        }
    }

    static void update_mesh_transform(Node* node, NodePool* node_pool) {
        update_transform(node, node_pool);

        MeshNode* mesh_node = static_cast<MeshNode*>(node);
        //mesh_node->gpu_mesh_data->model = node->world_transform.calculate_matrix();
    }

    // Helper functions //////////////////////////////////////////////////

    // Light
    Helix::PipelineHandle                  light_pipeline;

    
    void get_mesh_vertex_buffer(glTFScene& scene, i32 accessor_index, BufferHandle& out_buffer_handle, u32& out_buffer_offset) {

        if (accessor_index != -1) {
            glTF::Accessor& buffer_accessor = scene.gltf_scene.accessors[accessor_index];
            glTF::BufferView& buffer_view = scene.gltf_scene.buffer_views[buffer_accessor.buffer_view];
            BufferResource& buffer_gpu = scene.buffers[buffer_accessor.buffer_view + scene.current_buffers_count];

            
            out_buffer_handle = buffer_gpu.handle;
            out_buffer_offset = buffer_accessor.byte_offset == glTF::INVALID_INT_VALUE ? 0 : buffer_accessor.byte_offset;
        }
        else {
            // TODO: Right now if a glTF model doesn't have a vertex buffer we just bind the 0 index buffer
            out_buffer_handle.index = 0;
        }
    }

    int gltf_mesh_material_compare(const void* a, const void* b) {
        const Mesh* mesh_a = (const Mesh*)a;
        const Mesh* mesh_b = (const Mesh*)b;

        if (mesh_a->pbr_material.material->render_index < mesh_b->pbr_material.material->render_index) return -1;
        if (mesh_a->pbr_material.material->render_index > mesh_b->pbr_material.material->render_index) return  1;
        return 0;
    }

    static void copy_gpu_material_data(GPUMeshData& gpu_mesh_data, const Mesh& mesh) {
        gpu_mesh_data.textures[0] = mesh.pbr_material.diffuse_texture_index;
        gpu_mesh_data.textures[1] = mesh.pbr_material.roughness_texture_index;
        gpu_mesh_data.textures[2] = mesh.pbr_material.normal_texture_index;
        gpu_mesh_data.textures[3] = mesh.pbr_material.occlusion_texture_index;
        gpu_mesh_data.base_color_factor = mesh.pbr_material.base_color_factor;
        gpu_mesh_data.metallic_roughness_occlusion_factor = mesh.pbr_material.metallic_roughness_occlusion_factor;
        gpu_mesh_data.alpha_cutoff = mesh.pbr_material.alpha_cutoff;
        gpu_mesh_data.flags = mesh.pbr_material.flags;
    }

    //
    //
    static void copy_gpu_mesh_matrix(GPUMeshData& gpu_mesh_data, const Mesh& mesh, const f32 global_scale, const ResourcePool* mesh_nodes) {

        // Apply global scale matrix
        // NOTE: for left-handed systems (as defined in cglm) need to invert positive and negative Z.
        glm::mat4 scale_mat = glm::scale(glm::mat4(1.0f), glm::vec3(-global_scale, global_scale, -global_scale));
        MeshNode* mesh_node = (MeshNode*)mesh_nodes->access_resource(mesh.node_index);
        gpu_mesh_data.model = mesh_node->world_transform.calculate_matrix() * scale_mat;
        gpu_mesh_data.inverse_model = glm::inverse(glm::transpose(gpu_mesh_data.model));
    }

    //
    // DepthPrePass ///////////////////////////////////////////////////////
    void DepthPrePass::render(CommandBuffer* gpu_commands, Scene* render_scene) {
        glTFScene* scene = (glTFScene*)render_scene;

        Material* last_material = nullptr;
        for (u32 mesh_index = 0; mesh_index < mesh_count; ++mesh_index) {
            //MeshInstance& mesh_instance = mesh_instances[mesh_index];
            Mesh& mesh = meshes[mesh_index];

            if (mesh.pbr_material.material != last_material) {
                PipelineHandle pipeline = renderer->get_pipeline(mesh.pbr_material.material, 0);

                gpu_commands->bind_pipeline(pipeline);

                last_material = mesh.pbr_material.material;
            }

            scene->draw_mesh(gpu_commands, mesh);
        }
    }

    void DepthPrePass::init(){
        renderer = nullptr;
        //mesh_instances.size = 0;
        meshes = nullptr;
        mesh_count = 0;
    }

    void DepthPrePass::prepare_draws(glTFScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator) {
        
        renderer = scene.renderer;

        FrameGraphNode* node = frame_graph->get_node("depth_pre_pass");
        HASSERT(node);

        //const u64 hashed_name = hash_calculate("geometry");
        //Program* geometry_program = renderer->resource_cache.programs.get(hashed_name);

        //glTF::glTF& gltf_scene = scene.gltf_scene;

        //mesh_instances.init(resident_allocator, 16);

        // Copy all mesh draws and change only material.
        if (scene.opaque_meshes.size) {
            meshes = &scene.opaque_meshes[0];
            mesh_count = scene.opaque_meshes.size;
        }
    }

    void DepthPrePass::free_gpu_resources() {
        //mesh_instances.shutdown();
    }


    //
    // GBufferPass ////////////////////////////////////////////////////////
    void GBufferPass::render(CommandBuffer* gpu_commands, Scene* render_scene) {
        glTFScene* scene = (glTFScene*)render_scene;

        Material* last_material = nullptr;
        for (u32 mesh_index = 0; mesh_index < mesh_count; ++mesh_index) {
            //MeshInstance& mesh_instance = mesh_instances[mesh_index];
            Mesh& mesh = meshes[mesh_index];

            if (mesh.pbr_material.material != last_material) {
                // TODO: Right now all transparent objects are drawn using the 2nd pipeline (no_cull) in the program.
                //       Make more configurable
                PipelineHandle pipeline = renderer->get_pipeline(mesh.pbr_material.material, 2);

                gpu_commands->bind_pipeline(pipeline);

                last_material = mesh.pbr_material.material;
            }

            scene->draw_mesh(gpu_commands, mesh);
        }
    }

    void GBufferPass::init(){
        renderer = nullptr;
        //mesh_instances.size = 0;
        mesh_count = 0;
        meshes = nullptr;
    }

    void GBufferPass::prepare_draws(glTFScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator) {
        renderer = scene.renderer;

        FrameGraphNode* node = frame_graph->get_node("gbuffer_pass");
        HASSERT(node);

        //const u64 hashed_name = hash_calculate("geometry");
        //Program* geometry_program = renderer->resource_cache.programs.get(hashed_name);

        //glTF::glTF& gltf_scene = scene.gltf_scene;

        //mesh_instances.init(resident_allocator, 16);

        // Copy all mesh draws and change only material.
        if (scene.opaque_meshes.size) {
            meshes = &scene.opaque_meshes[0];
            mesh_count = scene.opaque_meshes.size;
        }
    }

    void GBufferPass::free_gpu_resources() {
        //mesh_instances.shutdown();
    }

    //
    // LightPass //////////////////////////////////////////////////////////
    void LightPass::render(CommandBuffer* gpu_commands, Scene* render_scene) {
        glTFScene* scene = (glTFScene*)render_scene;

        if (renderer) {
            PipelineHandle pipeline = renderer->get_pipeline(mesh.pbr_material.material, 0);

            gpu_commands->bind_pipeline(pipeline);
            gpu_commands->bind_vertex_buffer(mesh.position_buffer, 0, 0);
            gpu_commands->bind_descriptor_set(&mesh.pbr_material.descriptor_set, 1, nullptr, 0);

            gpu_commands->draw(TopologyType::Triangle, 0, 3, 0, 1);
        }
    }

    void LightPass::init(){
        renderer = nullptr;
    }

    void LightPass::prepare_draws(glTFScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator) {
        if (renderer)
            return;
        renderer = scene.renderer;

        FrameGraphNode* node = frame_graph->get_node("lighting_pass");
        HASSERT(node);

        const u64 hashed_name = hash_calculate("pbr_lighting");
        Program* lighting_program = renderer->resource_cache.programs.get(hashed_name);

        MaterialCreation material_creation;

        material_creation.set_name("material_pbr").set_program(lighting_program).set_render_index(0);
        Material* material_pbr = renderer->create_material(material_creation);

        BufferCreation buffer_creation;
        buffer_creation.reset().set(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof(GPUMeshData)).set_name("mesh_data");
        mesh.pbr_material.material_buffer = renderer->gpu->create_buffer(buffer_creation);

        DescriptorSetCreation ds_creation{};
        DescriptorSetLayoutHandle layout = renderer->gpu->get_descriptor_set_layout(lighting_program->passes[0].pipeline, k_material_descriptor_set_index);
        ds_creation.buffer(scene.local_constants_buffer, 0).buffer(mesh.pbr_material.material_buffer, 1).set_layout(layout);
        mesh.pbr_material.descriptor_set = renderer->gpu->create_descriptor_set(ds_creation);

        BufferHandle fs_vb = renderer->gpu->get_fullscreen_vertex_buffer();
        mesh.position_buffer = fs_vb;

        FrameGraphResource* color_texture = frame_graph->access_resource(node->inputs[0]);
        FrameGraphResource* normal_texture = frame_graph->access_resource(node->inputs[1]);
        FrameGraphResource* roughness_texture = frame_graph->access_resource(node->inputs[2]);
        FrameGraphResource* position_texture = frame_graph->access_resource(node->inputs[3]);

        mesh.pbr_material.diffuse_texture_index = color_texture->resource_info.texture.texture.index;
        mesh.pbr_material.normal_texture_index = normal_texture->resource_info.texture.texture.index;
        mesh.pbr_material.roughness_texture_index = roughness_texture->resource_info.texture.texture.index;
        mesh.pbr_material.occlusion_texture_index = position_texture->resource_info.texture.texture.index;
        mesh.pbr_material.material = material_pbr;
    }

    void LightPass::fill_gpu_material_buffer() {

        if(renderer){
            MapBufferParameters cb_map = { mesh.pbr_material.material_buffer, 0, 0 };
            GPUMeshData* mesh_data = (GPUMeshData*)renderer->gpu->map_buffer(cb_map);
            if (mesh_data) {
                copy_gpu_material_data(*mesh_data, mesh);
                renderer->gpu->unmap_buffer(cb_map);
            }
        }
    }

    void LightPass::free_gpu_resources() {
        if(renderer){
            GpuDevice& gpu = *renderer->gpu;

            gpu.destroy_buffer(mesh.pbr_material.material_buffer);
            gpu.destroy_descriptor_set(mesh.pbr_material.descriptor_set);
        }
    }

    //
    // TransparentPass ////////////////////////////////////////////////////////
    void TransparentPass::render(CommandBuffer* gpu_commands, Scene* render_scene) {
        glTFScene* scene = (glTFScene*)render_scene;

        Material* last_material = nullptr;
        for (u32 mesh_index = 0; mesh_index < mesh_count; ++mesh_index) {
            //MeshInstance& mesh_instance = mesh_instances[mesh_index];
            Mesh& mesh = meshes[mesh_index];

            if (mesh.pbr_material.material != last_material) {
                // TODO: Right now all transparent objects are drawn using the 4th pipeline in the program.
                //       Make more configurable
                PipelineHandle pipeline = renderer->get_pipeline(mesh.pbr_material.material, 3);

                gpu_commands->bind_pipeline(pipeline);

                last_material = mesh.pbr_material.material;
            }

            scene->draw_mesh(gpu_commands, mesh);
        }
    }

    void TransparentPass::init(){
        renderer = nullptr;
        meshes = nullptr;
        mesh_count = 0;
    }

    void TransparentPass::prepare_draws(glTFScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator) {
        renderer = scene.renderer;

        FrameGraphNode* node = frame_graph->get_node("transparent_pass");
        HASSERT(node);

        // Create pipeline state
        //PipelineCreation pipeline_creation;

        //const u64 hashed_name = hash_calculate("geometry");
        //Program* geometry_program = renderer->resource_cache.programs.get(hashed_name);

        //glTF::glTF& gltf_scene = scene.gltf_scene;

        //mesh_instances.init(resident_allocator, 16);

        // Copy all mesh draws and change only material.

        if(scene.transparent_meshes.size){
            meshes = &scene.transparent_meshes[0];
            mesh_count = scene.transparent_meshes.size;
        }
    }

    void TransparentPass::free_gpu_resources() {
        //mesh_instances.shutdown();
    }

    cstring node_type_to_cstring(NodeType type) {
        switch (type)
        {
        case Helix::NodeType::Node:
            return "Node";
            break;
        case Helix::NodeType::MeshNode:
            return "Mesh Node";
            break;
        case Helix::NodeType::LightNode:
            return "Light Node";
            break;
        default:
            HCRITICAL("Invalid node type");
            break;
        }
    }

    //
    // LightDebugPass ////////////////////////////////////////////////////////
    void LightDebugPass::render(CommandBuffer* gpu_commands, Scene* render_scene) {
        glTFScene* scene = (glTFScene*)render_scene;

        gpu_commands->bind_pipeline(light_pipeline);
        gpu_commands->bind_descriptor_set(&light_descriptor_set, 1, nullptr, 0);
        gpu_commands->draw(TopologyType::Triangle, 0, 3, 0, 1);
    }

    void LightDebugPass::init(){
        renderer = nullptr;
    }

    void LightDebugPass::prepare_draws(glTFScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator) {
        if (renderer)
            return;
        renderer = scene.renderer;

        FrameGraphNode* node = frame_graph->get_node("light_debug_pass");
        HASSERT(node);

        const u64 hashed_name = hash_calculate("light_debug");
        Program* light_debug_program = scene.renderer->resource_cache.programs.get(hashed_name);

        light_pipeline = light_debug_program->passes[0].pipeline;

        DescriptorSetCreation ds_creation{};
        DescriptorSetLayoutHandle layout = scene.renderer->gpu->get_descriptor_set_layout(light_debug_program->passes[0].pipeline, 1);
        ds_creation.buffer(scene.light_cb, 0).set_layout(layout);
        light_descriptor_set = scene.renderer->gpu->create_descriptor_set(ds_creation);
    }

    void LightDebugPass::free_gpu_resources() {
        renderer->gpu->destroy_descriptor_set(light_descriptor_set);
    }

    // glTFDrawTask //////////////////////////////////

    void glTFDrawTask::init(GpuDevice* gpu_, FrameGraph* frame_graph_, Renderer* renderer_, ImGuiService* imgui_, GPUProfiler* gpu_profiler_, glTFScene* scene_) {
        gpu = gpu_;
        frame_graph = frame_graph_;
        renderer = renderer_;
        imgui = imgui_;
        gpu_profiler = gpu_profiler_;
        scene = scene_;
    }

    void glTFDrawTask::ExecuteRange(enki::TaskSetPartition range_, uint32_t threadnum_) {
        ZoneScoped;

        thread_id = threadnum_;

        //HTRACE( "Executing draw task from thread {}", threadnum_ );
        // TODO: improve getting a command buffer/pool
        CommandBuffer* gpu_commands = gpu->get_command_buffer(threadnum_, true);

        frame_graph->render(gpu_commands, scene);


        gpu_commands->push_marker("Fullscreen");
        gpu_commands->clear(0.3f, 0.3f, 0.3f, 1.f);
        gpu_commands->clear_depth_stencil(1.0f, 0);
        gpu_commands->bind_pass(gpu->fullscreen_render_pass, gpu->fullscreen_framebuffer, false);
        gpu_commands->set_scissor(nullptr);
        Viewport viewport{};
        viewport.rect = { 0,0,gpu->swapchain_width, gpu->swapchain_height };
        viewport.max_depth = 1.0f;
        viewport.min_depth = 0.0f;

        gpu_commands->set_viewport(&viewport);
        gpu_commands->bind_pipeline(scene->fullscreen_program->passes[0].pipeline);
        gpu_commands->bind_descriptor_set(&scene->fullscreen_ds, 1, nullptr, 0);
        gpu_commands->draw(TopologyType::Triangle, 0, 3, scene->fullscreen_texture_index, 1);

        imgui->render(*gpu_commands, false);

        gpu_commands->pop_marker();
        gpu_commands->pop_marker();

        gpu_profiler->update(*gpu);

        // Send commands to GPU
        gpu->queue_command_buffer(gpu_commands);
    }

    // gltfScene //////////////////////////////////////////////////

    void glTFScene::init(Renderer* _renderer, Allocator* resident_allocator, FrameGraph* _frame_graph, StackAllocator* stack_allocator, AsynchronousLoader* async_loader){
        u32 k_num_meshes = 200;
        renderer = _renderer;
        frame_graph = _frame_graph;
        scratch_allocator = stack_allocator;
        main_allocator = resident_allocator;
        loader = async_loader;

        transparent_meshes.init(resident_allocator, k_num_meshes);
        opaque_meshes.init(resident_allocator, k_num_meshes);

        node_pool.init(resident_allocator);

        images.init(resident_allocator, k_num_meshes);
        samplers.init(resident_allocator, 1);
        buffers.init(resident_allocator, k_num_meshes);

        // Create material
        u64 hashed_name = hash_calculate("geometry");
        Program* geometry_program = renderer->resource_cache.programs.get(hashed_name);

        MaterialCreation material_creation;
        material_creation.set_name("material_no_cull_opaque").set_program(geometry_program).set_render_index(0);

        pbr_material = renderer->create_material(material_creation);

        names.init(4024, main_allocator);

        local_constants_buffer = k_invalid_buffer;

        // Creating the light image
        stbi_set_flip_vertically_on_load(true);
        TextureResource* tr = renderer->create_texture("Light", HELIX_TEXTURE_FOLDER"lights/point_light.png", true);
        stbi_set_flip_vertically_on_load(false);
        HASSERT(tr != nullptr);
        light_texture = *tr;

        hashed_name = hash_calculate("light_debug");
        Program* light_program = renderer->resource_cache.programs.get(hashed_name);

        light_pipeline = light_program->passes[0].pipeline;

        BufferCreation buffer_creation;
        buffer_creation.reset().set(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof(LightUniform)).set_name("light_uniform_buffer");
        light_cb = renderer->gpu->create_buffer(buffer_creation);

        NodeHandle light_node_handle = node_pool.obtain_node(NodeType::LightNode);

        LightNode* light_node = (LightNode*)node_pool.access_node(light_node_handle);
        light_node->name = "Point Light";
        light_node->local_transform.scale = { 1.0f, 1.0f, 1.0f };
        light_node->world_transform.scale = { 1.0f, 1.0f, 1.0f };
        light_node->world_transform.translation.y = 1.0f;

        node_pool.get_root_node()->add_child(light_node);

        // Constant buffer
        buffer_creation.reset().set(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof(UniformData)).set_name("local_constants_buffer");
        local_constants_buffer = renderer->gpu->create_buffer(buffer_creation);


        depth_pre_pass.init();
        gbuffer_pass.init();
        transparent_pass.init();
        light_pass.init();
        light_debug_pass.init();

        fullscreen_program = renderer->resource_cache.programs.get(hash_calculate("fullscreen"));

        DescriptorSetCreation dsc;
        DescriptorSetLayoutHandle descriptor_set_layout = renderer->gpu->get_descriptor_set_layout(fullscreen_program->passes[0].pipeline, k_material_descriptor_set_index);
        dsc.reset().buffer(local_constants_buffer, 0).set_layout(descriptor_set_layout);
        fullscreen_ds = renderer->gpu->create_descriptor_set(dsc);

        FrameGraphResource* texture = frame_graph->get_resource("final");
        if (texture != nullptr) {
            fullscreen_texture_index = texture->resource_info.texture.texture.index;
        }
    }

    void glTFScene::load(cstring filename, cstring path, Allocator* resident_allocator, StackAllocator* temp_allocator, AsynchronousLoader* async_loader) {


        //renderer = async_loader->renderer;
        sizet temp_allocator_initial_marker = temp_allocator->get_marker();

        // Time statistics
        i64 start_scene_loading = Time::now();

        gltf_scene = gltf_load_file(filename);

        i64 end_loading_file = Time::now();

        //node_pool.init(resident_allocator);


        // Load all textures
        //images.init(resident_allocator, gltf_scene.images_count);

        StringBuffer name_buffer;
        name_buffer.init(hkilo(100), temp_allocator);

        Array<FileLoadRequest> texture_requests;
        texture_requests.init(resident_allocator, gltf_scene.images_count, gltf_scene.images_count); // TODO: Maybe use stack allocator;

        for (u32 image_index = 0; image_index < gltf_scene.images_count; ++image_index) {
            glTF::Image& image = gltf_scene.images[image_index];

            int comp, width, height;

            stbi_info(image.uri.data, &width, &height, &comp);

            u32 mip_levels = 1;
            if (true) {
                u32 w = width;
                u32 h = height;

                while (w > 1 && h > 1) {
                    w /= 2;
                    h /= 2;

                    ++mip_levels;
                }
            }
            // Creates an empty texture resource.
            TextureCreation tc;
            tc.set_data(nullptr).set_format_type(VK_FORMAT_R8G8B8A8_UNORM, TextureType::Texture2D).set_flags(mip_levels, 0).set_size((u16)width, (u16)height, 1).set_name(image.uri.data);
            TextureResource* tr = renderer->create_texture(tc);
            HASSERT(tr != nullptr);

            images.push(*tr);

            // Reconstruct file path
            char* full_filename = name_buffer.append_use_f("%s%s", path, image.uri.data);

            FileLoadRequest& request = texture_requests[image_index];
            request.texture = tr->handle;
            strcpy(request.path, full_filename);

            //async_loader->request_texture_data(full_filename, tr->handle);
            // Reset name buffer
            name_buffer.clear();
        }

        async_loader->file_load_requests.push_array(texture_requests);

        texture_requests.shutdown();

        i64 end_loading_textures_files = Time::now();

        i64 end_creating_textures = Time::now();

        // Load all samplers
        //samplers.init(resident_allocator, gltf_scene.samplers_count);

        for (u32 sampler_index = 0; sampler_index < gltf_scene.samplers_count; ++sampler_index) {
            glTF::Sampler& sampler = gltf_scene.samplers[sampler_index];

            char* sampler_name = renderer->resource_name_buffer.append_use_f("sampler_%u", sampler_index + current_samplers_count);

            SamplerCreation creation;
            switch (sampler.min_filter) {
            case glTF::Sampler::NEAREST:
                creation.min_filter = VK_FILTER_NEAREST;
                break;
            case glTF::Sampler::LINEAR:
                creation.min_filter = VK_FILTER_LINEAR;
                break;
            case glTF::Sampler::LINEAR_MIPMAP_NEAREST:
                creation.min_filter = VK_FILTER_LINEAR;
                creation.mip_filter = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                break;
            case glTF::Sampler::LINEAR_MIPMAP_LINEAR:
                creation.min_filter = VK_FILTER_LINEAR;
                creation.mip_filter = VK_SAMPLER_MIPMAP_MODE_LINEAR;
                break;
            case glTF::Sampler::NEAREST_MIPMAP_NEAREST:
                creation.min_filter = VK_FILTER_NEAREST;
                creation.mip_filter = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                break;
            case glTF::Sampler::NEAREST_MIPMAP_LINEAR:
                creation.min_filter = VK_FILTER_NEAREST;
                creation.mip_filter = VK_SAMPLER_MIPMAP_MODE_LINEAR;
                break;
            }

            creation.mag_filter = sampler.mag_filter == glTF::Sampler::Filter::LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;

            switch (sampler.wrap_s) {
            case glTF::Sampler::CLAMP_TO_EDGE:
                creation.address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                break;
            case glTF::Sampler::MIRRORED_REPEAT:
                creation.address_mode_u = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
                break;
            case glTF::Sampler::REPEAT:
                creation.address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                break;
            }

            switch (sampler.wrap_t) {
            case glTF::Sampler::CLAMP_TO_EDGE:
                creation.address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                break;
            case glTF::Sampler::MIRRORED_REPEAT:
                creation.address_mode_v = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
                break;
            case glTF::Sampler::REPEAT:
                creation.address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                break;
            }

            creation.name = sampler_name;

            SamplerResource* sr = renderer->create_sampler(creation);
            HASSERT(sr != nullptr);

            samplers.push(*sr);
        }

        i64 end_creating_samplers = Time::now();

        // Temporary array of buffer data
        Array<void*> buffers_data;
        buffers_data.init(resident_allocator, gltf_scene.buffers_count);

        for (u32 buffer_index = 0; buffer_index < gltf_scene.buffers_count; ++buffer_index) {
            glTF::Buffer& buffer = gltf_scene.buffers[buffer_index];

            FileReadResult buffer_data = file_read_binary(buffer.uri.data, resident_allocator);
            if (buffer_data.data == nullptr) {
                HWARN("No .bin found in the glTF model");
                buffer_data.data = (char*)halloca(buffer.byte_length, resident_allocator);
                memory_copy(buffer_data.data, buffer.uri.data, buffer.byte_length);
                buffer_data.size = buffer.byte_length;
            }
            buffers_data.push(buffer_data.data);
        }

        i64 end_reading_buffers_data = Time::now();

        // Load all buffers and initialize them with buffer data
        //buffers.init(resident_allocator, gltf_scene.buffer_views_count);

        for (u32 buffer_index = 0; buffer_index < gltf_scene.buffer_views_count; ++buffer_index) {
            glTF::BufferView& buffer = gltf_scene.buffer_views[buffer_index];

            i32 offset = buffer.byte_offset;
            if (offset == glTF::INVALID_INT_VALUE) {
                offset = 0;
            }

            u8* buffer_data = (u8*)buffers_data[buffer.buffer] + offset;

            // NOTE(marco): the target attribute of a BufferView is not mandatory, so we prepare for both uses
            VkBufferUsageFlags flags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

            char* buffer_name = buffer.name.data;
            if (buffer_name == nullptr) {
                //buffer_name = name_buffer.append_use_f("buffer_%u", buffer_index);
            }
            // TODO: Identify resources (buffers in this case) that have the same name
            buffer_name = renderer->resource_name_buffer.append_use_f("buffer_%u", buffer_index + current_buffers_count);
            BufferResource* br = renderer->create_buffer(flags, ResourceUsageType::Immutable, buffer.byte_length, buffer_data, buffer_name);
            HASSERT(br != nullptr);

            buffers.push(*br);
        }

        for (u32 buffer_index = 0; buffer_index < gltf_scene.buffers_count; ++buffer_index) {
            void* buffer = buffers_data[buffer_index];
            resident_allocator->deallocate(buffer);
        }
        buffers_data.shutdown();

        i64 end_creating_buffers = Time::now();

        temp_allocator->free_marker(temp_allocator_initial_marker);

        // Init runtime meshes
        // TODO: Right now mesh node has a reference to the data in meshes, This data changes because
        //       its in an array.
        //       Figure out how to make it so when meshes "grows" this does not affect MeshNode.
        //meshes.init(resident_allocator, 200);

        i64 end_loading = Time::now();

        HINFO("Loaded scene {} in {} seconds.\nStats:\n\tReading GLTF file {} seconds\n\tTextures Creating {} seconds\n\tCreating Samplers {} seconds\n\tReading Buffers Data {} seconds\n\tCreating Buffers {} seconds", filename,
            Time::delta_seconds(start_scene_loading, end_loading), Time::delta_seconds(start_scene_loading, end_loading_file), Time::delta_seconds(end_loading_file, end_creating_textures),
            Time::delta_seconds(end_creating_textures, end_creating_samplers),
            Time::delta_seconds(end_creating_samplers, end_reading_buffers_data), Time::delta_seconds(end_reading_buffers_data, end_creating_buffers));
    }

    void glTFScene::free_gpu_resources(Renderer* renderer) {
        GpuDevice& gpu = *renderer->gpu;

        for (u32 mesh_index = 0; mesh_index < transparent_meshes.size; ++mesh_index) {
            Mesh& mesh = transparent_meshes[mesh_index];
            gpu.destroy_buffer(mesh.pbr_material.material_buffer);
            // TODO: Destroy the images.
            gpu.destroy_descriptor_set(mesh.pbr_material.descriptor_set);
        }
        for (u32 mesh_index = 0; mesh_index < opaque_meshes.size; ++mesh_index) {
            Mesh& mesh = opaque_meshes[mesh_index];
            gpu.destroy_buffer(mesh.pbr_material.material_buffer);
            // TODO: Destroy the images.
            gpu.destroy_descriptor_set(mesh.pbr_material.descriptor_set);
        }

        depth_pre_pass.free_gpu_resources();
        gbuffer_pass.free_gpu_resources();
        light_pass.free_gpu_resources();
        transparent_pass.free_gpu_resources();
        light_debug_pass.free_gpu_resources();

        gpu.destroy_descriptor_set(fullscreen_ds);

        transparent_meshes.shutdown();
        opaque_meshes.shutdown();
        //meshes.shutdown();
    }

    void glTFScene::unload(Renderer* renderer) {
        GpuDevice& gpu = *renderer->gpu;

        destroy_node(node_pool.root_node);

        node_pool.shutdown();
        // Free scene buffers
        samplers.shutdown();
        images.shutdown();
        buffers.shutdown();


        // NOTE(marco): we can't destroy this sooner as textures and buffers
        // hold a pointer to the names stored here
        gltf_free(gltf_scene);

        names.shutdown();
    }

    void glTFScene::register_render_passes(FrameGraph* frame_graph_) {
        frame_graph = frame_graph_;

        frame_graph->builder->register_render_pass("depth_pre_pass", &depth_pre_pass);
        frame_graph->builder->register_render_pass("gbuffer_pass", &gbuffer_pass);
        frame_graph->builder->register_render_pass("lighting_pass", &light_pass);
        frame_graph->builder->register_render_pass("transparent_pass", &transparent_pass);
        frame_graph->builder->register_render_pass("light_debug_pass", &light_debug_pass);

        light_debug_pass.prepare_draws(*this, frame_graph, renderer->gpu->allocator);
    }

    void glTFScene::prepare_draws(Renderer* renderer, StackAllocator* stack_allocator) {

        sizet cached_scratch_size = stack_allocator->get_marker();

        const u64 hashed_name = hash_calculate("geometry");
        Program* geometry_program = renderer->resource_cache.programs.get(hashed_name);

        glTF::Scene& root_gltf_scene = gltf_scene.scenes[gltf_scene.scene];


        Array<i32> node_parents;
        node_parents.init(stack_allocator, gltf_scene.nodes_count, gltf_scene.nodes_count);

        Array<glm::mat4> node_matrix;
        node_matrix.init(stack_allocator, gltf_scene.nodes_count, gltf_scene.nodes_count);

        Array<u32> node_stack;
        node_stack.init(&MemoryService::instance()->system_allocator, 8);

        // Create the node resources
        Array<NodeHandle> node_handles;
        node_handles.init(stack_allocator, gltf_scene.nodes_count, gltf_scene.nodes_count);

        for (u32 node_index = 0; node_index < gltf_scene.nodes_count; ++node_index) {
            glTF::Node& node = gltf_scene.nodes[node_index];
            node_handles[node_index] = node_pool.obtain_node(NodeType::Node);
        }

        // Root Nodes
        for (u32 node_index = 0; node_index < root_gltf_scene.nodes_count; ++node_index) {
            u32 root_node_index = root_gltf_scene.nodes[node_index];
            node_parents[root_node_index] = -1;
            node_stack.push(root_node_index);
            Node* node = (Node*)node_pool.access_node(node_handles[root_node_index]);
            node_pool.get_root_node()->add_child(node);
        }

        while (node_stack.size) {
            u32 node_index = node_stack.back();
            node_stack.pop();
            glTF::Node& node = gltf_scene.nodes[node_index];

            glm::mat4 local_matrix{ };
            Transform local_transform{ };
            local_transform.translation = glm::vec3(0.0f);
            local_transform.scale = glm::vec3(1.0f);
            local_transform.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

            if (node.matrix_count) {
                memcpy(&local_matrix, node.matrix, sizeof(glm::mat4));
                local_transform.set_transform(local_matrix);
            }
            else {
                glm::vec3 node_scale(1.0f, 1.0f, 1.0f);
                if (node.scale_count != 0) {
                    HASSERT(node.scale_count == 3);
                    node_scale = glm::vec3(node.scale[0], node.scale[1], node.scale[2]);
                }

                glm::vec3 node_translation(0.f, 0.f, 0.f);
                if (node.translation_count) {
                    HASSERT(node.translation_count == 3);
                    node_translation = glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
                }

                // Rotation is written as a plain quaternion
                glm::quat node_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                if (node.rotation_count) {
                    HASSERT(node.rotation_count == 4);
                    node_rotation = glm::quat(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]);
                }

                local_transform.translation = node_translation;
                local_transform.scale = node_scale;
                local_transform.rotation = node_rotation;

                local_matrix = local_transform.calculate_matrix();
            }

            node_matrix[node_index] = local_matrix;

            Node* base_node = (Node*)node_pool.access_node(node_handles[node_index]);

            cstring node_name = names.append_use_f("%s%d", "Node_", node_handles[node_index].index);

            base_node->name = node.name.data ? node.name.data : node_name;
            base_node->local_transform = local_transform;
            
            i32 node_parent = node_parents[node_index];
            // Nodes that don't have parents would already have their parent set to the root node
            if (node_parent != -1)
                base_node->parent = node_handles[node_parent];

            
            // Assuming nodes that contain meshes don't contain other glTF nodes
            if (node.mesh == glTF::INVALID_INT_VALUE) {
                base_node->children.init(node_pool.allocator, node.children_count, node.children_count);
                for (u32 child_index = 0; child_index < node.children_count; ++child_index) {
                    u32 child_node_index = node.children[child_index];
                    node_parents[child_node_index] = node_index;

                    node_stack.push(child_node_index);
                    base_node->children[child_index] = node_handles[child_node_index];
                }
                continue;
            }

            glTF::Mesh& gltf_mesh = gltf_scene.meshes[node.mesh];

            base_node->children.init(node_pool.allocator, gltf_mesh.primitives_count);

            glm::vec3 node_scale{ 1.0f, 1.0f, 1.0f };// TODO: Needed?
            if (node.scale_count != 0) {
                HASSERT(node.scale_count == 3);
                node_scale = glm::vec3{ node.scale[0], node.scale[1], node.scale[2] };
            }

            // Gltf primitives are conceptually submeshes.
            for (u32 primitive_index = 0; primitive_index < gltf_mesh.primitives_count; ++primitive_index) {
                Mesh mesh{ };

                //mesh.scale = node_scale;


                glTF::MeshPrimitive& mesh_primitive = gltf_mesh.primitives[primitive_index];

                const i32 position_accessor_index = gltf_get_attribute_accessor_index(mesh_primitive.attributes, mesh_primitive.attribute_count, "POSITION");
                const i32 tangent_accessor_index = gltf_get_attribute_accessor_index(mesh_primitive.attributes, mesh_primitive.attribute_count, "TANGENT");
                const i32 normal_accessor_index = gltf_get_attribute_accessor_index(mesh_primitive.attributes, mesh_primitive.attribute_count, "NORMAL");
                const i32 texcoord_accessor_index = gltf_get_attribute_accessor_index(mesh_primitive.attributes, mesh_primitive.attribute_count, "TEXCOORD_0");

                get_mesh_vertex_buffer(*this, position_accessor_index, mesh.position_buffer, mesh.position_offset);
                get_mesh_vertex_buffer(*this, tangent_accessor_index, mesh.tangent_buffer, mesh.tangent_offset);
                get_mesh_vertex_buffer(*this, normal_accessor_index, mesh.normal_buffer, mesh.normal_offset);
                get_mesh_vertex_buffer(*this, texcoord_accessor_index, mesh.texcoord_buffer, mesh.texcoord_offset);

                // Create index buffer
                glTF::Accessor& indices_accessor = gltf_scene.accessors[mesh_primitive.indices];
                HASSERT(indices_accessor.component_type == glTF::Accessor::ComponentType::UNSIGNED_SHORT || indices_accessor.component_type == glTF::Accessor::ComponentType::UNSIGNED_INT);
                mesh.index_type = (indices_accessor.component_type == glTF::Accessor::ComponentType::UNSIGNED_SHORT) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;

                glTF::BufferView& indices_buffer_view = gltf_scene.buffer_views[indices_accessor.buffer_view];
                BufferResource& indices_buffer_gpu = buffers[indices_accessor.buffer_view + current_buffers_count];
                mesh.index_buffer = indices_buffer_gpu.handle;
                mesh.index_offset = indices_accessor.byte_offset == glTF::INVALID_INT_VALUE ? 0 : indices_accessor.byte_offset;
                mesh.primitive_count = indices_accessor.count;

                // Create material
                glTF::Material& material = gltf_scene.materials[mesh_primitive.material];

                //bool transparent = get_mesh_material(*renderer, *this, material, mesh);

                fill_pbr_material(*renderer, material, mesh.pbr_material);

                // Create material buffer
                BufferCreation buffer_creation;
                buffer_creation.reset().set(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof(GPUMeshData)).set_name("mesh_data");
                mesh.pbr_material.material_buffer = renderer->gpu->create_buffer(buffer_creation);

                DescriptorSetCreation ds_creation{};
                DescriptorSetLayoutHandle layout = renderer->gpu->get_descriptor_set_layout(geometry_program->passes[3].pipeline, k_material_descriptor_set_index);
                ds_creation.buffer(local_constants_buffer, 0).buffer(mesh.pbr_material.material_buffer, 1).set_layout(layout);
                mesh.pbr_material.descriptor_set = renderer->gpu->create_descriptor_set(ds_creation);

                mesh.pbr_material.material = pbr_material;

                // TODO Make this a primitive struct. Not a MeshNode
                NodeHandle mesh_handle = node_pool.obtain_node(NodeType::MeshNode);
                MeshNode* mesh_node_primitive = (MeshNode*)node_pool.access_node(mesh_handle);
                mesh_node_primitive->children.size = 0;
                mesh_node_primitive->name = "Mesh_Primitive";
                mesh_node_primitive->parent = node_handles[node_index];

                mesh_node_primitive->children.size = 0;


                // TODO: Extract the position from the position buffer.
                base_node->children.push(mesh_handle);

                mesh.node_index = mesh_handle.index;

                if (mesh.is_transparent()) {
                    transparent_meshes.push(mesh);
                    mesh_node_primitive->mesh = &transparent_meshes[transparent_meshes.size - 1];
                }
                else {
                    opaque_meshes.push(mesh);
                    mesh_node_primitive->mesh = &opaque_meshes[opaque_meshes.size - 1];
                }

                //meshes.push(mesh);
                //mesh_node_primitive->mesh = &meshes[meshes.size - 1];

            }
        }

        current_images_count += gltf_scene.images_count;
        current_buffers_count += gltf_scene.buffer_views_count;
        current_samplers_count += gltf_scene.samplers_count;

        //qsort(meshes.data, meshes.size, sizeof(Mesh), gltf_mesh_material_compare);
        node_pool.get_root_node()->update_transform(&node_pool);

        stack_allocator->free_marker(cached_scratch_size);

        depth_pre_pass.prepare_draws(*this, frame_graph, renderer->gpu->allocator);
        gbuffer_pass.prepare_draws(*this, frame_graph, renderer->gpu->allocator);
        light_pass.prepare_draws(*this, frame_graph, renderer->gpu->allocator);
        transparent_pass.prepare_draws(*this, frame_graph, renderer->gpu->allocator);
        //light_debug_pass.prepare_draws(*this, frame_graph, renderer->gpu->allocator);

        // Handle fullscreen pass.
        

        //node_parents.shutdown();
        //node_matrix.shutdown();
        node_stack.shutdown();
    }

    void glTFScene::fill_pbr_material(Renderer& renderer, glTF::Material& material, PBRMaterial& pbr_material) {
        GpuDevice& gpu = *renderer.gpu;

        // Handle flags
        if (material.alpha_mode.data != nullptr && strcmp(material.alpha_mode.data, "MASK") == 0) {
            pbr_material.flags |= DrawFlags_AlphaMask;
        }
        else if (material.alpha_mode.data != nullptr && strcmp(material.alpha_mode.data, "BLEND") == 0) {
            pbr_material.flags |= DrawFlags_Transparent;
        }

        pbr_material.flags |= material.double_sided ? DrawFlags_DoubleSided : 0;
        // Alpha cutoff
        pbr_material.alpha_cutoff = material.alpha_cutoff != glTF::INVALID_FLOAT_VALUE ? material.alpha_cutoff : 1.f;

        if (material.pbr_metallic_roughness != nullptr) {
            if (material.pbr_metallic_roughness->base_color_factor_count != 0) {
                HASSERT(material.pbr_metallic_roughness->base_color_factor_count == 4);

                memcpy(&pbr_material.base_color_factor.x, material.pbr_metallic_roughness->base_color_factor, sizeof(glm::vec4));
            }
            else {
                pbr_material.base_color_factor = { 1.0f, 1.0f, 1.0f, 1.0f };
            }

            pbr_material.metallic_roughness_occlusion_factor.x = material.pbr_metallic_roughness->roughness_factor != glTF::INVALID_FLOAT_VALUE ? material.pbr_metallic_roughness->roughness_factor : 1.f;
            pbr_material.metallic_roughness_occlusion_factor.y = material.pbr_metallic_roughness->metallic_factor != glTF::INVALID_FLOAT_VALUE ? material.pbr_metallic_roughness->metallic_factor : 1.f;

            pbr_material.diffuse_texture_index = get_material_texture(gpu, material.pbr_metallic_roughness->base_color_texture);
            pbr_material.roughness_texture_index = get_material_texture(gpu, material.pbr_metallic_roughness->metallic_roughness_texture);
        }

        pbr_material.occlusion_texture_index = get_material_texture(gpu, (material.occlusion_texture != nullptr) ? material.occlusion_texture->index : -1);
        pbr_material.normal_texture_index = get_material_texture(gpu, (material.normal_texture != nullptr) ? material.normal_texture->index : -1);

        if (material.occlusion_texture != nullptr) {
            if (material.occlusion_texture->strength != glTF::INVALID_FLOAT_VALUE) {
                pbr_material.metallic_roughness_occlusion_factor.z = material.occlusion_texture->strength;
            }
            else {
                pbr_material.metallic_roughness_occlusion_factor.z = 1.0f;
            }
        }
    }

    u16 glTFScene::get_material_texture(GpuDevice& gpu, glTF::TextureInfo* texture_info) {
        if (texture_info != nullptr) {
            glTF::Texture& gltf_texture = gltf_scene.textures[texture_info->index];
            TextureResource& texture_gpu = images[gltf_texture.source + current_images_count];
            SamplerResource& sampler_gpu = samplers[gltf_texture.sampler + current_samplers_count];

            gpu.link_texture_sampler(texture_gpu.handle, sampler_gpu.handle);

            return texture_gpu.handle.index;
        }
        else {
            return k_invalid_index;
        }
    }

    u16 glTFScene::get_material_texture(GpuDevice& gpu, i32 gltf_texture_index) {
        if (gltf_texture_index >= 0) {
            glTF::Texture& gltf_texture = gltf_scene.textures[gltf_texture_index];
            TextureResource& texture_gpu = images[gltf_texture.source + current_images_count];
            SamplerResource& sampler_gpu = samplers[gltf_texture.sampler + current_samplers_count];

            gpu.link_texture_sampler(texture_gpu.handle, sampler_gpu.handle);

            return texture_gpu.handle.index;
        }
        else {
            return k_invalid_index;
        }
    }

    void glTFScene::fill_gpu_material_buffer(float model_scale) {
        // Update per mesh material buffer
        for (u32 mesh_index = 0; mesh_index < opaque_meshes.size; ++mesh_index) {
            Mesh& mesh = opaque_meshes[mesh_index];

            MapBufferParameters material_buffer_map = { mesh.pbr_material.material_buffer, 0, 0 };
            GPUMeshData* mesh_data = (GPUMeshData*)renderer->gpu->map_buffer(material_buffer_map);
            if (mesh_data) {
                copy_gpu_material_data(*mesh_data, mesh);
                copy_gpu_mesh_matrix(*mesh_data, mesh, model_scale, &node_pool.mesh_nodes);

                renderer->gpu->unmap_buffer(material_buffer_map);
            }
        }
        for (u32 mesh_index = 0; mesh_index < transparent_meshes.size; ++mesh_index) {
            Mesh& mesh = transparent_meshes[mesh_index];

            MapBufferParameters material_buffer_map = { mesh.pbr_material.material_buffer, 0, 0 };
            GPUMeshData* mesh_data = (GPUMeshData*)renderer->gpu->map_buffer(material_buffer_map);
            if (mesh_data) {
                copy_gpu_material_data(*mesh_data, mesh);
                copy_gpu_mesh_matrix(*mesh_data, mesh, model_scale, &node_pool.mesh_nodes);

                renderer->gpu->unmap_buffer(material_buffer_map);
            }
        }

        light_pass.fill_gpu_material_buffer();
    }

    void glTFScene::submit_draw_task(ImGuiService* imgui, GPUProfiler* gpu_profiler, enki::TaskScheduler* task_scheduler) {
        glTFDrawTask draw_task;
        draw_task.init(renderer->gpu, frame_graph, renderer, imgui, gpu_profiler, this);
        task_scheduler->AddTaskSetToPipe(&draw_task);
        task_scheduler->WaitforTask(&draw_task);
        // Avoid using the same command buffer
        renderer->add_texture_update_commands((draw_task.thread_id + 1) % task_scheduler->GetNumTaskThreads());
    }

    void glTFScene::draw_mesh(CommandBuffer* gpu_commands, Mesh& mesh) {

        gpu_commands->bind_vertex_buffer(mesh.position_buffer, 0, mesh.position_offset);
        gpu_commands->bind_vertex_buffer(mesh.tangent_buffer, 1, mesh.tangent_offset);
        gpu_commands->bind_vertex_buffer(mesh.normal_buffer, 2, mesh.normal_offset);
        gpu_commands->bind_vertex_buffer(mesh.texcoord_buffer, 3, mesh.texcoord_offset);
        gpu_commands->bind_index_buffer(mesh.index_buffer, mesh.index_offset, mesh.index_type);

        gpu_commands->bind_descriptor_set(&mesh.pbr_material.descriptor_set, 1, nullptr, 0);

        gpu_commands->draw_indexed(TopologyType::Triangle, mesh.primitive_count, 1, 0, 0, 0);
    }

    void glTFScene::destroy_node(NodeHandle handle) {
        Node* node = (Node*)node_pool.access_node(handle);
        for (u32 i = 0; i < node->children.size; i++) {
            destroy_node(node->children[i]);
        }
        node->children.shutdown();
        switch (handle.type)
        {
        case NodeType::Node:
            node_pool.base_nodes.release_resource(handle.index);
            break;
        case NodeType::MeshNode:
            node_pool.mesh_nodes.release_resource(handle.index);
            break;
        case NodeType::LightNode:
            node_pool.light_nodes.release_resource(handle.index);
            break;
        default:
            HERROR("Invalid NodeType");
            break;
        }
    }

    void glTFScene::imgui_draw_node_property(NodeHandle node_handle) {
        if (ImGui::Begin("Node Properties")) {
            if (node_handle.index == k_invalid_index) {
                ImGui::Text("No node selected");
            }
            else {
                Node* node = (Node*)node_pool.access_node(node_handle);
                ImGui::Text("Name: %s", node->name);
                ImGui::Text("Type: %s", node_type_to_cstring(node_handle.type));

                if (!(node->parent.index == k_invalid_index)) {
                    Node* parent = (Node*)node_pool.access_node(node->parent);
                    ImGui::Text("Parent: %s", parent->name);
                }

                bool modified = false;

                glm::vec3 local_rotation = glm::degrees(glm::eulerAngles(node->local_transform.rotation));
                glm::vec3 world_rotation = glm::degrees(glm::eulerAngles(node->world_transform.rotation));

                // TODO: Represent rotation as quats
                ImGui::Text("Local Transform");
                modified |= ImGui::InputFloat3("position##local", (float*)&node->local_transform.translation);
                modified |= ImGui::InputFloat3("scale##local", (float*)&node->local_transform.scale);
                modified |= ImGui::InputFloat3("rotation##local", (float*)&local_rotation);

                ImGui::Text("World Transform");
                ImGui::InputFloat3("position##world", (float*)&node->world_transform.translation);
                ImGui::InputFloat3("scale##world", (float*)&node->world_transform.scale);
                ImGui::InputFloat3("rotation##world", (float*)&world_rotation);

                if (modified) {
                    node->local_transform.rotation = glm::quat(glm::radians(local_rotation));
                    node->update_transform(&node_pool);
                }
            }
        }
        ImGui::End();
    }

    void glTFScene::imgui_draw_node(NodeHandle node_handle) {
        Node* node = (Node*)node_pool.access_node(node_handle);

        if (node->name == nullptr)
            return;
        // Make a tree node for nodes with children
        ImGuiTreeNodeFlags tree_node_flags = 0;
        tree_node_flags |= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
        if (!node->children.size) {
            tree_node_flags |= ImGuiTreeNodeFlags_Leaf;
        }
        if (ImGui::TreeNodeEx(node->name, tree_node_flags)) {

            if (current_node != node_handle && ImGui::IsItemClicked()) {
                current_node.index = node_handle.index;
                current_node.type = node_handle.type;
            }

            for (u32 i = 0; i < node->children.size; i++) {
                imgui_draw_node(node->children[i]);
            }
            ImGui::TreePop();
        }
    }

    void glTFScene::imgui_draw_hierarchy() {
        if (ImGui::Begin("Scene Hierarchy")) {
            ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
            if (ImGui::Button("Add GLTF model", { viewportPanelSize.x, 30 })) {
                char* file_path = nullptr;
                char* filename = nullptr;
                if (file_open_dialog(file_path, filename)) {
                    HDEBUG("Found file!, {}, Oath: {}", filename, file_path);

                    Directory cwd{};
                    directory_current(&cwd);

                    directory_change(file_path);

                    load(filename, file_path, main_allocator, scratch_allocator, loader);

                    directory_change(cwd.path);

                    prepare_draws(renderer, scratch_allocator);

                    //gltf_free(gltf_scene);

                    directory_change(cwd.path);

                    delete[] filename, file_path;
                }
            }

            imgui_draw_node(node_pool.root_node);
            ImGui::End();
        }
    }

    // Nodes //////////////////////////////////////////

    void NodePool::init(Allocator* allocator_) {
        allocator = allocator_;

        mesh_nodes.init(allocator_, 300, sizeof(MeshNode));
        base_nodes.init(allocator_, 50, sizeof(Node));
        light_nodes.init(allocator_, 5, sizeof(LightNode));

        root_node = obtain_node(NodeType::Node);

        Node* root = (Node*)access_node(root_node);
        root->children.init(allocator, 4);
        root->parent = { k_invalid_index, NodeType::Node };
        root->name = "Root_Node";
        root->world_transform = Transform{};
        root->local_transform = Transform{};
    }

    void NodePool::shutdown() {
        mesh_nodes.shutdown();
        base_nodes.shutdown();
        light_nodes.shutdown();
    }

    void* NodePool::access_node(NodeHandle handle) {
        switch (handle.type)
        {
        case NodeType::Node:
            return base_nodes.access_resource(handle.index);
        case NodeType::MeshNode:
            return mesh_nodes.access_resource(handle.index);
        case NodeType::LightNode:
            return light_nodes.access_resource(handle.index);
        default:
            HERROR("Invalid NodeType");
            return nullptr;
        }
    }

    Node* NodePool::get_root_node(){
        Node* root = (Node*)access_node(root_node);
        HASSERT(root);
        return root;
    }

    NodeHandle NodePool::obtain_node(NodeType type) {
        NodeHandle handle{};
        switch (type)
        {
        case NodeType::Node: {
            handle = { base_nodes.obtain_resource(), NodeType::Node };
            Node* base_node = new((Node*)access_node(handle)) Node();
            base_node->updateFunc = update_transform;
            base_node->handle = handle;
            break;
        }
        case NodeType::MeshNode: {
            handle = { mesh_nodes.obtain_resource(), NodeType::MeshNode };
            MeshNode* mesh_node = new((MeshNode*)access_node(handle)) MeshNode();
            mesh_node->updateFunc = update_mesh_transform;
            mesh_node->handle = handle;
            break;
        }
        case NodeType::LightNode: {
            handle = { light_nodes.obtain_resource(), NodeType::LightNode };
            LightNode* light_node = new((LightNode*)access_node(handle)) LightNode();
            light_node->updateFunc = update_transform;
            light_node->handle = handle;
            break;
        }
        default:
            HERROR("Invalid NodeType");
            break;
        }
        return handle;
    }
}// namespace Helix
