#pragma once

#include "vendor/enkiTS/TaskScheduler.h"

#include "Core/Gltf.hpp"

#include "Renderer/AsynchronousLoader.hpp"
#include "Renderer/Renderer.hpp"
#include "Renderer/HelixImgui.hpp"
#include "Renderer/GPUProfiler.hpp"
#include "Renderer/FrameGraph.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <vendor/glm/glm/glm.hpp>
#include <vendor/glm/glm/gtc/quaternion.hpp>
#include <vendor/glm/glm/gtx/quaternion.hpp>

namespace Helix {
    static const u16 INVALID_TEXTURE_INDEX = ~0u;

    static const u32 k_material_descriptor_set_index = 1;

    struct glTFScene;

    struct Transform {

        glm::vec3                   scale = glm::vec3(1.0f);
        glm::quat                   rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3                   translation = glm::vec3(0.0f);

        //void                    reset();
        glm::mat4                   calculate_matrix() const {
            glm::mat4 translation_matrix = glm::translate(glm::mat4(1.0f), translation);
            glm::mat4 rotation_matrix = glm::toMat4(rotation);
            glm::mat4 scale_matrix = glm::scale(glm::mat4(1.0f), scale);

            glm::mat4 local_matrix = translation_matrix * rotation_matrix * scale_matrix;
            return local_matrix;
        }

        void               set_transform(glm::mat4& model) {
            translation = glm::vec3(model[3]);

            glm::mat3 rotation_matrix = glm::mat3(
                glm::normalize(glm::vec3(model[0])),
                glm::normalize(glm::vec3(model[1])),
                glm::normalize(glm::vec3(model[2]))
            );
            // Convert the rotation matrix to a quaternion
            rotation = glm::quat_cast(rotation_matrix);

            scale.x = glm::length(glm::vec3(model[0]));
            scale.y = glm::length(glm::vec3(model[1]));
            scale.z = glm::length(glm::vec3(model[2]));
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
        DrawFlags_DoubleSided = 1 << 1,
        DrawFlags_Transparent = 1 << 2,
    }; // enum DrawFlags

    struct PBRMaterial {
        Material*           material;

        BufferHandle        material_buffer;
        DescriptorSetHandle descriptor_set;

        u16                 diffuse_texture_index;
        u16                 roughness_texture_index;
        u16                 normal_texture_index;
        u16                 occlusion_texture_index;

        glm::vec4           base_color_factor;
        glm::vec4           metallic_roughness_occlusion_factor;

        f32                 alpha_cutoff;
        u32                 flags;
    };

    struct Mesh {

        PBRMaterial         pbr_material;

        BufferHandle        index_buffer;
        VkIndexType         index_type;
        u32                 index_offset;

        BufferHandle        position_buffer;
        BufferHandle        tangent_buffer;
        BufferHandle        normal_buffer;
        BufferHandle        texcoord_buffer;

        u32                 position_offset;
        u32                 tangent_offset;
        u32                 normal_offset;
        u32                 texcoord_offset;

        u32                 primitive_count;
        u32                 node_index;

        bool                is_transparent() const { return (pbr_material.flags & (DrawFlags_AlphaMask | DrawFlags_Transparent)) != 0; }
        bool                is_double_sided() const { return (pbr_material.flags & DrawFlags_DoubleSided) == DrawFlags_DoubleSided; }
    }; // struct Mesh

    struct MeshInstance {
        Mesh*               mesh;
        u32                 material_pass_index;
    }; // struct MeshInstance

    struct GPUMeshData {
        glm::mat4           model;
        glm::mat4           inverse_model;

        u32                 textures[4]; // diffuse, roughness, normal, occlusion
        glm::vec4           base_color_factor;
        glm::vec4           metallic_roughness_occlusion_factor; // metallic, roughness, occlusion
        float               alpha_cutoff;
        float               padding_[3];

        u32                 flags;
    }; // struct GPUMeshData


    // Nodes //////////////////////////////////////
    enum class NodeType {
        Node,
        MeshNode,
        LightNode
    };

    struct NodeHandle {
        u32                     index = k_invalid_index;
        NodeType                type = NodeType::Node;

        // Equality operator
        bool operator==(const NodeHandle& other) const {
            return index == other.index && type == other.type;
        }

        // Inequality operator
        bool operator!=(const NodeHandle& other) const {
            return !(*this == other);
        }
    };

    struct Node;

    struct NodePool {

        void                    init(Allocator* allocator);
        void                    shutdown();

        NodeHandle              obtain_node(NodeType type);
        void*                   access_node(NodeHandle handle);
        Node*                   get_root_node();

        Allocator* allocator;

        NodeHandle              root_node;

        ResourcePool            base_nodes;
        ResourcePool            mesh_nodes;
        ResourcePool            light_nodes;
    };

    struct Node {
        NodeHandle              handle = { k_invalid_index, NodeType::Node };
        NodeHandle              parent = { k_invalid_index, NodeType::Node };
        Array<NodeHandle>       children;
        Transform               local_transform{ };
        Transform               world_transform{ };

        cstring                 name = nullptr;

        void                    (*updateFunc)(Node*, NodePool* node_pool);

        void            update_transform(NodePool* node_pool) {
            if (!updateFunc) {
                HWARN("Node does not have update function");
                return;
            }
            updateFunc(this, node_pool);


        }
        void                    add_child(Node* node) {
            node->parent = handle;
            children.push(node->handle);// TODO: Fix array issue!!!!!!!!!!!!
        }
    };

    struct MeshNode : public Node {
        Mesh* mesh; 
    };

    struct LightNode : public Node {
        // Doesn't hold any data for now;
    };



    ///////////////////////////////////////////////
    struct Scene {

        virtual void            load(cstring filename, cstring path, Allocator* resident_allocator, StackAllocator* temp_allocator, AsynchronousLoader* async_loader) { };
        virtual void            free_gpu_resources(Renderer* renderer) { };
        virtual void            unload(Renderer* renderer) { };

        virtual void            register_render_passes(FrameGraph* frame_graph) { };
        virtual void            prepare_draws(Renderer* renderer, StackAllocator* scratch_allocator) { };

        virtual void            upload_materials(float model_scale) { };
        virtual void            submit_draw_task(ImGuiService* imgui, GPUProfiler* gpu_profiler, enki::TaskScheduler* task_scheduler) { };
    }; // struct Scene

    //
    //
    struct DepthPrePass : public FrameGraphRenderPass {
        void                render(CommandBuffer* gpu_commands, Scene* render_scene) override;

        void                prepare_draws(glTFScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator);
        void                free_gpu_resources();

        Array<MeshInstance> mesh_instances;
        Renderer*           renderer;
    }; // struct DepthPrePass

    //
    //
    struct GBufferPass : public FrameGraphRenderPass {
        void                render(CommandBuffer* gpu_commands, Scene* render_scene) override;

        void                prepare_draws(glTFScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator);
        void                free_gpu_resources();

        Array<MeshInstance> mesh_instances;
        Renderer* renderer;
    }; // struct GBufferPass

    //
    //
    struct LightPass : public FrameGraphRenderPass {
        void                render(CommandBuffer* gpu_commands, Scene* render_scene) override;

        void                prepare_draws(glTFScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator);
        void                upload_materials();
        void                free_gpu_resources();

        Mesh                mesh;
        Renderer*           renderer;
    }; // struct LightPass

    //
    //
    struct TransparentPass : public FrameGraphRenderPass {
        void                render(CommandBuffer* gpu_commands, Scene* render_scene) override;

        void                prepare_draws(glTFScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator);
        void                free_gpu_resources();

        Array<MeshInstance> mesh_instances;
        Renderer* renderer;
    }; // struct TransparentPass

    //
    //
    struct LightDebugPass : public FrameGraphRenderPass {

        void                    render(CommandBuffer* gpu_commands, Scene* render_scene) override {};

        void                    prepare_draws(glTFScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator) {};
        void                    upload_materials() {};
        void                    free_gpu_resources() {};

        Mesh                    mesh;
        Renderer* renderer;
    }; // struct DoFPass


    struct glTFScene : public Scene {

        void                    load(cstring filename, cstring path, Allocator* resident_allocator, StackAllocator* temp_allocator, AsynchronousLoader* async_loader) override;
        void                    free_gpu_resources(Renderer* renderer) override;
        void                    unload(Renderer* renderer) override;

        void                    register_render_passes(FrameGraph* frame_graph) override;
        void                    prepare_draws(Renderer* renderer, StackAllocator* scratch_allocator) override;
        void                    fill_pbr_material(Renderer& renderer, glTF::Material& material, PBRMaterial& pbr_material);
        u16                     get_material_texture(GpuDevice& gpu, glTF::TextureInfo* texture_info);
        u16                     get_material_texture(GpuDevice& gpu, i32 gltf_texture_index);

        void                    upload_materials(float model_scale) override;
        void                    submit_draw_task(ImGuiService* imgui, GPUProfiler* gpu_profiler, enki::TaskScheduler* task_scheduler) override;

        void                    draw_mesh(CommandBuffer* gpu_commands, Mesh& mesh);

        void                    destroy_node(NodeHandle handle);

        void                    imgui_draw_node(NodeHandle node_handle);
        void                    imgui_draw_hierarchy();

        void                    imgui_draw_node_property(NodeHandle node_handle);

        Array<Mesh>             meshes;

        // All graphics resources used by the scene
        Array<TextureResource>  images; // TODO: Maybe just store the pool index rather than the whole Texture resource
        Array<SamplerResource>  samplers;
        Array<BufferResource>   buffers;

        glTF::glTF              gltf_scene; // Source gltf scene

        NodePool                node_pool;

        DepthPrePass            depth_pre_pass;
        GBufferPass             gbuffer_pass;
        LightPass               light_pass;
        TransparentPass         transparent_pass;
        LightDebugPass          light_debug_pass;

        // Fullscreen data
        Program*                fullscreen_program = nullptr;
        DescriptorSetHandle     fullscreen_ds;
        u32                     fullscreen_texture_index = u32_max;

        NodeHandle              current_node{ };

        Renderer*               renderer;
        FrameGraph*             frame_graph;

        BufferHandle            local_constants_buffer;
        Helix::BufferHandle     light_cb;
        Helix::TextureResource  light_texture;

    }; // struct GltfScene

    struct glTFDrawTask : public enki::ITaskSet {

        GpuDevice*          gpu = nullptr;
        FrameGraph*         frame_graph = nullptr;
        Renderer*           renderer = nullptr;
        ImGuiService*       imgui = nullptr;
        GPUProfiler*        gpu_profiler = nullptr;
        glTFScene*          scene = nullptr;
        u32                 thread_id = 0;

        void                init(GpuDevice* gpu_, FrameGraph* frame_graph_, Renderer* renderer_, ImGuiService* imgui_, GPUProfiler* gpu_profiler_, glTFScene* scene_);

        void                ExecuteRange(enki::TaskSetPartition range_, uint32_t threadnum_) override;

    }; // struct DrawTask



} // namespace Helix

