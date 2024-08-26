#pragma once

#include "vendor/enkiTS/TaskScheduler.h"

#include "Core/Gltf.hpp"

#include "Renderer/AsynchronousLoader.hpp"
#include "Renderer/Renderer.hpp"
#include "Renderer/HelixImgui.hpp"
#include "Renderer/GPUProfiler.hpp"
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <vendor/glm/glm/glm.hpp>
#include <vendor/glm/glm/gtc/quaternion.hpp>
#include <vendor/glm/glm/gtx/quaternion.hpp>

namespace Helix {
    static const u16 INVALID_TEXTURE_INDEX = ~0u;

    struct Transform {

        glm::vec3                   scale;
        glm::quat                   rotation;
        glm::vec3                   translation;

        //void                    reset();
        glm::mat4                   calculate_matrix() const {
            const glm::mat4 translation_matrix = glm::translate(glm::mat4(1.0f), translation);
            const glm::mat4 scale_matrix = glm::scale(glm::mat4(1.0f), scale);
            const glm::mat4 local_matrix = (translation_matrix * glm::toMat4(rotation) * scale_matrix);
            return local_matrix;
        }

        void               calculate_transform(glm::mat4& model) {
            translation = glm::vec3(model[3]);

            scale.x = glm::length(glm::vec3(model[0]));
            scale.y = glm::length(glm::vec3(model[1]));
            scale.z = glm::length(glm::vec3(model[2]));
            // TODO: Rotation

        }
    }; // struct Transform

    struct LightUniform {
        glm::mat4       model;
        glm::mat4       view_projection;
        glm::vec4       camera_position;
        u32             texture_index;
    };

    struct UniformData {
        glm::mat4       view_projection;
        glm::vec4       camera_position;
        glm::vec4       light_position;
        f32             light_range;
        f32             light_intensity;

    }; // struct UniformData

    enum DrawFlags {
        DrawFlags_AlphaMask = 1 << 0,
    }; // enum DrawFlags

    struct PBRMaterial {
        u16             diffuse_texture_index;
        u16             roughness_texture_index;
        u16             normal_texture_index;
        u16             occlusion_texture_index;

        glm::vec4       base_color_factor;
        glm::vec4       metallic_roughness_occlusion_factor;
    };

    struct MeshDraw{

        Material*       material;

        BufferHandle    index_buffer;
        BufferHandle    position_buffer;
        BufferHandle    tangent_buffer;
        BufferHandle    normal_buffer;
        BufferHandle    texcoord_buffer;
        BufferHandle    material_buffer;

        VkIndexType     index_type;
        u32             index_offset;

        u32             position_offset;
        u32             tangent_offset;
        u32             normal_offset;
        u32             texcoord_offset;

        u32             primitive_count;

        // Indices used for bindless textures.
        u16             diffuse_texture_index;
        u16             roughness_texture_index;
        u16             normal_texture_index;
        u16             occlusion_texture_index;

        glm::vec4           base_color_factor;
        glm::vec4           metallic_roughness_occlusion_factor;

        glm::vec3           scale;

        glm::mat4           model;

        f32             alpha_cutoff;
        u32             flags;

        DescriptorSetHandle descriptor_set;
    }; // struct MeshDraw

    struct MeshData {
        glm::mat4   m;
        glm::mat4   inverseM;

        u32         textures[4]; // diffuse, roughness, normal, occlusion
        glm::vec4   base_color_factor;
        glm::vec4   metallic_roughness_occlusion_factor; // metallic, roughness, occlusion
        float       alpha_cutoff;
        float       padding_[3];
        u32         flags;
    }; // struct MeshData


    // Nodes //////////////////////////////////////
    enum NodeType {
        NodeType_Node,
        NodeType_MeshNode,
        NodeType_LightNode
    };

    struct NodeHandle {
        u32                     index = k_invalid_index;
        NodeType                type =  NodeType_Node;

        // Equality operator
        bool operator==(const NodeHandle& other) const {
            return index == other.index && type == other.type;
        }

        // Inequality operator
        bool operator!=(const NodeHandle& other) const {
            return !(*this == other);
        }
    };


    struct Node {
        NodeHandle              parent;
        Array<NodeHandle>       children;
        Transform               local_transform;
        Transform               world_transform;

        cstring                 name = nullptr;

        // TODO: update children
        void                    update_transform(Transform& transform) {

            world_transform.translation += transform.translation;
            world_transform.scale *= transform.scale;
            // TODO: Rotation
            //world_transform.rotation.z += local_transform.rotation.y;
        }
    };

    struct MeshNode : public Node {
        u32                     mesh_draw_index; // Index into the MeshDraw array
    };

    struct LightNode : public Node {
        // Doesn't hold any data for now;
    };

    struct NodePool {

        void                    init(Allocator* allocator);
        void                    shutdown();

        void*                   access_node(NodeHandle handle);
        NodeHandle              obtain_node(NodeType type);

        Allocator*              allocator;

        Array<NodeHandle>       root_nodes;

        ResourcePool            base_nodes;
        ResourcePool            mesh_nodes;
        ResourcePool            light_nodes;
    };

    ///////////////////////////////////////////////
    struct Scene {

        virtual void            load(cstring filename, cstring path, Allocator* resident_allocator, StackAllocator* temp_allocator, AsynchronousLoader* async_loader) { };
        virtual void            free_gpu_resources(Renderer* renderer) { };
        virtual void            unload(Renderer* renderer) { };

        virtual void            prepare_draws(Renderer* renderer, StackAllocator* scratch_allocator) { };

        virtual void            upload_materials(float model_scale) { };
        virtual void            submit_draw_task(ImGuiService* imgui, GPUProfiler* gpu_profiler, enki::TaskScheduler* task_scheduler) { };
    }; // struct Scene

    struct glTFScene : public Scene {

        void                    load(cstring filename, cstring path, Allocator* resident_allocator, StackAllocator* temp_allocator, AsynchronousLoader* async_loader);
        void                    free_gpu_resources(Renderer* renderer);
        void                    unload(Renderer* renderer);

        void                    prepare_draws(Renderer* renderer, StackAllocator* scratch_allocator);
        void                    upload_materials(float model_scale);
        void                    submit_draw_task(ImGuiService* imgui, GPUProfiler* gpu_profiler, enki::TaskScheduler* task_scheduler);

        void                    destroy_node(NodeHandle handle);

        void                    imgui_draw_node(NodeHandle node_handle);
        void                    imgui_draw_hierarchy();

        void                    imgui_draw_node_property(NodeHandle node_handle);

        Array<MeshDraw>         mesh_draws;

        // All graphics resources used by the scene
        Array<TextureResource>  images; // TODO: Maybe just store the pool index rather than the whole Texture resource
        Array<SamplerResource>  samplers;
        Array<BufferResource>   buffers;

        glTF::glTF              gltf_scene; // Source gltf scene

        NodePool                node_pool;

        NodeHandle              current_node{ };

        Renderer*               renderer;

        BufferHandle            scene_buffer;
        Helix::BufferHandle     light_cb;
        Helix::TextureResource  light_texture;

    }; // struct GltfScene

    struct glTFDrawTask : public enki::ITaskSet {

        GpuDevice*              gpu = nullptr;
        Renderer*               renderer = nullptr;
        ImGuiService*           imgui = nullptr;
        GPUProfiler*            gpu_profiler = nullptr;
        glTFScene*              scene = nullptr;
        u32                     thread_id = 0;

        void init(GpuDevice* gpu_, Renderer* renderer_, ImGuiService* imgui_, GPUProfiler* gpu_profiler_, glTFScene* scene_);

        void ExecuteRange(enki::TaskSetPartition range_, uint32_t threadnum_) override;

    }; // struct DrawTask



} // namespace Helix

