
#include "Renderer.hpp"

#include "Renderer/CommandBuffer.hpp"

#include "Core/Memory.hpp"
#include "Core/File.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "vendor/imgui/stb_image.h"
#include "vendor/imgui/imgui.h"

#include <vulkan/vk_enum_string_helper.h>

namespace Helix {
    // Program ////////////////////////////////////////////////////////////////
    u32 Program::get_pass_index(cstring name) {
        const u64 name_hash = hash_calculate(name);
        return name_hash_to_index.get(name_hash);
    }

    // MaterialCreation ///////////////////////////////////////////////////////
    MaterialCreation& MaterialCreation::reset() {
        program = nullptr;
        name = nullptr;
        render_index = ~0u;
        return *this;
    }

    MaterialCreation& MaterialCreation::set_program(Program* program_) {
        program = program_;
        return *this;
    }

    MaterialCreation& MaterialCreation::set_render_index(u32 render_index_) {
        render_index = render_index_;
        return *this;
    }

    MaterialCreation& MaterialCreation::set_name(cstring name_) {
        name = name_;
        return *this;
    }
    static TextureHandle create_texture_from_file(GpuDevice& gpu, cstring filename, cstring name, bool create_mipmaps) {

        if (filename) {
            int comp, width, height;
            uint8_t* image_data = stbi_load(filename, &width, &height, &comp, 4);
            if (!image_data) {
                HERROR("Error loading texture {}", filename);
                return k_invalid_texture;
            }

            u32 mip_levels = 1;
            if (create_mipmaps) {
                u32 w = width;
                u32 h = height;

                while (w > 1 && h > 1) {
                    w /= 2;
                    h /= 2;

                    ++mip_levels;
                }
            }

            TextureCreation creation;
            creation.set_data(image_data).set_format_type(VK_FORMAT_R8G8B8A8_UNORM, TextureType::Texture2D).set_flags(mip_levels, 0).set_size((u16)width, (u16)height, 1).set_name(name);

            Helix::TextureHandle new_texture = gpu.create_texture(creation);

            // IMPORTANT:
            // Free memory loaded from file, it should not matter!
            free(image_data);

            return new_texture;
        }

        return k_invalid_texture;
    }

    // Program //////////////////////////////////////////////////////////////////////

    ProgramCreation& ProgramCreation::reset() {
        num_creations = 0;
        name = nullptr;
        return *this;
    }

    ProgramCreation& ProgramCreation::add_pipeline(const PipelineCreation& pipeline) {
        creations[num_creations++] = pipeline;
        return *this;
    }

    ProgramCreation& ProgramCreation::set_name(cstring name_) {
        name = name_;
        return *this;
    }

    // Renderer /////////////////////////////////////////////////////////////////////

    u64 TextureResource::k_type_hash = 0;
    u64 BufferResource::k_type_hash = 0;
    u64 SamplerResource::k_type_hash = 0;
    u64 Program::k_type_hash = 0;
    u64 Material::k_type_hash = 0;

    static Renderer s_renderer;

    Renderer* Renderer::instance() {
        return &s_renderer;
    }

    void Renderer::init(const RendererCreation& creation) {

        HINFO("Renderer init");


        gpu = creation.gpu;

        width = gpu->swapchain_width;
        height = gpu->swapchain_height;

        textures.init(creation.allocator, k_textures_pool_size);
        buffers.init(creation.allocator, k_buffers_pool_size);
        samplers.init(creation.allocator, k_samplers_pool_size);
        programs.init(creation.allocator, k_pipelines_pool_size);
        materials.init(creation.allocator, 128);

        resource_cache.init(creation.allocator);

        // Init resource hashes
        TextureResource::k_type_hash = hash_calculate(TextureResource::k_type);
        BufferResource::k_type_hash = hash_calculate(BufferResource::k_type);
        SamplerResource::k_type_hash = hash_calculate(SamplerResource::k_type);
        Program::k_type_hash = hash_calculate(Program::k_type);
        Material::k_type_hash = hash_calculate(Material::k_type);


        resource_name_buffer.init(hkilo(30), creation.allocator);
        //s_texture_loader.renderer = this;
        //s_buffer_loader.renderer = this;
        //s_sampler_loader.renderer = this;

        const u32 gpu_heap_counts = gpu->get_memory_heap_count();
        gpu_heap_budgets.init(gpu->allocator, gpu_heap_counts, gpu_heap_counts);
    }

    void Renderer::shutdown() {

        resource_cache.shutdown(this);
        resource_name_buffer.shutdown();
        gpu_heap_budgets.shutdown();

        textures.shutdown();
        buffers.shutdown();
        samplers.shutdown();
        materials.shutdown();
        programs.shutdown();

        HINFO("Renderer shutdown");

        gpu->shutdown();
    }

    void Renderer::set_loaders(Helix::ResourceManager* manager) {

        //manager->set_loader(TextureResource::k_type, &s_texture_loader);
        //manager->set_loader(BufferResource::k_type, &s_buffer_loader);
        //manager->set_loader(SamplerResource::k_type, &s_sampler_loader);
    }

    void Renderer::begin_frame() {
        gpu->new_frame();
    }

    void Renderer::end_frame() {
        // Present
        gpu->present(nullptr);
    }

    void Renderer::imgui_draw() {

        // Print memory stats
        vmaGetHeapBudgets(gpu->vma_allocator, gpu_heap_budgets.data);

        sizet total_memory_used = 0;
        for (u32 i = 0; i < gpu->get_memory_heap_count(); ++i) {
            total_memory_used += gpu_heap_budgets[i].usage;
        }

        ImGui::Text("GPU Memory Total: %lluMB", total_memory_used / (1024 * 1024));
        ImGui::Text("FPS: %i", (u32)fps);
    }

    void Renderer::imgui_resources_draw()
    {
        if (ImGui::Begin("Renderer Resources")) {
            
            if (ImGui::BeginTabBar("MyTabBar", ImGuiTabBarFlags_None)) {
                if (ImGui::BeginTabItem("Textures")){
                    Helix::FlatHashMapIterator it = resource_cache.textures.iterator_begin();
                    while (it.is_valid()) {
                        Helix::TextureResource* texture = resource_cache.textures.get(it);
                        if (ImGui::TreeNode(texture->name)) {
                            ImGui::Text("Width: %d", texture->desc.width);
                            ImGui::Text("Height: %d", texture->desc.height);
                            ImGui::Text("Format: %s", string_VkFormat(texture->desc.format));
                            ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
                            ImGui::Image((ImTextureID)&texture->handle, { viewportPanelSize.x, viewportPanelSize.x });
                            ImGui::TreePop();
                        }
                        resource_cache.textures.iterator_advance(it);
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Buffers"))
                {
                    Helix::FlatHashMapIterator it = resource_cache.buffers.iterator_begin();
                    while (it.is_valid()) {
                        Helix::BufferResource* buffer = resource_cache.buffers.get(it);
                        if (ImGui::TreeNode(buffer->desc.name)) {
                            ImGui::Text("Size: %d", buffer->desc.size);
                            //ImGui::Text("Type: %s", string_VkBufferUsageFlags(buffer->desc.type_flags));
                            ImGui::TreePop();
                        }
                        resource_cache.buffers.iterator_advance(it);
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Samplers"))
                {
                    Helix::FlatHashMapIterator it = resource_cache.samplers.iterator_begin();
                    while (it.is_valid()) {
                        Helix::SamplerResource* sampler = resource_cache.samplers.get(it);
                        if (ImGui::TreeNode(sampler->desc.name)) {
                            
                            ImGui::Text("Min Filter: %s", string_VkFilter(sampler->desc.min_filter));
                            ImGui::Text("Mag Filter: %s", string_VkFilter(sampler->desc.mag_filter));
                            ImGui::Text("Mip Filter: %s", string_VkSamplerMipmapMode(sampler->desc.mip_filter));

                            ImGui::Text("Address Mode U: %s", string_VkSamplerAddressMode(sampler->desc.address_mode_u));
                            ImGui::Text("Address Mode V: %s", string_VkSamplerAddressMode(sampler->desc.address_mode_v));
                            ImGui::Text("Address Mode W: %s", string_VkSamplerAddressMode(sampler->desc.address_mode_w));
                            ImGui::TreePop();
                        }
                        resource_cache.samplers.iterator_advance(it);
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Pipelines"))
                {
                    Helix::FlatHashMapIterator it = resource_cache.programs.iterator_begin();
                    while (it.is_valid()) {
                        Helix::Program* program = resource_cache.programs.get(it);
                        if (ImGui::TreeNode(program->name)) {
                            for (u32 i = 0; i < program->passes.size; ++i) {
                                Pipeline* pipeline = gpu->access_pipeline(program->passes[i].pipeline);
                                ImGui::Text("%s", pipeline->name);
                            }
                            ImGui::TreePop();
                        }
                        resource_cache.programs.iterator_advance(it);
                    }
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }
        ImGui::End();
        
    }

    void Renderer::resize_swapchain(u32 width_, u32 height_) {
        gpu->resize((u16)width_, (u16)height_);

        width = gpu->swapchain_width;
        height = gpu->swapchain_height;
    }

    f32 Renderer::aspect_ratio() const {
        return gpu->swapchain_width * 1.f / gpu->swapchain_height;
    }

    BufferResource* Renderer::create_buffer(const BufferCreation& creation) {

        BufferResource* buffer = buffers.obtain();
        if (buffer) {
            BufferHandle handle = gpu->create_buffer(creation);
            buffer->handle = handle;
            buffer->name = creation.name;
            gpu->query_buffer(handle, buffer->desc);

            if (creation.name != nullptr) {
                resource_cache.buffers.insert(hash_calculate(creation.name), buffer);
            }else
                HWARN("Buffer Creation does not have a name");

            buffer->references = 1;

            return buffer;
        }
        return nullptr;
    }

    BufferResource* Renderer::create_buffer(VkBufferUsageFlags type, ResourceUsageType::Enum usage, u32 size, void* data, cstring name) {
        BufferCreation creation{ type, usage, size, 0, 0, data, name };
        return create_buffer(creation);
    }

    TextureResource* Renderer::create_texture(const TextureCreation& creation) {
        TextureResource* texture = textures.obtain();

        if (texture) {
            TextureHandle handle = gpu->create_texture(creation);
            texture->handle = handle;
            texture->name = creation.name;
            gpu->query_texture(handle, texture->desc);

            if (creation.name != nullptr) {
                resource_cache.textures.insert(hash_calculate(creation.name), texture);
            }else
                HWARN("Texture Creation does not have a name");

            texture->references = 1;

            return texture;
        }
        return nullptr;
    }

    TextureResource* Renderer::create_texture(cstring name, cstring filename, bool create_mipmaps) {
        TextureResource* texture = textures.obtain();

        if (texture) {
            TextureHandle handle = create_texture_from_file(*gpu, filename, name, create_mipmaps);
            texture->handle = handle;
            gpu->query_texture(handle, texture->desc);
            texture->references = 1;
            texture->name = name;

            resource_cache.textures.insert(hash_calculate(name), texture);

            return texture;
        }
        return nullptr;
    }

    SamplerResource* Renderer::create_sampler(const SamplerCreation& creation) {
        SamplerResource* sampler = samplers.obtain();
        if (sampler) {
            SamplerHandle handle = gpu->create_sampler(creation);
            sampler->handle = handle;
            sampler->name = creation.name;
            gpu->query_sampler(handle, sampler->desc);

            if (creation.name != nullptr) {
                resource_cache.samplers.insert(hash_calculate(creation.name), sampler);
            }else
                HWARN("Sampler Creation does not have a name");

            sampler->references = 1;

            return sampler;
        }
        return nullptr;
    }

    Program* Renderer::create_program(const ProgramCreation& creation) {
        Program* program = programs.obtain();
        if (program) {
            program->passes.init(gpu->allocator, creation.num_creations, creation.num_creations);
            program->name_hash_to_index.init(gpu->allocator, creation.num_creations);
            program->name = creation.name;

            StringBuffer pipeline_cache_path;
            pipeline_cache_path.init(1024, gpu->allocator);

            for (uint32_t i = 0; i < creation.num_creations; ++i) {
                ProgramPass& pass = program->passes[i];
                const PipelineCreation& pass_creation = creation.creations[i];
                if (pass_creation.name != nullptr) {
                    char* cache_path = pipeline_cache_path.append_use_f("%s%s.cache", HELIX_SHADER_FOLDER"cache/", pass_creation.name);
                    pass.pipeline = gpu->create_pipeline(pass_creation, cache_path);

                    resource_cache.programs.insert(hash_calculate(creation.name), program);
                }
                else {
                    HWARN("Pipeline Creation does not have a name");
                    pass.pipeline = gpu->create_pipeline(pass_creation);
                }

                program->name_hash_to_index.insert(hash_calculate(pass_creation.name), (u32)i);
                pass.descriptor_set_layout = gpu->get_descriptor_set_layout(pass.pipeline, 0);
            }

            pipeline_cache_path.shutdown();

            program->references = 1;

            return program;
        }
        HWARN("Renderer could not create a Program");
        return nullptr;
    }

    Material* Renderer::create_material(const MaterialCreation& creation) {
        Material* material = materials.obtain();
        if (material) {
            material->program = creation.program;
            material->name = creation.name;
            material->render_index = creation.render_index;

            if (creation.name != nullptr) {
                resource_cache.materials.insert(hash_calculate(creation.name), material);
            }else
                HWARN("Material Creation does not have a name");

            material->references = 1;

            return material;
        }
        return nullptr;
    }

    PipelineHandle Renderer::get_pipeline(Material* material, u32 pass_index) {
        HASSERT(material != nullptr);

        return material->program->passes[pass_index].pipeline;
    }

    /*DescriptorSetHandle Renderer::create_descriptor_set(CommandBuffer* command_buffer, Material* material, DescriptorSetCreation& ds_creation) {
        HASSERT(material != nullptr);

        DescriptorSetLayoutHandle set_layout = material->program->passes[0].descriptor_set_layout;

        ds_creation.set_layout(set_layout);

        return command_buffer->create_descriptor_set(ds_creation);
    }*/

    void Renderer::destroy_buffer(BufferResource* buffer) {
        if (!buffer) {
            return;
        }

        buffer->remove_reference();
        if (buffer->references) {
            return;
        }

        resource_cache.buffers.remove(hash_calculate(buffer->desc.name));
        gpu->destroy_buffer(buffer->handle);
        buffers.release(buffer);
    }

    void Renderer::destroy_texture(TextureResource* texture) {
        if (!texture) {
            return;
        }

        texture->remove_reference();
        if (texture->references) {
            return;
        }

        resource_cache.textures.remove(hash_calculate(texture->desc.name));
        gpu->destroy_texture(texture->handle);
        textures.release(texture);
    }

    void Renderer::destroy_sampler(SamplerResource* sampler) {
        if (!sampler) {
            return;
        }

        sampler->remove_reference();
        if (sampler->references) {
            return;
        }

        resource_cache.samplers.remove(hash_calculate(sampler->desc.name));
        gpu->destroy_sampler(sampler->handle);
        samplers.release(sampler);
    }

    void Renderer::destroy_program(Program* program) {
        if (!program) {
            return;
        }

        program->remove_reference();
        if (program->references) {
            return;
        }

        resource_cache.programs.remove(hash_calculate(program->name));

        for (u32 i = 0; i < program->passes.size; i++) {
            gpu->destroy_pipeline(program->passes[i].pipeline);
        }

        program->passes.shutdown();
        program->name_hash_to_index.shutdown();

        programs.release(program);
    }

    void Renderer::destroy_material(Material* material) {
        if (!material) {
            return;
        }

        material->remove_reference();
        if (material->references) {
            return;
        }

        resource_cache.materials.remove(hash_calculate(material->name));
        materials.release(material);
    }

    void* Renderer::map_buffer(BufferResource* buffer, u32 offset, u32 size) {

        MapBufferParameters cb_map = { buffer->handle, offset, size };
        return gpu->map_buffer(cb_map);
    }

    void Renderer::unmap_buffer(BufferResource* buffer) {

        if (buffer->desc.parent_handle.index == k_invalid_index) {
            MapBufferParameters cb_map = { buffer->handle, 0, 0 };
            gpu->unmap_buffer(cb_map);
        }
    }

    void Renderer::add_texture_to_update(Helix::TextureHandle texture) {
        std::lock_guard<std::mutex> guard(texture_update_mutex);

        textures_to_update[num_textures_to_update++] = texture;
    }

    //TODO:
    static void generate_mipmaps(Helix::Texture* texture, Helix::CommandBuffer* cb, bool from_transfer_queue) {
        using namespace Helix;

        if (texture->mip_level_count > 1) {
            util_add_image_barrier(cb->device, cb->vk_handle, texture->vk_image, from_transfer_queue ? RESOURCE_STATE_COPY_SOURCE : RESOURCE_STATE_COPY_SOURCE, RESOURCE_STATE_COPY_SOURCE, 0, 1, false);
        }

        i32 w = texture->width;
        i32 h = texture->height;

        for (int mip_index = 1; mip_index < texture->mip_level_count; ++mip_index) {
            util_add_image_barrier(cb->device, cb->vk_handle, texture->vk_image, RESOURCE_STATE_UNDEFINED, RESOURCE_STATE_COPY_DEST, mip_index, 1, false);

            VkImageBlit blit_region{ };
            blit_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit_region.srcSubresource.mipLevel = mip_index - 1;
            blit_region.srcSubresource.baseArrayLayer = 0;
            blit_region.srcSubresource.layerCount = 1;

            blit_region.srcOffsets[0] = { 0, 0, 0 };
            blit_region.srcOffsets[1] = { w, h, 1 };

            w /= 2;
            h /= 2;

            blit_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit_region.dstSubresource.mipLevel = mip_index;
            blit_region.dstSubresource.baseArrayLayer = 0;
            blit_region.dstSubresource.layerCount = 1;

            blit_region.dstOffsets[0] = { 0, 0, 0 };
            blit_region.dstOffsets[1] = { w, h, 1 };

            vkCmdBlitImage(cb->vk_handle, texture->vk_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture->vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit_region, VK_FILTER_LINEAR);

            // Prepare current mip for next level
            util_add_image_barrier(cb->device, cb->vk_handle, texture->vk_image, RESOURCE_STATE_COPY_DEST, RESOURCE_STATE_COPY_SOURCE, mip_index, 1, false);
        }

        // Transition
        if (from_transfer_queue) {
            util_add_image_barrier(cb->device, cb->vk_handle, texture->vk_image, (texture->mip_level_count > 1) ? RESOURCE_STATE_COPY_SOURCE : RESOURCE_STATE_COPY_DEST, RESOURCE_STATE_SHADER_RESOURCE, 0, texture->mip_level_count, false);
        }
        else {
            util_add_image_barrier(cb->device, cb->vk_handle, texture->vk_image, RESOURCE_STATE_UNDEFINED, RESOURCE_STATE_SHADER_RESOURCE, 0, texture->mip_level_count, false);
        }
    }


    void Renderer::add_texture_update_commands(u32 thread_id) {
        std::lock_guard<std::mutex> guard(texture_update_mutex);

        if (num_textures_to_update == 0) {
            return;
        }

        CommandBuffer* cb = gpu->get_command_buffer(thread_id, false);
        cb->begin();

        for (u32 i = 0; i < num_textures_to_update; ++i) {

            Texture* texture = gpu->access_texture(textures_to_update[i]);

            // TODO set the vk_image_layout of the texture
            util_add_image_barrier(cb->device, cb->vk_handle, texture->vk_image, RESOURCE_STATE_COPY_DEST, RESOURCE_STATE_COPY_SOURCE,
                0, 1, false, gpu->vulkan_transfer_queue_family, gpu->vulkan_main_queue_family, QueueType::CopyTransfer, QueueType::Graphics);

            generate_mipmaps(texture, cb, true);
        }

        // TODO: this is done before submitting to the queue in the device.
        //cb->end();
        gpu->queue_command_buffer(cb);

        num_textures_to_update = 0;
    }


    // ResourceCache //////////////////////////////////////
    void ResourceCache::init(Allocator* allocator) {
        // Init resources caching
        textures.init(allocator, 16);
        buffers.init(allocator, 16);
        samplers.init(allocator, 16);
        programs.init(allocator, 16);
        materials.init(allocator, 16);
    }

    void ResourceCache::shutdown(Renderer* renderer) {

        Helix::FlatHashMapIterator it = textures.iterator_begin();

        while (it.is_valid()) {
            Helix::TextureResource* texture = textures.get(it);
            renderer->destroy_texture(texture);

            textures.iterator_advance(it);
        }

        it = buffers.iterator_begin();

        while (it.is_valid()) {
            Helix::BufferResource* buffer = buffers.get(it);
            renderer->destroy_buffer(buffer);

            buffers.iterator_advance(it);
        }

        it = samplers.iterator_begin();

        while (it.is_valid()) {
            Helix::SamplerResource* sampler = samplers.get(it);
            renderer->destroy_sampler(sampler);

            samplers.iterator_advance(it);
        }

        it = materials.iterator_begin();

        while (it.is_valid()) {
            Helix::Material* material = materials.get(it);
            renderer->destroy_material(material);

            materials.iterator_advance(it);
        }

        it = programs.iterator_begin();

        while (it.is_valid()) {
            Helix::Program* program = programs.get(it);
            renderer->destroy_program(program);
            
            programs.iterator_advance(it);
        }

        textures.shutdown();
        buffers.shutdown();
        samplers.shutdown();
        materials.shutdown();
        programs.shutdown();
    }

} // namespace Helix
