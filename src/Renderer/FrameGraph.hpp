#pragma once

#include "Core/Array.hpp"
#include "Core/DataStructures.hpp"
#include "Core/HashMap.hpp"
#include "Core/Service.hpp"

#include "Renderer/GPUResources.hpp"

#include <vulkan/vulkan.h>

namespace Helix {

    struct Allocator;
    struct CommandBuffer;
    struct FrameGraph;
    struct GpuDevice;
    struct Scene;

    typedef u32                             FrameGraphHandle;

    struct FrameGraphNodeHandle {
        FrameGraphHandle                    index;
    };

    struct FrameGraphResourceHandle {
        FrameGraphHandle                    index;
    };

	enum FrameGraphResourceType {
		FrameGraphResourceType_Invalid = -1,

		FrameGraphResourceType_Buffer = 0,
		FrameGraphResourceType_Texture = 1,
		FrameGraphResourceType_Attachment = 2,
		FrameGraphResourceType_Reference = 3
	};

    struct FrameGraphResourceInfo {
        bool                                external = false;

        union {
            struct {
                sizet                       size;
                VkBufferUsageFlags          flags;

                BufferHandle                handle;
            } buffer;

            struct {
                u32                         width;
                u32                         height;
                u32                         depth;

                f32                         scale_width;
                f32                         scale_height;

                VkFormat                    format;
                VkImageUsageFlags           flags;

                RenderPassOperation::Enum   load_op;

                TextureHandle               handle;

                bool                        compute;
            } texture;
        };
    };

    struct FrameGraphResourceCreation {
        FrameGraphResourceType              type;
        FrameGraphResourceInfo              resource_info;

        const char*                         name;
    };

    //NOTE: Represents a resource used by a node in the frame graph
	struct FrameGraphResource {
		FrameGraphResourceType				type;
		FrameGraphResourceInfo				resource_info;

		FrameGraphNodeHandle				producer;

		FrameGraphResourceHandle			output_handle;

		i32                                 ref_count = 0;

		cstring                             name = nullptr;
	};

    struct FrameGraphRenderPass
    {
        virtual void                        add_ui() { }
        virtual void                        pre_render(CommandBuffer* gpu_commands, Scene* scene) { }
        virtual void                        render(CommandBuffer* gpu_commands, Scene* scene) { }
        virtual void                        post_render(u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph) { }
        virtual void                        on_resize(GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height) {}

        bool                                enabled = false;
    };

    struct FrameGraphNodeCreation {
        Array<FrameGraphResourceCreation>   input_creations;
        Array<FrameGraphResourceCreation>   output_creations;

        bool                                enabled;

        cstring                             name;
        bool                                compute;
    };

    struct FrameGraphNode {
        i32                                 ref_count = 0;

        RenderPassHandle                    render_pass;
        FramebufferHandle                   framebuffer;
        FrameGraphRenderPass*               graph_render_pass;

        Array<FrameGraphResourceHandle>     inputs;
        Array<FrameGraphResourceHandle>     outputs;

        Array<FrameGraphNodeHandle>         edges;

        bool                                compute = false;

        bool                                enabled = true;
        cstring                             name = nullptr;
    };
    ////////////////////////////////////////
    // Caches
    struct FrameGraphRenderPassCache {
        void                                    init(Allocator* allocator);
        void                                    shutdown();

        FlatHashMap<u64, FrameGraphRenderPass*> render_pass_map;
    };

    struct FrameGraphResourceCache {
        void                                        init(Allocator* allocator, GpuDevice* device);
        void                                        shutdown();

        GpuDevice* device;

        FlatHashMap<u64, u32>                       resource_map;
        ResourcePoolTyped<FrameGraphResource>       resources;
    };

    struct FrameGraphNodeCache {
        void                                    init(Allocator* allocator, GpuDevice* device);
        void                                    shutdown();

        GpuDevice* device;

        FlatHashMap<u64, u32>                   node_map;
        ResourcePool                            nodes;
    };
    ////////////////////////////////////////
    // FrameGraphBuilder
    struct FrameGraphBuilder : public Service {
        void                            init(GpuDevice* device);
        void                            shutdown();

        void                            register_render_pass(cstring name, FrameGraphRenderPass* render_pass);

        FrameGraphResourceHandle        create_node_output(const FrameGraphResourceCreation& creation, FrameGraphNodeHandle producer);
        FrameGraphResourceHandle        create_node_input(const FrameGraphResourceCreation& creation);
        FrameGraphNodeHandle            create_node(const FrameGraphNodeCreation& creation);

        FrameGraphNode*                 get_node(cstring name);
        FrameGraphNode*                 access_node(FrameGraphNodeHandle handle);

        FrameGraphResource*             get_resource(cstring name);
        FrameGraphResource*             access_resource(FrameGraphResourceHandle handle);

        FrameGraphResourceCache         resource_cache;
        FrameGraphNodeCache             node_cache;
        FrameGraphRenderPassCache       render_pass_cache;

        Allocator*                      allocator;

        GpuDevice*                      device;

        static constexpr u32            k_max_render_pass_count = 256;
        static constexpr u32            k_max_resources_count = 1024;
        static constexpr u32            k_max_nodes_count = 1024;

        static constexpr cstring        k_name = "helix_frame_graph_builder_service";
    };

    //
    //
    struct FrameGraph {
        void                            init(FrameGraphBuilder* builder);
        void                            shutdown();

        void                            parse(cstring file_path, StackAllocator* temp_allocator);

        // NOTE(marco): each frame we rebuild the graph so that we can enable only
        // the nodes we are interested in
        void                            reset();
        void                            enable_render_pass(cstring render_pass_name);
        void                            disable_render_pass(cstring render_pass_name);
        void                            compile();
        void                            add_ui();
        void                            render(u32 current_frame_index, CommandBuffer* gpu_commands, Scene* scene);
        void                            on_resize(GpuDevice& gpu, u32 new_width, u32 new_height);

        FrameGraphNode*                 get_node(cstring name);
        FrameGraphNode*                 access_node(FrameGraphNodeHandle handle);

        FrameGraphResource*             get_resource(cstring name);
        FrameGraphResource*             access_resource(FrameGraphResourceHandle handle);

        // TODO(marco): in case we want to add a pass in code
        void                            add_node(FrameGraphNodeCreation& node);

        // NOTE(marco): nodes sorted in topological order
        Array<FrameGraphNodeHandle>     nodes;

        FrameGraphBuilder*              builder;
        Allocator*                      allocator;

        LinearAllocator                 linear_allocator;

        cstring                         name = nullptr;
    };

} // namespace Helix