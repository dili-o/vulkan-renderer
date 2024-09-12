#pragma once

#include "Renderer/Renderer.hpp"

namespace Helix {

    struct FrameGraph;

    //
    //
    struct ResourcesLoader {

        void            init(Helix::Renderer* renderer, Helix::StackAllocator* temp_allocator, Helix::FrameGraph* frame_graph);
        void            shutdown();

        void            load_gpu_technique(cstring json_path);

        Renderer*       renderer;
        FrameGraph*     frame_graph;
        StackAllocator* temp_allocator;

    }; // struct ResourcesLoader

} // namespace Helix
