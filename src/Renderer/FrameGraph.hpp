#include "Core/Array.hpp"
#include "Core/DataStructures.hpp"
#include "Core/HashMap.hpp"
#include "Core/Service.hpp"

#include "GPUResources.hpp"

#include <vulkan/vulkan.h>

namespace Helix {

    typedef u32                             FrameGraphHandle;


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

                BufferHandle                buffer;
            } buffer;

            struct {
                u32                         width;
                u32                         height;
                u32                         depth;

                VkFormat                    format;
                VkImageUsageFlags           flags;

                RenderPassOperation::Enum   load_op;

                TextureHandle               texture;
            } texture;
        };
    };

    struct FrameGraphNodeHandle {
        FrameGraphHandle                    index;
    };

    struct FrameGraphResourceHandle {
        FrameGraphHandle                    index;
    };

    //NOTE: Represents a resource used by a node in the frame graph
	struct FrameGraphResource {
		FrameGraphResourceType				type;
		FrameGraphResourceInfo				resource_info;
		FrameGraphNodeHandle				producer;
		FrameGraphResourceHandle			output_handle;
		i32 ref_count						= 0;
		cstring name						= nullptr;
	};

    struct FrameGraphNode {
        RenderPassHandle                    render_pass;
        FramebufferHandle                   framebuffer;
        //FrameGraphRenderPass*               graph_render_pass;

        Array<FrameGraphResourceHandle>     inputs;
        Array<FrameGraphResourceHandle>     outputs;

        Array<FrameGraphNodeHandle>         edges;
        cstring name                        = nullptr;
    };


} // namespace Helix