#pragma once

#include "vendor/enkiTS/TaskScheduler.h"

#include "Core/Gltf.hpp"

#include "Renderer/AsynchronousLoader.hpp"
#include "Renderer/Renderer.hpp"
#include "Renderer/HelixImgui.hpp"
#include "Renderer/GPUProfiler.hpp"
#include <cglm/struct/affine.h>
#include <cglm/struct/quat.h>

namespace Helix {
    static const u16 INVALID_TEXTURE_INDEX = ~0u;

    struct Transform {

        vec3s                   scale;
        versors                 rotation;
        vec3s                   translation;

        //void                    reset();
        mat4s                   calculate_matrix() const {
            const mat4s translation_matrix = glms_translate_make(translation);
            const mat4s scale_matrix = glms_scale_make(scale);
            const mat4s local_matrix = glms_mat4_mul(glms_mat4_mul(translation_matrix, glms_quat_mat4(rotation)), scale_matrix);
            return local_matrix;
        }

        void               calculate_transform(mat4s& model) {
            translation.x = model.m30;;
            translation.y = model.m31;;
            translation.z = model.m32;;

            scale.x = sqrtf(model.m00 * model.m00 + model.m10 * model.m10 + model.m20 * model.m20);
            scale.y = sqrtf(model.m01 * model.m01 + model.m11 * model.m11 + model.m21 * model.m21);
            scale.z = sqrtf(model.m02 * model.m02 + model.m12 * model.m12 + model.m22 * model.m22);
            // TODO: Rotation

        }
    }; // struct Transform

    struct LightUniform {
        mat4s       model;
        mat4s       view_projection;
        vec4s       camera_position;
        u32         texture_index;
    };

    struct UniformData {
        mat4s       view_projection;
        vec4s       camera_position;
        vec4s       light_position;
        float       light_range;
        float       light_intensity;

    }; // struct UniformData

    enum DrawFlags {
        DrawFlags_AlphaMask = 1 << 0,
    }; // enum DrawFlags

    struct PBRMaterial {
        u16             diffuse_texture_index;
        u16             roughness_texture_index;
        u16             normal_texture_index;
        u16             occlusion_texture_index;

        vec4s           base_color_factor;
        vec4s           metallic_roughness_occlusion_factor;
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

        vec4s           base_color_factor;
        vec4s           metallic_roughness_occlusion_factor;

        vec3s           scale;

        mat4s           model;

        f32             alpha_cutoff;
        u32             flags;

        DescriptorSetHandle descriptor_set;
    }; // struct MeshDraw

    struct MeshData {
        mat4s       m;
        mat4s       inverseM;

        u32         textures[4]; // diffuse, roughness, normal, occlusion
        vec4s       base_color_factor;
        vec4s       metallic_roughness_occlusion_factor; // metallic, roughness, occlusion
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

        // TODO: Factor in parent's transform
        void                    update_transform(Transform& transform) {

            world_transform.translation = glms_vec3_add(world_transform.translation, transform.translation);
            world_transform.scale = glms_vec3_mul(world_transform.scale, transform.scale);
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

