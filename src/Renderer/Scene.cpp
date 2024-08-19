#include "Scene.hpp"

#include <vendor/imgui/stb_image.h>

#include "Core/Gltf.hpp"
#include "Core/Time.hpp"
#include "Core/File.hpp"
#include <vendor/tracy/tracy/Tracy.hpp>
#include <imgui/imgui.h>

namespace Helix {
    

    // Helper functions //////////////////////////////////////////////////

    // Light
    Helix::PipelineHandle                  light_pipeline;
   
    Helix::DescriptorSetHandle             light_ds;
    

    void get_mesh_vertex_buffer(glTFScene& scene, i32 accessor_index, BufferHandle& out_buffer_handle, u32& out_buffer_offset) {

        if (accessor_index != -1) {
            glTF::Accessor& buffer_accessor = scene.gltf_scene.accessors[accessor_index];
            glTF::BufferView& buffer_view = scene.gltf_scene.buffer_views[buffer_accessor.buffer_view];
            BufferResource& buffer_gpu = scene.buffers[buffer_accessor.buffer_view];

            out_buffer_handle = buffer_gpu.handle;
            out_buffer_offset = buffer_accessor.byte_offset == glTF::INVALID_INT_VALUE ? 0 : buffer_accessor.byte_offset;
        }
    }

    bool get_mesh_material(Renderer& renderer, glTFScene& scene, glTF::Material& material, MeshDraw& mesh_draw) {

        bool transparent = false;
        GpuDevice& gpu = *renderer.gpu;

        if (material.pbr_metallic_roughness != nullptr) {
            if (material.pbr_metallic_roughness->base_color_factor_count != 0) {
                HASSERT(material.pbr_metallic_roughness->base_color_factor_count == 4);

                mesh_draw.base_color_factor = {
                    material.pbr_metallic_roughness->base_color_factor[0],
                    material.pbr_metallic_roughness->base_color_factor[1],
                    material.pbr_metallic_roughness->base_color_factor[2],
                    material.pbr_metallic_roughness->base_color_factor[3],
                };
            }
            else {
                mesh_draw.base_color_factor = { 1.0f, 1.0f, 1.0f, 1.0f };
            }

            if (material.pbr_metallic_roughness->roughness_factor != glTF::INVALID_FLOAT_VALUE) {
                mesh_draw.metallic_roughness_occlusion_factor.x = material.pbr_metallic_roughness->roughness_factor;
            }
            else {
                mesh_draw.metallic_roughness_occlusion_factor.x = 1.0f;
            }

            if (material.alpha_mode.data != nullptr && strcmp(material.alpha_mode.data, "MASK") == 0) {
                mesh_draw.flags |= DrawFlags_AlphaMask;
                transparent = true;
            }

            if (material.alpha_cutoff != glTF::INVALID_FLOAT_VALUE) {
                mesh_draw.alpha_cutoff = material.alpha_cutoff;
            }

            if (material.pbr_metallic_roughness->metallic_factor != glTF::INVALID_FLOAT_VALUE) {
                mesh_draw.metallic_roughness_occlusion_factor.y = material.pbr_metallic_roughness->metallic_factor;
            }
            else {
                mesh_draw.metallic_roughness_occlusion_factor.y = 1.0f;
            }

            if (material.pbr_metallic_roughness->base_color_texture != nullptr) {
                glTF::Texture& diffuse_texture = scene.gltf_scene.textures[material.pbr_metallic_roughness->base_color_texture->index];
                TextureResource& diffuse_texture_gpu = scene.images[diffuse_texture.source];
                SamplerResource& diffuse_sampler_gpu = scene.samplers[diffuse_texture.sampler];

                mesh_draw.diffuse_texture_index = diffuse_texture_gpu.handle.index;

                gpu.link_texture_sampler(diffuse_texture_gpu.handle, diffuse_sampler_gpu.handle);
            }
            else {
                mesh_draw.diffuse_texture_index = INVALID_TEXTURE_INDEX;
            }

            if (material.pbr_metallic_roughness->metallic_roughness_texture != nullptr) {
                glTF::Texture& roughness_texture = scene.gltf_scene.textures[material.pbr_metallic_roughness->metallic_roughness_texture->index];
                TextureResource& roughness_texture_gpu = scene.images[roughness_texture.source];
                SamplerResource& roughness_sampler_gpu = scene.samplers[roughness_texture.sampler];

                mesh_draw.roughness_texture_index = roughness_texture_gpu.handle.index;

                gpu.link_texture_sampler(roughness_texture_gpu.handle, roughness_sampler_gpu.handle);
            }
            else {
                mesh_draw.roughness_texture_index = INVALID_TEXTURE_INDEX;
            }
        }

        if (material.occlusion_texture != nullptr) {
            glTF::Texture& occlusion_texture = scene.gltf_scene.textures[material.occlusion_texture->index];

            TextureResource& occlusion_texture_gpu = scene.images[occlusion_texture.source];
            SamplerResource& occlusion_sampler_gpu = scene.samplers[occlusion_texture.sampler];

            mesh_draw.occlusion_texture_index = occlusion_texture_gpu.handle.index;

            if (material.occlusion_texture->strength != glTF::INVALID_FLOAT_VALUE) {
                mesh_draw.metallic_roughness_occlusion_factor.z = material.occlusion_texture->strength;
            }
            else {
                mesh_draw.metallic_roughness_occlusion_factor.z = 1.0f;
            }

            gpu.link_texture_sampler(occlusion_texture_gpu.handle, occlusion_sampler_gpu.handle);
        }
        else {
            mesh_draw.occlusion_texture_index = INVALID_TEXTURE_INDEX;
        }

        if (material.normal_texture != nullptr) {
            glTF::Texture& normal_texture = scene.gltf_scene.textures[material.normal_texture->index];
            TextureResource& normal_texture_gpu = scene.images[normal_texture.source];
            SamplerResource& normal_sampler_gpu = scene.samplers[normal_texture.sampler];

            gpu.link_texture_sampler(normal_texture_gpu.handle, normal_sampler_gpu.handle);

            mesh_draw.normal_texture_index = normal_texture_gpu.handle.index;
        }
        else {
            mesh_draw.normal_texture_index = INVALID_TEXTURE_INDEX;
        }

        // Create material buffer
        BufferCreation buffer_creation;
        buffer_creation.reset().set(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof(MeshData)).set_name("mesh_data");
        mesh_draw.material_buffer = gpu.create_buffer(buffer_creation);

        return transparent;
    }

    int gltf_mesh_material_compare(const void* a, const void* b) {
        const MeshDraw* mesh_a = (const MeshDraw*)a;
        const MeshDraw* mesh_b = (const MeshDraw*)b;

        if (mesh_a->material->render_index < mesh_b->material->render_index) return -1;
        if (mesh_a->material->render_index > mesh_b->material->render_index) return  1;
        return 0;
    }

    static void upload_material(MeshData& mesh_data, const MeshDraw& mesh_draw, const f32 global_scale) {
        mesh_data.textures[0] = mesh_draw.diffuse_texture_index;
        mesh_data.textures[1] = mesh_draw.roughness_texture_index;
        mesh_data.textures[2] = mesh_draw.normal_texture_index;
        mesh_data.textures[3] = mesh_draw.occlusion_texture_index;
        mesh_data.base_color_factor = mesh_draw.base_color_factor;
        mesh_data.metallic_roughness_occlusion_factor = mesh_draw.metallic_roughness_occlusion_factor;
        mesh_data.alpha_cutoff = mesh_draw.alpha_cutoff;
        mesh_data.flags = mesh_draw.flags;

        // NOTE: for left-handed systems (as defined in cglm) need to invert positive and negative Z.
        mat4s scale_mat = glms_scale_make({ -global_scale, global_scale, global_scale });
        //mesh_data.m = model;
        mesh_data.m = glms_mat4_mul(mesh_draw.model, scale_mat);
        mesh_data.inverseM = glms_mat4_inv(glms_mat4_transpose(mesh_data.m));
    }

    static void draw_mesh(Renderer& renderer, CommandBuffer* gpu_commands, MeshDraw& mesh_draw) {

        gpu_commands->bind_vertex_buffer(mesh_draw.position_buffer, 0, mesh_draw.position_offset);
        gpu_commands->bind_vertex_buffer(mesh_draw.tangent_buffer, 1, mesh_draw.tangent_offset);
        gpu_commands->bind_vertex_buffer(mesh_draw.normal_buffer, 2, mesh_draw.normal_offset);
        gpu_commands->bind_vertex_buffer(mesh_draw.texcoord_buffer, 3, mesh_draw.texcoord_offset);
        gpu_commands->bind_index_buffer(mesh_draw.index_buffer, mesh_draw.index_offset, mesh_draw.index_type);

        gpu_commands->bind_descriptor_set(&mesh_draw.descriptor_set, 1, nullptr, 0);

        gpu_commands->draw_indexed(TopologyType::Triangle, mesh_draw.primitive_count, 1, 0, 0, 0);
    }

    cstring node_type_to_cstring(NodeType type) {
        switch (type)
        {
        case Helix::NodeType_Node:
            return "Node";
            break;
        case Helix::NodeType_MeshNode:
            return "Mesh Node";
            break;
        case Helix::NodeType_LightNode:
            return "Light Node";
            break;
        default:
            HCRITICAL("Invalid node type");
            break;
        }
    }

    // glTFDrawTask //////////////////////////////////

    void glTFDrawTask::init(GpuDevice* gpu_, Renderer* renderer_, ImGuiService* imgui_, GPUProfiler* gpu_profiler_, glTFScene* scene_) {
        gpu = gpu_;
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
        gpu_commands->push_marker("Frame");

        gpu_commands->clear(0.3f, 0.3f, 0.3f, 1.f);
        gpu_commands->clear_depth_stencil(1.0f, 0);
        gpu_commands->bind_pass(gpu->fullscreen_render_pass, gpu->fullscreen_framebuffer, false);
        
        gpu_commands->set_scissor(nullptr);
        Viewport viewport{};
        viewport.rect = { 0,0,gpu->swapchain_width, gpu->swapchain_height };
        viewport.max_depth = 1.0f;
        viewport.min_depth = 0.0f;
        gpu_commands->set_viewport(&viewport);

        Material* last_material = nullptr;
        // TODO(marco): loop by material so that we can deal with multiple passes
        for (u32 mesh_index = 0; mesh_index < scene->mesh_draws.size; ++mesh_index) {
            MeshDraw& mesh_draw = scene->mesh_draws[mesh_index];

            if (mesh_draw.material != last_material) {
                PipelineHandle pipeline = renderer->get_pipeline(mesh_draw.material);

                gpu_commands->bind_pipeline(pipeline);

                last_material = mesh_draw.material;
            }

            draw_mesh(*renderer, gpu_commands, mesh_draw);
        }

        gpu_commands->bind_pipeline(light_pipeline);
        gpu_commands->bind_descriptor_set(&light_ds, 1, nullptr, 0);
        gpu_commands->draw(TopologyType::Triangle, 0, 3, 0, 1);

        imgui->render(*gpu_commands, false);

        gpu_commands->pop_marker();

        gpu_profiler->update(*gpu);

        // Send commands to GPU
        gpu->queue_command_buffer(gpu_commands);
    }

    // gltfScene //////////////////////////////////////////////////

    void glTFScene::load(cstring filename, cstring path, Allocator* resident_allocator, StackAllocator* temp_allocator, AsynchronousLoader* async_loader) {


        renderer = async_loader->renderer;
        sizet temp_allocator_initial_marker = temp_allocator->get_marker();

        // Time statistics
        i64 start_scene_loading = Time::now();

        gltf_scene = gltf_load_file(filename);

        i64 end_loading_file = Time::now();

        node_pool.init(resident_allocator);

        // Load all textures
        images.init(resident_allocator, gltf_scene.images_count);

        StringBuffer name_buffer;
        name_buffer.init(hkilo(100), temp_allocator);

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
            async_loader->request_texture_data(full_filename, tr->handle);
            // Reset name buffer
            name_buffer.clear();
        }

        i64 end_loading_textures_files = Time::now();

        i64 end_creating_textures = Time::now();

        // Load all samplers
        samplers.init(resident_allocator, gltf_scene.samplers_count);

        for (u32 sampler_index = 0; sampler_index < gltf_scene.samplers_count; ++sampler_index) {
            glTF::Sampler& sampler = gltf_scene.samplers[sampler_index];

            char* sampler_name = renderer->resource_name_buffer.append_use_f("sampler_%u", sampler_index);

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
        buffers.init(resident_allocator, gltf_scene.buffer_views_count);

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
            buffer_name = renderer->resource_name_buffer.append_use_f("buffer_%u", buffer_index);
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
        mesh_draws.init(resident_allocator, gltf_scene.meshes_count);

        i64 end_loading = Time::now();

        HINFO("Loaded scene {} in {} seconds.\nStats:\n\tReading GLTF file {} seconds\n\tTextures Creating {} seconds\n\tCreating Samplers {} seconds\n\tReading Buffers Data {} seconds\n\tCreating Buffers {} seconds", filename,
            Time::delta_seconds(start_scene_loading, end_loading), Time::delta_seconds(start_scene_loading, end_loading_file), Time::delta_seconds(end_loading_file, end_creating_textures),
            Time::delta_seconds(end_creating_textures, end_creating_samplers),
            Time::delta_seconds(end_creating_samplers, end_reading_buffers_data), Time::delta_seconds(end_reading_buffers_data, end_creating_buffers));
    }

    void glTFScene::free_gpu_resources(Renderer* renderer) {
        GpuDevice& gpu = *renderer->gpu;

        for (u32 mesh_index = 0; mesh_index < mesh_draws.size; ++mesh_index) {
            MeshDraw& mesh_draw = mesh_draws[mesh_index];
            gpu.destroy_buffer(mesh_draw.material_buffer);
            // TODO: Destroy the images.
            gpu.destroy_descriptor_set(mesh_draw.descriptor_set);
        }

        gpu.destroy_descriptor_set(light_ds);

        mesh_draws.shutdown();
    }

    void glTFScene::unload(Renderer* renderer) {
        GpuDevice& gpu = *renderer->gpu;

        for (u32 i = 0; i < node_pool.root_nodes.size; i++)
            destroy_node(node_pool.root_nodes[i]);


        node_pool.shutdown();
        // Free scene buffers
        samplers.shutdown();
        images.shutdown();
        buffers.shutdown();

        // NOTE(marco): we can't destroy this sooner as textures and buffers
        // hold a pointer to the names stored here
        gltf_free(gltf_scene);
    }

    void glTFScene::prepare_draws(Renderer* renderer, StackAllocator* stack_allocator) {

        sizet cached_scratch_size = stack_allocator->get_marker();

        {
            // Creating the light image
            directory_change("D:/HelixEngine/Engine/assets/textures/lights");
            stbi_set_flip_vertically_on_load(true);
            TextureResource* tr = renderer->create_texture("Light", "point_light.png", true);
            stbi_set_flip_vertically_on_load(false);
            HASSERT(tr != nullptr);
            light_texture = *tr;
            // Create the light pipeline
            PipelineCreation pipeline_creation;

            pipeline_creation.name = "Light";

            FileReadResult vs_code = file_read_text("D:/HelixEngine/Engine/assets/shaders/lights/light.vert.glsl", stack_allocator);
            FileReadResult fs_code = file_read_text("D:/HelixEngine/Engine/assets/shaders/lights/light.frag.glsl", stack_allocator);
            FileReadResult gs_code = file_read_text("D:/HelixEngine/Engine/assets/shaders/lights/light.geom.glsl", stack_allocator);

            pipeline_creation.vertex_input.reset();
            pipeline_creation.render_pass = renderer->gpu->get_swapchain_output();
            pipeline_creation.depth_stencil.set_depth(true, VK_COMPARE_OP_LESS_OR_EQUAL);
            pipeline_creation.blend_state.add_blend_state().set_color(VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD);
            pipeline_creation.shaders
                .set_name("Light")
                .add_stage(vs_code.data, (u32)vs_code.size, VK_SHADER_STAGE_VERTEX_BIT)
                .add_stage(gs_code.data, (u32)gs_code.size, VK_SHADER_STAGE_GEOMETRY_BIT)
                .add_stage(fs_code.data, (u32)fs_code.size, VK_SHADER_STAGE_FRAGMENT_BIT);

            Program* light_program = renderer->create_program({ pipeline_creation });
            light_pipeline = light_program->passes[0].pipeline;
            
            BufferCreation buffer_creation;
            buffer_creation.reset().set(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof(LightUniform)).set_name("light_uniform_buffer");
            light_cb = renderer->gpu->create_buffer(buffer_creation);

            DescriptorSetCreation ds_creation{};
            DescriptorSetLayoutHandle layout = renderer->gpu->get_descriptor_set_layout(light_program->passes[0].pipeline, 0);
            ds_creation.buffer(light_cb, 0).set_layout(layout);
            light_ds = renderer->gpu->create_descriptor_set(ds_creation);
            
            NodeHandle light_node_handle = node_pool.obtain_node(NodeType_LightNode);
            node_pool.root_nodes.push(light_node_handle);

            LightNode* light_node = (LightNode*)node_pool.access_node(light_node_handle);
            light_node->name = "Point Light";
            light_node->local_transform.scale = { 1.0f, 1.0f, 1.0f };
            light_node->world_transform.scale = { 1.0f, 1.0f, 1.0f };
        }
        // Create pipeline state
        PipelineCreation pipeline_creation;

        StringBuffer path_buffer;
        path_buffer.init(1024, stack_allocator);

        cstring vert_file = "pbr.vert.glsl";
        char* vert_path = path_buffer.append_use_f("%s%s", HELIX_SHADER_FOLDER, vert_file);
        FileReadResult vert_code = file_read_text(vert_path, stack_allocator);

        cstring frag_file = "pbr.frag.glsl";
        char* frag_path = path_buffer.append_use_f("%s%s", HELIX_SHADER_FOLDER, frag_file);
        FileReadResult frag_code = file_read_text(frag_path, stack_allocator);

        // Vertex input
        // TODO(marco): could these be inferred from SPIR-V?
        pipeline_creation.vertex_input.add_vertex_attribute({ 0, 0, 0, VertexComponentFormat::Float3 }); // position
        pipeline_creation.vertex_input.add_vertex_stream({ 0, 12, VertexInputRate::PerVertex });

        pipeline_creation.vertex_input.add_vertex_attribute({ 1, 1, 0, VertexComponentFormat::Float4 }); // tangent
        pipeline_creation.vertex_input.add_vertex_stream({ 1, 16, VertexInputRate::PerVertex });

        pipeline_creation.vertex_input.add_vertex_attribute({ 2, 2, 0, VertexComponentFormat::Float3 }); // normal
        pipeline_creation.vertex_input.add_vertex_stream({ 2, 12, VertexInputRate::PerVertex });

        pipeline_creation.vertex_input.add_vertex_attribute({ 3, 3, 0, VertexComponentFormat::Float2 }); // texcoord
        pipeline_creation.vertex_input.add_vertex_stream({ 3, 8, VertexInputRate::PerVertex });

        // Render pass
        pipeline_creation.render_pass = renderer->gpu->get_swapchain_output();
        // Depth
        pipeline_creation.depth_stencil.set_depth(true, VK_COMPARE_OP_LESS_OR_EQUAL);

        // Blend
        pipeline_creation.blend_state.add_blend_state().set_color(VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD);

        pipeline_creation.shaders.set_name("pbr").add_stage(vert_code.data, vert_code.size, VK_SHADER_STAGE_VERTEX_BIT).add_stage(frag_code.data, frag_code.size, VK_SHADER_STAGE_FRAGMENT_BIT);

        pipeline_creation.rasterization.front = VK_FRONT_FACE_CLOCKWISE;

        // Constant buffer
        BufferCreation buffer_creation;
        buffer_creation.reset().set(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof(UniformData)).set_name("scene_buffer");
        scene_buffer = renderer->gpu->create_buffer(buffer_creation);

        pipeline_creation.name = "pbr_no_cull";
        Program* program_no_cull = renderer->create_program({ pipeline_creation });

        pipeline_creation.rasterization.cull_mode = VK_CULL_MODE_BACK_BIT;

        pipeline_creation.name = "pbr_cull";
        Program* program_cull = renderer->create_program({ pipeline_creation });

        MaterialCreation material_creation;

        material_creation.set_name("material_no_cull_opaque").set_program(program_no_cull).set_render_index(0);
        Material* material_no_cull_opaque = renderer->create_material(material_creation);

        material_creation.set_name("material_cull_opaque").set_program(program_cull).set_render_index(1);
        Material* material_cull_opaque = renderer->create_material(material_creation);

        material_creation.set_name("material_no_cull_transparent").set_program(program_no_cull).set_render_index(2);
        Material* material_no_cull_transparent = renderer->create_material(material_creation);

        material_creation.set_name("material_cull_transparent").set_program(program_cull).set_render_index(3);
        Material* material_cull_transparent = renderer->create_material(material_creation);

        stack_allocator->free_marker(cached_scratch_size);

        glTF::Scene& root_gltf_scene = gltf_scene.scenes[gltf_scene.scene];


        Array<i32> node_parents;
        node_parents.init(stack_allocator, gltf_scene.nodes_count, gltf_scene.nodes_count);

        Array<mat4s> node_matrix;
        node_matrix.init(stack_allocator, gltf_scene.nodes_count, gltf_scene.nodes_count);

        Array<u32> node_stack;
        node_stack.init(stack_allocator, 8);

        // Create the node resources
        Array<NodeHandle> node_handles;
        node_handles.init(stack_allocator, gltf_scene.nodes_count, gltf_scene.nodes_count);

        for (u32 node_index = 0; node_index < gltf_scene.nodes_count; ++node_index) {
            glTF::Node& node = gltf_scene.nodes[node_index];
            if (node.mesh == glTF::INVALID_INT_VALUE)
                node_handles[node_index] = node_pool.obtain_node(NodeType_Node);
            else
                node_handles[node_index] = node_pool.obtain_node(NodeType_MeshNode);
        }

        // Root Nodes
        for (u32 node_index = 0; node_index < root_gltf_scene.nodes_count; ++node_index) {
            u32 root_node = root_gltf_scene.nodes[node_index];
            node_parents[root_node] = -1;
            node_stack.push(root_node);
            node_pool.root_nodes.push(node_handles[root_node]);
        }

        while (node_stack.size) {
            u32 node_index = node_stack.back();
            node_stack.pop();
            glTF::Node& node = gltf_scene.nodes[node_index];

            mat4s local_matrix{ };
            Transform transform{ };
            transform.translation = { 0 };
            transform.scale = { 0 };
            transform.rotation = { 0 };

            if (node.matrix_count) {
                memcpy(&local_matrix, node.matrix, sizeof(mat4s));
            }
            else {
                vec3s node_scale{ 1.0f, 1.0f, 1.0f };
                if (node.scale_count != 0) {
                    HASSERT(node.scale_count == 3);
                    node_scale = vec3s{ node.scale[0], node.scale[1], node.scale[2] };
                }

                vec3s node_translation{ 0.f, 0.f, 0.f };
                if (node.translation_count) {
                    HASSERT(node.translation_count == 3);
                    node_translation = vec3s{ node.translation[0], node.translation[1], node.translation[2] };
                }

                // Rotation is written as a plain quaternion
                versors node_rotation = glms_quat_identity();
                if (node.rotation_count) {
                    HASSERT(node.rotation_count == 4);
                    node_rotation = glms_quat_init(node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3]);
                }

                
                transform.translation = node_translation;
                transform.scale = node_scale;
                transform.rotation = node_rotation;

                local_matrix = transform.calculate_matrix();
            }

            node_matrix[node_index] = local_matrix;

            if (node.mesh == glTF::INVALID_INT_VALUE) {
                Node* base_node = (Node*)node_pool.base_nodes.access_resource(node_handles[node_index].index);
                base_node->name = node.name.data ? node.name.data : "Node";
                base_node->local_transform = transform;
                base_node->world_transform = transform;
                base_node->children.init(node_pool.allocator, node.children_count, node.children_count);

                for (u32 child_index = 0; child_index < node.children_count; ++child_index) {
                    u32 child_node_index = node.children[child_index];
                    node_parents[child_node_index] = node_index;

                    node_stack.push(child_node_index);
                    base_node->children[child_index] = node_handles[child_node_index];
                }

                continue;
            }
            
            MeshNode* mesh_node = (MeshNode*)node_pool.mesh_nodes.access_resource(node_handles[node_index].index);
            mesh_node->name = node.name.data ? node.name.data : "Node";
            mesh_node->local_transform = transform;
            //base_node->world_transform = local_matrix;
            mesh_node->children.init(node_pool.allocator, node.children_count, node.children_count);
            for (u32 child_index = 0; child_index < node.children_count; ++child_index) {
                u32 child_node_index = node.children[child_index];
                node_parents[child_node_index] = node_index;
                node_stack.push(child_node_index);
                mesh_node->children[child_index] = node_handles[child_node_index];
            }


            glTF::Mesh& mesh = gltf_scene.meshes[node.mesh];

            mat4s final_matrix = local_matrix;
            i32 node_parent = node_parents[node_index];
            while (node_parent != -1) {
                final_matrix = glms_mat4_mul(node_matrix[node_parent], final_matrix);
                node_parent = node_parents[node_parent];
            }

            vec3s node_scale{ 1.0f, 1.0f, 1.0f };
            if (node.scale_count != 0) {
                HASSERT(node.scale_count == 3);
                node_scale = vec3s{ node.scale[0], node.scale[1], node.scale[2] };
            }

            // Gltf primitives are conceptually submeshes.
            for (u32 primitive_index = 0; primitive_index < mesh.primitives_count; ++primitive_index) {
                MeshDraw mesh_draw{ };

                mesh_draw.scale = node_scale;

                mesh_draw.model = final_matrix;

                glTF::MeshPrimitive& mesh_primitive = mesh.primitives[primitive_index];


                const i32 position_accessor_index = gltf_get_attribute_accessor_index(mesh_primitive.attributes, mesh_primitive.attribute_count, "POSITION");
                const i32 tangent_accessor_index = gltf_get_attribute_accessor_index(mesh_primitive.attributes, mesh_primitive.attribute_count, "TANGENT");
                const i32 normal_accessor_index = gltf_get_attribute_accessor_index(mesh_primitive.attributes, mesh_primitive.attribute_count, "NORMAL");
                const i32 texcoord_accessor_index = gltf_get_attribute_accessor_index(mesh_primitive.attributes, mesh_primitive.attribute_count, "TEXCOORD_0");

                get_mesh_vertex_buffer(*this, position_accessor_index, mesh_draw.position_buffer, mesh_draw.position_offset);
                get_mesh_vertex_buffer(*this, tangent_accessor_index, mesh_draw.tangent_buffer, mesh_draw.tangent_offset);
                get_mesh_vertex_buffer(*this, normal_accessor_index, mesh_draw.normal_buffer, mesh_draw.normal_offset);
                get_mesh_vertex_buffer(*this, texcoord_accessor_index, mesh_draw.texcoord_buffer, mesh_draw.texcoord_offset);

                // Create index buffer
                glTF::Accessor& indices_accessor = gltf_scene.accessors[mesh_primitive.indices];
                HASSERT(indices_accessor.component_type == glTF::Accessor::ComponentType::UNSIGNED_SHORT || indices_accessor.component_type == glTF::Accessor::ComponentType::UNSIGNED_INT);
                mesh_draw.index_type = (indices_accessor.component_type == glTF::Accessor::ComponentType::UNSIGNED_SHORT) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;

                glTF::BufferView& indices_buffer_view = gltf_scene.buffer_views[indices_accessor.buffer_view];
                BufferResource& indices_buffer_gpu = buffers[indices_accessor.buffer_view];
                mesh_draw.index_buffer = indices_buffer_gpu.handle;
                mesh_draw.index_offset = indices_accessor.byte_offset == glTF::INVALID_INT_VALUE ? 0 : indices_accessor.byte_offset;
                mesh_draw.primitive_count = indices_accessor.count;

                // Create material
                glTF::Material& material = gltf_scene.materials[mesh_primitive.material];

                bool transparent = get_mesh_material(*renderer, *this, material, mesh_draw);

                DescriptorSetCreation ds_creation{};
                DescriptorSetLayoutHandle layout = renderer->gpu->get_descriptor_set_layout(program_cull->passes[0].pipeline, 0);
                ds_creation.buffer(scene_buffer, 0).buffer(mesh_draw.material_buffer, 1).set_layout(layout);
                mesh_draw.descriptor_set = renderer->gpu->create_descriptor_set(ds_creation);

                if (transparent) {
                    if (material.double_sided) {
                        mesh_draw.material = material_no_cull_transparent;
                    }
                    else {
                        mesh_draw.material = material_cull_transparent;
                    }
                }
                else {
                    if (material.double_sided) {
                        mesh_draw.material = material_no_cull_opaque;
                    }
                    else {
                        mesh_draw.material = material_cull_opaque;
                    }
                }

                // TODO Make this a primitive struct. Not a MeshNode
                NodeHandle mesh_handle = node_pool.obtain_node(NodeType_MeshNode);
                MeshNode* mesh_node_primitive = (MeshNode*)node_pool.access_node(mesh_handle);
                mesh_node_primitive->children.size = 0;
                mesh_node_primitive->name = "Mesh_Primitive";
                mesh_node_primitive->parent = node_handles[node_index];

                mesh_node->children.push(mesh_handle);

                mesh_draws.push(mesh_draw);
            }
        }

        qsort(mesh_draws.data, mesh_draws.size, sizeof(MeshDraw), gltf_mesh_material_compare);
    }

    void glTFScene::upload_materials(float model_scale) {
        // Update per mesh material buffer
        for (u32 mesh_index = 0; mesh_index < mesh_draws.size; ++mesh_index) {
            MeshDraw& mesh_draw = mesh_draws[mesh_index];

            MapBufferParameters cb_map = { mesh_draw.material_buffer, 0, 0 };
            MeshData* mesh_data = (MeshData*)renderer->gpu->map_buffer(cb_map);
            if (mesh_data) {
                upload_material(*mesh_data, mesh_draw, model_scale);

                renderer->gpu->unmap_buffer(cb_map);
            }
        }
    }

    void glTFScene::submit_draw_task(ImGuiService* imgui, GPUProfiler* gpu_profiler, enki::TaskScheduler* task_scheduler) {
        glTFDrawTask draw_task;
        draw_task.init(renderer->gpu, renderer, imgui, gpu_profiler, this);
        task_scheduler->AddTaskSetToPipe(&draw_task);
        task_scheduler->WaitforTask(&draw_task);
        // Avoid using the same command buffer
        renderer->add_texture_update_commands((draw_task.thread_id + 1) % task_scheduler->GetNumTaskThreads());
    }

    void glTFScene::destroy_node(NodeHandle handle) {
        Node* node = (Node*)node_pool.access_node(handle);
        for (u32 i = 0; i < node->children.size; i++) {
            destroy_node(node->children[i]);
        }
        node->children.shutdown();
        switch (handle.type)
        {
        case NodeType_Node:
            node_pool.base_nodes.release_resource(handle.index);
            break;
        case NodeType_MeshNode:
            node_pool.mesh_nodes.release_resource(handle.index);
            break;
        case NodeType_LightNode:
            node_pool.light_nodes.release_resource(handle.index);
            break;
        default:
            HERROR("Invalid NodeType");
            break;
        }
    }

    void glTFScene::imgui_draw_node_property(NodeHandle node_handle){
        if (ImGui::Begin("Node Properties")) {
            if (node_handle.index == k_invalid_index) {
                ImGui::Text("No node selected");
            }
            else {
                Node* node = (Node*)node_pool.access_node(node_handle);
                ImGui::Text("Name: %s", node->name);
                ImGui::Text("Type: %s", node_type_to_cstring(node_handle.type));

                Transform old_transform = node->local_transform;
                bool modified = false;

                ImGui::Text("Local Transform");
                modified |= ImGui::InputFloat3("position##local", node->local_transform.translation.raw);
                modified |= ImGui::InputFloat3("scale##local", node->local_transform.scale.raw);
                ImGui::InputFloat3("rotation##local", node->local_transform.rotation.raw);

                ImGui::Text("World Transform");
                ImGui::InputFloat3("position##world", node->world_transform.translation.raw);
                ImGui::InputFloat3("scale##world", node->world_transform.scale.raw);
                ImGui::InputFloat3("rotation##world", node->world_transform.rotation.raw);

                if (modified) {
                    old_transform.translation = glms_vec3_sub(node->local_transform.translation, old_transform.translation);
                    old_transform.scale = glms_vec3_div(node->local_transform.scale, old_transform.scale);
                    node->update_transform(old_transform);
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

    void glTFScene::imgui_draw_hierarchy(){
        if (ImGui::Begin("Scene Hierarchy")) {
            for (u32 i = 0; i < node_pool.root_nodes.size; i++) {
                imgui_draw_node(node_pool.root_nodes[i]);
            }
            ImGui::End();
        }
    }

    // Nodes //////////////////////////////////////////

    void NodePool::init(Allocator* allocator_){
        allocator = allocator_;

        mesh_nodes.init(allocator_, 300, sizeof(MeshNode));
        base_nodes.init(allocator_, 50, sizeof(Node));
        light_nodes.init(allocator_, 5, sizeof(LightNode));

        root_nodes.init(allocator_, 15, 0);
    }

    void NodePool::shutdown(){
        mesh_nodes.shutdown();
        base_nodes.shutdown();
        light_nodes.shutdown();
        root_nodes.shutdown();
    }

    void* NodePool::access_node(NodeHandle handle){

        switch (handle.type)
        {
        case NodeType_Node:
            return base_nodes.access_resource(handle.index);
        case NodeType_MeshNode:
            return mesh_nodes.access_resource(handle.index);
        case NodeType_LightNode:
            return light_nodes.access_resource(handle.index);
        default:
            HERROR("Invalid NodeType");
            return nullptr;
        }
    }

    NodeHandle NodePool::obtain_node(NodeType type){
        switch (type)
        {
        case NodeType_Node:
            return { base_nodes.obtain_resource(), NodeType_Node };
        case NodeType_MeshNode:
            return { mesh_nodes.obtain_resource(), NodeType_MeshNode };
        case NodeType_LightNode:
            return { light_nodes.obtain_resource(), NodeType_LightNode };
        default:
            HERROR("Invalid NodeType");
            return NodeHandle();
        }
    }
}// namespace Helix
