#include "FrameGraph.hpp"

#include "Core/File.hpp"
#include "Core/Memory.hpp"
#include "Core/String.hpp"

#include "Renderer/CommandBuffer.hpp"
#include "Renderer/GPUDevice.hpp"

#include <vendor/json.hpp>

//#include <string>

namespace Helix {

    static FrameGraphResourceType string_to_resource_type(cstring input_type) {
        if (strcmp(input_type, "texture") == 0) {
            return FrameGraphResourceType_Texture;
        }

        if (strcmp(input_type, "attachment") == 0) {
            return FrameGraphResourceType_Attachment;
        }

        if (strcmp(input_type, "buffer") == 0) {
            return FrameGraphResourceType_Buffer;
        }

        if (strcmp(input_type, "reference") == 0) {
            // This is used for resources that need to create an edge but are not actually
            // used by the render pass
            return FrameGraphResourceType_Reference;
        }

        HASSERT_MSG(false, "Invalid FrameGraphResourceType");
        return FrameGraphResourceType_Invalid;
    }

    RenderPassOperation::Enum string_to_render_pass_operation(cstring op) {
        if (strcmp(op, "clear") == 0) {
            return RenderPassOperation::Clear;
        }
        else if (strcmp(op, "load") == 0) {
            return RenderPassOperation::Load;
        }

        HASSERT_MSG(false, "Invalid Render pass operation");
        return RenderPassOperation::DontCare;
    }
    // Sets the output edges of node
    static void compute_edges(FrameGraph* frame_graph, FrameGraphNode* node, u32 node_index) {
        for (u32 r = 0; r < node->inputs.size; ++r) {
            FrameGraphResource* input_resource = frame_graph->access_resource(node->inputs[r]);

            FrameGraphResource* output_resource = frame_graph->get_resource(input_resource->name);
            if (output_resource == nullptr && !input_resource->resource_info.external) {
                // TODO(marco): external resources
                HASSERT_MSG(false, "Requested resource is not produced by any node and is not external.");
                continue;
            }

            input_resource->producer        = output_resource->producer;
            input_resource->resource_info   = output_resource->resource_info;
            input_resource->output_handle   = output_resource->output_handle;

            FrameGraphNode* parent_node = frame_graph->access_node(input_resource->producer);

            parent_node->edges.push(frame_graph->nodes[node_index]);
        }
    }

    static void create_framebuffer(FrameGraph* frame_graph, FrameGraphNode* node) {
        FramebufferCreation framebuffer_creation{ };
        framebuffer_creation.render_pass = node->render_pass;
        framebuffer_creation.set_name(node->name);

        u32 width = 0;
        u32 height = 0;
        f32 scale_width = 0.f;
        f32 scale_height = 0.f;

        for (u32 r = 0; r < node->outputs.size; ++r) {
            FrameGraphResource* resource = frame_graph->access_resource(node->outputs[r]);

            FrameGraphResourceInfo& info = resource->resource_info;

            if (resource->type != FrameGraphResourceType_Attachment) {
                continue;
            }

            if (width == 0) {
                width = info.texture.width;
                scale_width = info.texture.scale_width > 0.f ? info.texture.scale_width : 1.f;
            }
            else {
                HASSERT(width == info.texture.width);
            }

            if (height == 0) {
                height = info.texture.height;
                scale_height = info.texture.scale_height > 0.f ? info.texture.scale_height : 1.f;
            }
            else {
                HASSERT(height == info.texture.height);
            }

            if (info.texture.format == VK_FORMAT_D32_SFLOAT) {
                framebuffer_creation.set_depth_stencil_texture(info.texture.handle);
            }
            else {
                framebuffer_creation.add_render_texture(info.texture.handle);
            }
        }

        for (u32 r = 0; r < node->inputs.size; ++r) {
            FrameGraphResource* input_resource = frame_graph->access_resource(node->inputs[r]);

            if (input_resource->type != FrameGraphResourceType_Attachment) {
                continue;
            }

            FrameGraphResource* resource = frame_graph->get_resource(input_resource->name);

            FrameGraphResourceInfo& info = resource->resource_info;

            input_resource->resource_info.texture.handle = info.texture.handle;

            if (width == 0) {
                width = info.texture.width;
            }
            else {
                HASSERT(width == info.texture.width);
            }

            if (height == 0) {
                height = info.texture.height;
            }
            else {
                HASSERT(height == info.texture.height);
            }

            if (info.texture.format == VK_FORMAT_D32_SFLOAT) {
                framebuffer_creation.set_depth_stencil_texture(info.texture.handle);
            }
            else {
                framebuffer_creation.add_render_texture(info.texture.handle);
            }
        }

        framebuffer_creation.width = width;
        framebuffer_creation.height = height;
        framebuffer_creation.set_scaling(scale_width, scale_height, 1);

        node->framebuffer = frame_graph->builder->device->create_framebuffer(framebuffer_creation);

    }

    static void create_render_pass(FrameGraph* frame_graph, FrameGraphNode* node) {
        RenderPassCreation render_pass_creation{ };
        render_pass_creation.set_name(node->name);

        // NOTE(marco): first create the outputs, then we can patch the input resources
        // with the right handles
        for (u32 i = 0; i < node->outputs.size; ++i) {
            FrameGraphResource* output_resource = frame_graph->access_resource(node->outputs[i]);

            FrameGraphResourceInfo& info = output_resource->resource_info;

            if (output_resource->type == FrameGraphResourceType_Attachment) {
                if (info.texture.format == VK_FORMAT_D32_SFLOAT) {
                    render_pass_creation.set_depth_stencil(info.texture.format, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

                    render_pass_creation.depth_operation = RenderPassOperation::Clear;
                }
                else {
                    render_pass_creation.add_attachment(info.texture.format, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, info.texture.load_op);
                }
            }
        }

        for (u32 i = 0; i < node->inputs.size; ++i) {
            FrameGraphResource* input_resource = frame_graph->access_resource(node->inputs[i]);

            FrameGraphResourceInfo& info = input_resource->resource_info;

            if (input_resource->type == FrameGraphResourceType_Attachment) {
                if (info.texture.format == VK_FORMAT_D32_SFLOAT) {
                    render_pass_creation.set_depth_stencil(info.texture.format, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

                    render_pass_creation.depth_operation = RenderPassOperation::Load;
                }
                else {
                    render_pass_creation.add_attachment(info.texture.format, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, RenderPassOperation::Load);
                }
            }
        }

        // TODO(marco): make sure formats are valid for attachment
        node->render_pass = frame_graph->builder->device->create_render_pass(render_pass_creation);
    }

    // FrameGraphRenderPassCache /////////////////////////////////////////////////////////////

    void FrameGraphRenderPassCache::init(Allocator* allocator)
    {
        render_pass_map.init(allocator, FrameGraphBuilder::k_max_render_pass_count);
    }

    void FrameGraphRenderPassCache::shutdown()
    {
        render_pass_map.shutdown();
    }

    // FrameGraphResourceCache /////////////////////////////////////////////////////////////

    void FrameGraphResourceCache::init(Allocator* allocator, GpuDevice* device_)
    {
        device = device_;

        resources.init(allocator, FrameGraphBuilder::k_max_resources_count);
        resource_map.init(allocator, FrameGraphBuilder::k_max_resources_count);
    }

    void FrameGraphResourceCache::shutdown()
    {
        FlatHashMapIterator it = resource_map.iterator_begin();
        while (it.is_valid()) {

            u32 resource_index = resource_map.get(it);
            FrameGraphResource* resource = resources.get(resource_index);

            if (resource->type == FrameGraphResourceType_Texture || resource->type == FrameGraphResourceType_Attachment) {
                Texture* texture = device->access_texture(resource->resource_info.texture.handle);
                device->destroy_texture(texture->handle);
            }
            else if (resource->type == FrameGraphResourceType_Buffer) {
                // TODO: Right now buffer resources in framegraphs are handled separately by the Scene class
                //Buffer* buffer = device->access_buffer(resource->resource_info.buffer.handle);
                //device->destroy_buffer(buffer->handle);
            }

            resource_map.iterator_advance(it);
        }

        resources.free_all_resources();
        resources.shutdown();
        resource_map.shutdown();
    }

    // FrameGraphNodeCache /////////////////////////////////////////////////////////////

    void FrameGraphNodeCache::init(Allocator* allocator, GpuDevice* device_)
    {
        device = device_;

        nodes.init(allocator, FrameGraphBuilder::k_max_nodes_count, sizeof(FrameGraphNode));
        node_map.init(allocator, FrameGraphBuilder::k_max_nodes_count);
    }

    void FrameGraphNodeCache::shutdown()
    {
        nodes.free_all_resources();
        nodes.shutdown();
        node_map.shutdown();
    }

    // FrameGraphBuilder /////////////////////////////////////////////////////////////

    void FrameGraphBuilder::init(GpuDevice* device_) {
        device = device_;

        allocator = device->allocator;

        resource_cache.init(allocator, device);
        node_cache.init(allocator, device);
        render_pass_cache.init(allocator);
    }

    void FrameGraphBuilder::shutdown() {
        resource_cache.shutdown();
        node_cache.shutdown();
        render_pass_cache.shutdown();
    }

    void FrameGraphBuilder::register_render_pass(cstring name, FrameGraphRenderPass* render_pass)
    {
        u64 key = hash_calculate(name);

        FlatHashMapIterator it = render_pass_cache.render_pass_map.find(key);
        if (it.is_valid()) {
            return;
        }

        render_pass_cache.render_pass_map.insert(key, render_pass);

        it = node_cache.node_map.find(key);
        HASSERT(it.is_valid());

        FrameGraphNode* node = (FrameGraphNode*)node_cache.nodes.access_resource(node_cache.node_map.get(it));
        node->graph_render_pass = render_pass;
    }

    FrameGraphResourceHandle FrameGraphBuilder::create_node_output(const FrameGraphResourceCreation& creation, FrameGraphNodeHandle producer)
    {
        FrameGraphResourceHandle resource_handle{ k_invalid_index };
        resource_handle.index = resource_cache.resources.obtain_resource();

        if (resource_handle.index == k_invalid_index) {
            return resource_handle;
        }

        FrameGraphResource* resource = resource_cache.resources.get(resource_handle.index);
        resource->name = creation.name;
        resource->type = creation.type;

        if (creation.type != FrameGraphResourceType_Reference) {
            resource->resource_info = creation.resource_info;
            resource->output_handle = resource_handle;
            resource->producer = producer;
            resource->ref_count = 0;

            resource_cache.resource_map.insert(hash_bytes((void*)resource->name, strlen(creation.name)), resource_handle.index);
        }

        return resource_handle;
    }

    FrameGraphResourceHandle FrameGraphBuilder::create_node_input(const FrameGraphResourceCreation& creation)
    {
        FrameGraphResourceHandle resource_handle = { k_invalid_index };

        resource_handle.index = resource_cache.resources.obtain_resource();

        if (resource_handle.index == k_invalid_index) {
            return resource_handle;
        }

        FrameGraphResource* resource = resource_cache.resources.get(resource_handle.index);

        resource->resource_info = { };
        resource->producer.index = k_invalid_index;
        resource->output_handle.index = k_invalid_index;
        resource->type = creation.type;
        resource->name = creation.name;
        resource->ref_count = 0;

        return resource_handle;
    }

    FrameGraphNodeHandle FrameGraphBuilder::create_node(const FrameGraphNodeCreation& creation)
    {
        FrameGraphNodeHandle node_handle{ k_invalid_index };
        node_handle.index = node_cache.nodes.obtain_resource();

        if (node_handle.index == k_invalid_index) {
            return node_handle;
        }

        FrameGraphNode* node = (FrameGraphNode*)node_cache.nodes.access_resource(node_handle.index);
        node->name = creation.name;
        node->enabled = creation.enabled;
        node->compute = creation.compute;
        node->inputs.init(allocator, creation.input_creations.size);
        node->outputs.init(allocator, creation.output_creations.size);
        node->edges.init(allocator, creation.output_creations.size);
        node->framebuffer = k_invalid_framebuffer;
        node->render_pass = { k_invalid_index };

        node_cache.node_map.insert(hash_bytes((void*)node->name, strlen(node->name)), node_handle.index);

        // NOTE(marco): first create the outputs, then we can patch the input resources
        // with the right handles
        for (u32 i = 0; i < creation.output_creations.size; ++i) {
            const FrameGraphResourceCreation& output_creation = creation.output_creations[i];

            FrameGraphResourceHandle output = create_node_output(output_creation, node_handle);

            node->outputs.push(output);
        }

        for (u32 i = 0; i < creation.input_creations.size; ++i) {
            const FrameGraphResourceCreation& input_creation = creation.input_creations[i];

            FrameGraphResourceHandle input_handle = create_node_input(input_creation);

            node->inputs.push(input_handle);
        }

        return node_handle;
    }

    FrameGraphNode* FrameGraphBuilder::get_node(cstring name) {
        FlatHashMapIterator it = node_cache.node_map.find(hash_calculate(name));
        if (it.is_invalid()) {
            return nullptr;
        }

        FrameGraphNode* node = (FrameGraphNode*)node_cache.nodes.access_resource(node_cache.node_map.get(it));

        return node;
    }

    FrameGraphNode* FrameGraphBuilder::access_node(FrameGraphNodeHandle handle) {
        FrameGraphNode* node = (FrameGraphNode*)node_cache.nodes.access_resource(handle.index);

        return node;
    }

    FrameGraphResource* FrameGraphBuilder::get_resource(cstring name) {
        FlatHashMapIterator it = resource_cache.resource_map.find(hash_calculate(name));
        if (it.is_invalid()) {
            return nullptr;
        }

        FrameGraphResource* resource = resource_cache.resources.get(resource_cache.resource_map.get(it));

        return resource;
    }

    FrameGraphResource* FrameGraphBuilder::access_resource(FrameGraphResourceHandle handle) {
        FrameGraphResource* resource = resource_cache.resources.get(handle.index);

        return resource;
    }

    // FrameGraph /////////////////////////////////////////////////////////////

    void FrameGraph::init(FrameGraphBuilder* builder_) {
        allocator = &MemoryService::instance()->system_allocator;

        linear_allocator.init(hmega(1));

        builder = builder_;

        nodes.init(allocator, FrameGraphBuilder::k_max_nodes_count);
    }

    void FrameGraph::shutdown() {
        for (u32 i = 0; i < nodes.size; ++i) {
            FrameGraphNodeHandle handle = nodes[i];
            FrameGraphNode* node = builder->access_node(handle);

            if (!node->compute) {
                builder->device->destroy_render_pass(node->render_pass);
                builder->device->destroy_framebuffer(node->framebuffer);
            }
            node->inputs.shutdown();
            node->outputs.shutdown();
            node->edges.shutdown();
        }

        nodes.shutdown();

        linear_allocator.shutdown();
    }

    void FrameGraph::parse(cstring file_path, StackAllocator* temp_allocator) {
        // TODO: Check for external resources

        using json = nlohmann::json;

        if (!file_exists(file_path)) {
            HERROR("Cannot find file {}", file_path);
            return;
        }

        sizet current_allocator_marker = temp_allocator->get_marker();

        FileReadResult read_result = file_read_text(file_path, temp_allocator);

        json graph_data = json::parse(read_result.data);

        StringBuffer string_buffer;
        string_buffer.init(1024, &linear_allocator);

        std::string name_value = graph_data.value("name", "");
        name = string_buffer.append_use_f("%s", name_value.c_str());

        json passes = graph_data["passes"];
        for (sizet i = 0; i < passes.size(); ++i) {
            json pass = passes[i];

            json pass_inputs = pass["inputs"];
            json pass_outputs = pass["outputs"];

            FrameGraphNodeCreation node_creation{ };
            node_creation.input_creations.init(temp_allocator, (u32)pass_inputs.size());
            node_creation.output_creations.init(temp_allocator, (u32)pass_outputs.size());

            node_creation.compute = pass.value("type", "").compare("compute") == 0;

            for (sizet ii = 0; ii < pass_inputs.size(); ++ii) {
                json pass_input = pass_inputs[ii];

                FrameGraphResourceCreation input_creation{ };

                std::string input_type = pass_input.value("type", "");
                HASSERT(!input_type.empty());

                std::string input_name = pass_input.value("name", "");
                HASSERT(!input_name.empty());

                input_creation.type = string_to_resource_type(input_type.c_str());

                if (pass_input.contains("external"))
                {
                    input_creation.resource_info.external = pass_input["external"];
                }
                else {
                    input_creation.resource_info.external = false;
                }
                

                input_creation.name = string_buffer.append_use_f("%s", input_name.c_str());

                node_creation.input_creations.push(input_creation);
            }

            for (sizet oi = 0; oi < pass_outputs.size(); ++oi) {
                json pass_output = pass_outputs[oi];

                FrameGraphResourceCreation output_creation{ };

                std::string output_type = pass_output.value("type", "");
                HASSERT(!output_type.empty());

                std::string output_name = pass_output.value("name", "");
                HASSERT(!output_name.empty());

                output_creation.type = string_to_resource_type(output_type.c_str());
                output_creation.name = string_buffer.append_use_f("%s", output_name.c_str());

                if (pass_output.contains("external"))
                {
                    output_creation.resource_info.external = pass_output["external"];
                }
                else {
                    output_creation.resource_info.external = false;
                }

                switch (output_creation.type) {
                case FrameGraphResourceType_Texture:
                {

                }break;
                case FrameGraphResourceType_Attachment:
                {
                    std::string format = pass_output.value("format", "");
                    HASSERT(!format.empty());

                    output_creation.resource_info.texture.format = util_string_to_vk_format(format.c_str());

                    std::string load_op = pass_output.value("load_operation", "");
                    HASSERT(!load_op.empty());

                    output_creation.resource_info.texture.load_op = string_to_render_pass_operation(load_op.c_str());

                    json resolution = pass_output["resolution"];
                    json scaling = pass_output["resolution_scale"];

                    if (resolution.is_array()) {
                        output_creation.resource_info.texture.width = resolution[0];
                        output_creation.resource_info.texture.height = resolution[1];
                        output_creation.resource_info.texture.depth = 1;
                        output_creation.resource_info.texture.scale_width = 0.f;
                        output_creation.resource_info.texture.scale_height = 0.f;
                    }
                    else if (scaling.is_array()) {
                        output_creation.resource_info.texture.width = 0;
                        output_creation.resource_info.texture.height = 0;
                        output_creation.resource_info.texture.depth = 1;
                        output_creation.resource_info.texture.scale_width = scaling[0];
                        output_creation.resource_info.texture.scale_height = scaling[1];
                    }
                    else {
                        output_creation.resource_info.texture.width = 0;
                        output_creation.resource_info.texture.height = 0;
                        output_creation.resource_info.texture.depth = 1;
                        output_creation.resource_info.texture.scale_width = 1.f;
                        output_creation.resource_info.texture.scale_height = 1.f;
                    }

                    output_creation.resource_info.texture.compute = node_creation.compute;

                } break;
                case FrameGraphResourceType_Buffer:
                {
                    // TODO(marco)
                    //HASSERT_MSG(false, "ResourceType_Buffer not currently supported");
                } break;
                }

                node_creation.output_creations.push(output_creation);
            }

            name_value = pass.value("name", "");
            HASSERT(!name_value.empty());

            bool enabled = pass.value("enabled", true);

            node_creation.name = string_buffer.append_use_f("%s", name_value.c_str());
            node_creation.enabled = enabled;

            FrameGraphNodeHandle node_handle = builder->create_node(node_creation);
            nodes.push(node_handle);
        }

        temp_allocator->free_marker(current_allocator_marker);
    }

    void FrameGraph::enable_render_pass(cstring render_pass_name) {
        FrameGraphNode* node = builder->get_node(render_pass_name);
        node->enabled = true;
    }

    void FrameGraph::disable_render_pass(cstring render_pass_name) {
        FrameGraphNode* node = builder->get_node(render_pass_name);
        node->enabled = false;
    }
   
    void FrameGraph::compile() {
        // TODO(marco)
        // - check that input has been produced by a different node
        // - cull inactive nodes

        for (u32 i = 0; i < nodes.size; ++i) {
            FrameGraphNode* node = builder->access_node(nodes[i]);

            // NOTE(marco): we want to clear all edges first, then populate them. If we clear them inside the loop
            // below we risk clearing the list after it has already been used by one of the child nodes
            node->edges.clear();
        }

        for (u32 i = 0; i < nodes.size; ++i) {
            FrameGraphNode* node = builder->access_node(nodes[i]);
            if (!node->enabled) {
                continue;
            }
            compute_edges(this, node, i);
        }

        Array<FrameGraphNodeHandle> sorted_nodes;
        sorted_nodes.init(&linear_allocator, nodes.size);

        Array<u8> visited;
        visited.init(&linear_allocator, nodes.size, nodes.size);
        memset(visited.data, 0, sizeof(bool) * nodes.size);

        Array<FrameGraphNodeHandle> stack;
        stack.init(&linear_allocator, nodes.size);

        // Topological sorting
        for (u32 n = 0; n < nodes.size; ++n) {
            FrameGraphNode* node = builder->access_node(nodes[n]);
            if (!node->enabled) {
                continue;
            }

            stack.push(nodes[n]);

            while (stack.size > 0) {
                FrameGraphNodeHandle node_handle = stack.back();

                if (visited[node_handle.index] == 2) {
                    stack.pop();

                    continue;
                }

                if (visited[node_handle.index] == 1) {
                    visited[node_handle.index] = 2; // added

                    sorted_nodes.push(node_handle);

                    stack.pop();

                    continue;
                }

                visited[node_handle.index] = 1; // visited

                FrameGraphNode* node = builder->access_node(node_handle);

                // Leaf node
                if (node->edges.size == 0) {
                    continue;
                }

                for (u32 r = 0; r < node->edges.size; ++r) {
                    FrameGraphNodeHandle child_handle = node->edges[r];

                    if (!visited[child_handle.index]) {
                        stack.push(child_handle);
                    }
                }
            }
        }

        HASSERT(sorted_nodes.size == nodes.size);

        

        nodes.clear();

        for (i32 i = sorted_nodes.size - 1; i >= 0; --i) {
            nodes.push(sorted_nodes[i]);
        }

        HDEBUG("Sorted nodes=====================================");
        for (u32 i = 0; i < nodes.size; i++) {
            FrameGraphNode* node = builder->access_node(nodes[i]);
            HDEBUG(node->name);
        }
        HDEBUG("Sorted nodes=====================================");

        visited.shutdown();
        stack.shutdown();
        sorted_nodes.shutdown();

        // NOTE(marco): allocations and deallocations are used for verification purposes only
        sizet resource_count = builder->resource_cache.resources.used_indices;
        Array<FrameGraphNodeHandle> allocations;
        allocations.init(&linear_allocator, (u32)resource_count, (u32)resource_count);
        for (u32 i = 0; i < resource_count; ++i) {
            allocations[i].index = k_invalid_index;
        }

        Array<FrameGraphNodeHandle> deallocations;
        deallocations.init(&linear_allocator, (u32)resource_count, (u32)resource_count);
        for (u32 i = 0; i < resource_count; ++i) {
            deallocations[i].index = k_invalid_index;
        }

        Array<TextureHandle> free_list;
        free_list.init(&linear_allocator, (u32)resource_count);

        size_t peak_memory = 0;
        size_t instant_memory = 0;

        for (u32 i = 0; i < nodes.size; ++i) {
            FrameGraphNode* node = builder->access_node(nodes[i]);
            if (!node->enabled) {
                continue;
            }

            for (u32 j = 0; j < node->inputs.size; ++j) {
                FrameGraphResource* input_resource = builder->access_resource(node->inputs[j]);
                FrameGraphResource* resource = builder->access_resource(input_resource->output_handle);

                resource->ref_count++;
            }
        }

        for (u32 i = 0; i < nodes.size; ++i) {
            FrameGraphNode* node = builder->access_node(nodes[i]);
            if (!node->enabled) {
                continue;
            }

            for (u32 j = 0; j < node->outputs.size; ++j) {
                u32 resource_index = node->outputs[j].index;
                FrameGraphResource* resource = builder->access_resource(node->outputs[j]);

                if (!resource->resource_info.external && allocations[resource_index].index == k_invalid_index) {
                    HASSERT(deallocations[resource_index].index == k_invalid_index)
                        allocations[resource_index] = nodes[i];

                    if (resource->type == FrameGraphResourceType_Attachment) {
                        FrameGraphResourceInfo& info = resource->resource_info;

                        // Resolve texture size if needed
                        if (info.texture.width == 0 || info.texture.height == 0) {
                            info.texture.width = (u32)(builder->device->swapchain_width * info.texture.scale_width);
                            info.texture.height = (u32)(builder->device->swapchain_height * info.texture.scale_height);
                        }

                        TextureFlags::Mask texture_creation_flags = info.texture.compute ? (TextureFlags::Mask)(TextureFlags::RenderTarget_mask | TextureFlags::Compute_mask) : TextureFlags::RenderTarget_mask;

                        if (free_list.size > 0) {
                            // TODO(marco): find best fit
                            TextureHandle alias_texture = free_list.back();
                            free_list.pop();

                            TextureCreation texture_creation{ };
                            texture_creation.set_data(nullptr).set_alias(alias_texture).set_name(resource->name).set_format_type(info.texture.format, TextureType::Enum::Texture2D).set_size(info.texture.width, info.texture.height, info.texture.depth).set_flags(1, TextureFlags::RenderTarget_mask);
                            TextureHandle handle = builder->device->create_texture(texture_creation);

                            info.texture.handle = handle;
                        }
                        else {
                            TextureCreation texture_creation{ };
                            texture_creation.set_data(nullptr).set_name(resource->name).set_format_type(info.texture.format, TextureType::Enum::Texture2D).set_size(info.texture.width, info.texture.height, info.texture.depth).set_flags(1, TextureFlags::RenderTarget_mask);
                            TextureHandle handle = builder->device->create_texture(texture_creation);

                            info.texture.handle = handle;
                        }
                    }

                    HDEBUG("Output {} allocated on node {}", resource->name, nodes[i].index);
                }
            }

            for (u32 j = 0; j < node->inputs.size; ++j) {
                FrameGraphResource* input_resource = builder->access_resource(node->inputs[j]);

                u32 resource_index = input_resource->output_handle.index;
                FrameGraphResource* resource = builder->access_resource(input_resource->output_handle);

                resource->ref_count--;

                if (!resource->resource_info.external && resource->ref_count == 0) {
                    HASSERT(deallocations[resource_index].index == k_invalid_index);
                    deallocations[resource_index] = nodes[i];

                    if (resource->type == FrameGraphResourceType_Attachment || resource->type == FrameGraphResourceType_Texture) {
                        free_list.push(resource->resource_info.texture.handle);
                    }

                    HDEBUG("Output {} deallocated on node {}", resource->name, nodes[i].index);
                }
            }
        }

        allocations.shutdown();
        deallocations.shutdown();
        free_list.shutdown();

        for (u32 i = 0; i < nodes.size; ++i) {
            FrameGraphNode* node = builder->access_node(nodes[i]);
            if (!node->enabled) {
                continue;
            }

            if (node->compute) {
                continue;
            }

            if (node->render_pass.index == k_invalid_index) {
                create_render_pass(this, node);
            }

            if (node->framebuffer.index == k_invalid_index) {
                create_framebuffer(this, node);
            }
        }
    }

    void FrameGraph::add_ui() {
        for (u32 n = 0; n < nodes.size; ++n) {
            FrameGraphNode* node = builder->access_node(nodes[n]);
            if (!node->enabled) {
                continue;
            }

            node->graph_render_pass->add_ui();
        }
    }

    void FrameGraph::render(u32 current_frame_index, CommandBuffer* gpu_commands, Scene* scene)
    {
        gpu_commands->push_marker("Frame");

        for (u32 n = 0; n < nodes.size; ++n) {
            FrameGraphNode* node = builder->access_node(nodes[n]);
            if (!node->enabled) {
                continue;
            }

            if (node->compute) {
                gpu_commands->push_marker(node->name);

                for (u32 i = 0; i < node->inputs.size; ++i) {
                    FrameGraphResource* input_resource = builder->access_resource(node->inputs[i]);
                    FrameGraphResource* resource = builder->access_resource(input_resource->output_handle);

                    if (resource == nullptr || resource->resource_info.external) {
                        continue;
                    }

                    if (input_resource->type == FrameGraphResourceType_Texture) {
                        Texture* texture = gpu_commands->device->access_texture(resource->resource_info.texture.handle);

                        util_add_image_barrier(gpu_commands->device, gpu_commands->vk_handle, texture, RESOURCE_STATE_SHADER_RESOURCE, 0, 1, TextureFormat::has_depth(texture->vk_format));
                    }
                    else if (input_resource->type == FrameGraphResourceType_Attachment) {
                        // TODO: what to do with attachments ?
                        continue;
                    }
                }

                for (u32 o = 0; o < node->outputs.size; ++o) {
                    FrameGraphResource* resource = builder->access_resource(node->outputs[o]);

                    if (resource->type == FrameGraphResourceType_Attachment) {
                        Texture* texture = gpu_commands->device->access_texture(resource->resource_info.texture.handle);

                        if (TextureFormat::has_depth(texture->vk_format)) {
                            // Is this supported even ?
                            HASSERT(false);
                        }
                        else {
                            util_add_image_barrier(gpu_commands->device, gpu_commands->vk_handle, texture, RESOURCE_STATE_UNORDERED_ACCESS, 0, 1, false);
                        }
                    }
                }

                node->graph_render_pass->pre_render(gpu_commands, scene);
                node->graph_render_pass->render(gpu_commands, scene);
                node->graph_render_pass->post_render(current_frame_index, gpu_commands, this);

                gpu_commands->pop_marker();
            }
            else {
                gpu_commands->push_marker(node->name);
                // TODO(marco): add clear colour to json
                gpu_commands->clear(0.3f, 0.3f, 0.3f, 1.f);
                gpu_commands->clear_depth_stencil(1.0f, 0);

                u32 width = 0;
                u32 height = 0;

                for (u32 i = 0; i < node->inputs.size; ++i) {
                    FrameGraphResource* input_resource = builder->access_resource(node->inputs[i]);
                    FrameGraphResource* resource = builder->access_resource(input_resource->output_handle);

                    if (input_resource->type == FrameGraphResourceType_Texture) {
                        Texture* texture = gpu_commands->device->access_texture(resource->resource_info.texture.handle);

                        util_add_image_barrier(gpu_commands->device, gpu_commands->vk_handle, texture, /*RESOURCE_STATE_PIXEL_SHADER_RESOURCE*/RESOURCE_STATE_SHADER_RESOURCE, 0, 1, TextureFormat::has_depth(texture->vk_format));
                    }
                    else if (input_resource->type == FrameGraphResourceType_Attachment) {
                        Texture* texture = gpu_commands->device->access_texture(resource->resource_info.texture.handle);

                        width = texture->width;
                        height = texture->height;

                        // For textures that are read-write check if a transition is needed.
                        if (!TextureFormat::has_depth_or_stencil(texture->vk_format)) {
                            util_add_image_barrier(gpu_commands->device, gpu_commands->vk_handle, texture, RESOURCE_STATE_RENDER_TARGET, 0, 1, false);
                        }
                        else {
                            util_add_image_barrier(gpu_commands->device, gpu_commands->vk_handle, texture, RESOURCE_STATE_DEPTH_WRITE, 0, 1, true);
                        }
                    }
                }

                for (u32 o = 0; o < node->outputs.size; ++o) {
                    FrameGraphResource* resource = builder->access_resource(node->outputs[o]);

                    if (resource->type == FrameGraphResourceType_Attachment) {
                        Texture* texture = gpu_commands->device->access_texture(resource->resource_info.texture.handle);

                        width = texture->width;
                        height = texture->height;

                        if (TextureFormat::has_depth(texture->vk_format)) {
                            util_add_image_barrier(gpu_commands->device, gpu_commands->vk_handle, texture, RESOURCE_STATE_DEPTH_WRITE, 0, 1, true);
                        }
                        else {
                            util_add_image_barrier(gpu_commands->device, gpu_commands->vk_handle, texture, RESOURCE_STATE_RENDER_TARGET, 0, 1, false);
                        }
                    }
                }

                Rect2DInt scissor{ 0, 0,(u16)width, (u16)height };
                gpu_commands->set_scissor(&scissor);

                Viewport viewport{ };
                viewport.rect = { 0, 0, (u16)width, (u16)height };
                viewport.min_depth = 0.0f;
                viewport.max_depth = 1.0f;

                gpu_commands->set_viewport(&viewport);

                node->graph_render_pass->pre_render(gpu_commands, scene);

                gpu_commands->bind_pass(node->render_pass, node->framebuffer, false);

                node->graph_render_pass->render(gpu_commands, scene);

                gpu_commands->end_current_render_pass();

                node->graph_render_pass->post_render(current_frame_index, gpu_commands, this);

                gpu_commands->pop_marker();
            }

            
        }
    }

    void FrameGraph::on_resize(GpuDevice& gpu, u32 new_width, u32 new_height) {
        for (u32 n = 0; n < nodes.size; ++n) {
            FrameGraphNode* node = builder->access_node(nodes[n]);
            if (!node->enabled) {
                continue;
            }

            node->graph_render_pass->on_resize(gpu, this, new_width, new_height);

            gpu.resize_output_textures(node->framebuffer, new_width, new_height);
        }
    }

    FrameGraphNode* FrameGraph::get_node(cstring name) {
        return builder->get_node(name);
    }

    FrameGraphNode* FrameGraph::access_node(FrameGraphNodeHandle handle) {
        return builder->access_node(handle);
    }

    FrameGraphResource* FrameGraph::get_resource(cstring name) {
        return builder->get_resource(name);
    }

    FrameGraphResource* FrameGraph::access_resource(FrameGraphResourceHandle handle) {
        return builder->access_resource(handle);
    }

}// namespace Helix