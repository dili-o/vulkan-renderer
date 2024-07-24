#pragma once

#include "Core/Array.hpp"
#include "Renderer/GPUResources.hpp"

#if defined(_MSC_VER)
#include <spirv-headers/spirv.h>
#else
#include <spirv_cross/spirv.h>
#endif
#include <vulkan/vulkan.h>

namespace Helix {

    struct StringBuffer;

    namespace spirv {

        static const u32 MAX_SET_COUNT = 32;
        // TODO: Maybe include vertex input as well
        struct ParseResult {
            u32                         set_count;
            DescriptorSetLayoutCreation sets[MAX_SET_COUNT];
        };

        void                            parse_binary(const u32* data, size_t data_size, StringBuffer& name_buffer, ParseResult* parse_result);

    } // namespace spirv
} // namespace Helix
