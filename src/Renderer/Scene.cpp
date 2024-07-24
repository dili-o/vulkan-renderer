#include "Scene.hpp"

#include <vendor/imgui/stb_image.h>
#include <vendor/cglm/struct/affine.h>

#include "Core/Gltf.hpp"
#include "Core/Time.hpp"
#include "Core/File.hpp"
#include <vendor/tracy/tracy/Tracy.hpp>

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
        mat4s model = glms_scale_make(glms_vec3_mul(mesh_draw.scale, { global_scale, global_scale, -global_scale }));
        mesh_data.m = model;
        mesh_data.inverseM = glms_mat4_inv(glms_mat4_transpose(model));
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
        gpu_commands->bind_pass(gpu->get_swapchain_pass(), false);
        gpu_commands->set_scissor(nullptr);
        gpu_commands->set_viewport(nullptr);

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

            char* sampler_name = name_buffer.append_use_f("sampler_%u", sampler_index);

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
                buffer_name = name_buffer.append_use_f("buffer_%u", buffer_index);
            }

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

        // This is not needed anymore, free all temp memory after.
        //resource_name_buffer.shutdown();
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

        // Free scene buffers
        samplers.shutdown();
        images.shutdown();
        buffers.shutdown();

        // NOTE(marco): we can't destroy this sooner as textures and buffers
        // hold a pointer to the names stored here
        gltf_free(gltf_scene);
    }

    void glTFScene::prepare_draws(Renderer* renderer, StackAllocator* scratch_allocator) {

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

            FileReadResult vs_code = file_read_binary("D:/HelixEngine/Engine/assets/shaders/lights/light.vert.glsl", scratch_allocator);
            FileReadResult fs_code = file_read_binary("D:/HelixEngine/Engine/assets/shaders/lights/light.frag.glsl", scratch_allocator);
            FileReadResult gs_code = file_read_binary("D:/HelixEngine/Engine/assets/shaders/lights/light.geom.glsl", scratch_allocator);

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

        }
        // Create pipeline state
        PipelineCreation pipeline_creation;

        sizet cached_scratch_size = scratch_allocator->get_marker();

        StringBuffer path_buffer;
        path_buffer.init(1024, scratch_allocator);

        const char* vert_file = "pbr.vert.glsl";
        char* vert_path = path_buffer.append_use_f("%s%s", HELIX_SHADER_FOLDER, vert_file);
        FileReadResult vert_code = file_read_text(vert_path, scratch_allocator);

        const char* frag_file = "pbr.frag.glsl";
        char* frag_path = path_buffer.append_use_f("%s%s", HELIX_SHADER_FOLDER, frag_file);
        FileReadResult frag_code = file_read_text(frag_path, scratch_allocator);

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

        scratch_allocator->free_marker(cached_scratch_size);

        glTF::Scene& root_gltf_scene = gltf_scene.scenes[gltf_scene.scene];

        for (u32 node_index = 0; node_index < root_gltf_scene.nodes_count; ++node_index) {
            glTF::Node& node = gltf_scene.nodes[root_gltf_scene.nodes[node_index]];

            if (node.mesh == glTF::INVALID_INT_VALUE) {
                continue;
            }

            // TODO(marco): children

            glTF::Mesh& mesh = gltf_scene.meshes[node.mesh];

            vec3s node_scale{ 1.0f, 1.0f, 1.0f };
            if (node.scale_count != 0) {
                HASSERT(node.scale_count == 3);
                node_scale = vec3s{ node.scale[0], node.scale[1], node.scale[2] };
            }

            // Gltf primitives are conceptually submeshes.
            for (u32 primitive_index = 0; primitive_index < mesh.primitives_count; ++primitive_index) {
                MeshDraw mesh_draw{ };

                mesh_draw.scale = node_scale;

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
    
}// namespace Helix
