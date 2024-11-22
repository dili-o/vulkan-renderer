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
    static const u16    INVALID_TEXTURE_INDEX = ~0u;

    static const u32    k_material_descriptor_set_index = 1;
    static const u32    k_max_depth_pyramid_levels = 16;

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
        glm::vec4       camera_position_texture_index;
    };

    

    enum DrawFlags {
        DrawFlags_AlphaMask = 1 << 0,
        DrawFlags_DoubleSided = 1 << 1,
        DrawFlags_Transparent = 1 << 2,
        DrawFlags_Phong = 1 << 3,
        DrawFlags_HasNormals = 1 << 4,
        DrawFlags_HasTexCoords = 1 << 5,
        DrawFlags_HasTangents = 1 << 6,
        DrawFlags_HasJoints = 1 << 7,
        DrawFlags_HasWeights = 1 << 8,
        DrawFlags_AlphaDither = 1 << 9,
        DrawFlags_Cloth = 1 << 10,
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

        u32                 meshlet_offset;
        u32                 meshlet_count;

        u32                 gpu_mesh_index = u32_max;

        glm::vec4           bounding_sphere;

        bool                is_transparent() const { return (pbr_material.flags & (DrawFlags_AlphaMask | DrawFlags_Transparent)) != 0; }
        bool                is_double_sided() const { return (pbr_material.flags & DrawFlags_DoubleSided) == DrawFlags_DoubleSided; }
    }; // struct Mesh

    struct MeshInstance {
        Mesh*               mesh;
        u32                 material_pass_index;
    }; // struct MeshInstance


    // Gpu Data Structs /////////////////////////////////////////////////////////////////////////
    struct alignas(16) GPUMeshDrawCounts {
        u32                     opaque_mesh_visible_count;
        u32                     opaque_mesh_culled_count;
        u32                     transparent_mesh_visible_count;
        u32                     transparent_mesh_culled_count;

        u32                     total_count;
        u32                     depth_pyramid_texture_index;
        u32                     late_flag;
        u32                     pad001;
    }; // struct GPUMeshDrawCounts Draw count buffer used in indirect draw calls

    struct alignas(16) GPUMeshDrawCommand {
        u32                     mesh_index;
        VkDrawIndexedIndirectCommand indirect; // 5 uint32_t
        VkDrawMeshTasksIndirectCommandNV indirectMS; // 2 uint32_t
    }; // struct GpuMeshDrawCommand 

    struct alignas(16) GPUMaterialData {

        u32                     textures[4]; // diffuse, roughness, normal, occlusion
        // PBR
        glm::vec4               emissive; // emissive_color_factor + emissive texture index
        glm::vec4               base_color_factor;
        glm::vec4               metallic_roughness_occlusion_factor; // metallic, roughness, occlusion

        u32                     flags;
        f32                     alpha_cutoff;
        u32                     vertex_offset;
        u32                     mesh_index; // Not used

        u32                     meshlet_offset;
        u32                     meshlet_count;
        u32                     padding0_;
        u32                     padding1_;

        // Phong
        glm::vec4               diffuse_colour;

        glm::vec3               specular_colour;
        f32                     specular_exp;

        glm::vec3               ambient_colour;
        f32                     padding2_;

    }; // struct GpuMaterialData

    struct alignas(16) GPUMeshInstanceData {
        glm::mat4               world;
        glm::mat4               inverse_world;

        u32                     mesh_index;
        u32                     pad000;
        u32                     pad001;
        u32                     pad002;
    }; // struct GpuMeshInstanceData

    struct GPUMeshData {
        glm::mat4           model;
        glm::mat4           inverse_model;

        u32                 textures[4]; // diffuse, roughness, normal, occlusion
        glm::vec4           base_color_factor;
        glm::vec4           metallic_roughness_occlusion_factor; // metallic, roughness, occlusion
        float               alpha_cutoff;
        float               padding_[3];

        u32                 flags;
        float               padding_2[3];
    }; // struct GPUMeshData


    struct alignas(16) GPUMeshlet {

        glm::vec3               center;
        f32                     radius;

        i8                      cone_axis[3];
        i8                      cone_cutoff;

        u32                     data_offset;
        u32                     mesh_index;
        u8                      vertex_count;
        u8                      triangle_count;
    }; // struct GPUMeshlet

    struct GPUMeshletVertexPosition {

        float                   position[3];
        float                   padding;
    }; // struct GPUMeshletVertexPosition

    struct GPUMeshletVertexData {

        u8                      normal[4];
        u8                      tangent[4];
        u16                     uv_coords[2];
        float                   padding;
    }; // struct GPUMeshletVertexData

    struct GPUSceneData {
        glm::mat4               view_projection;
        glm::mat4               view_projection_debug;
        glm::mat4               inverse_view_projection;
        glm::mat4               world_to_camera;    // view matrix
        glm::mat4               world_to_camera_debug;
        glm::mat4               previous_view_projection;

        glm::vec4               camera_position;
        glm::vec4               camera_position_debug;
        glm::vec4               light_position;

        f32                     light_range;
        f32                     light_intensity;
        u32                     dither_texture_index;
        f32                     z_near;

        f32                     z_far;
        f32                     projection_00;
        f32                     projection_11;
        u32                     frustum_cull_meshes;

        u32                     frustum_cull_meshlets;
        u32                     occlusion_cull_meshes;
        u32                     occlusion_cull_meshlets;
        u32                     freeze_occlusion_camera;

        f32                     resolution_x;
        f32                     resolution_y;
        f32                     aspect_ratio;
        f32                     pad0001;

        glm::vec4               frustum_planes[6];

    }; // struct GPUSceneData

    // Gpu Data Structs /////////////////////////////////////////////////////////////////////////

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

        virtual void            init(Renderer* renderer, Allocator* resident_allocator, FrameGraph* frame_graph, StackAllocator* stack_allocator, AsynchronousLoader* async_loader) { };
        virtual void            load(cstring filename, cstring path, Allocator* resident_allocator, StackAllocator* temp_allocator, AsynchronousLoader* async_loader) { };
        virtual void            free_gpu_resources(Renderer* renderer) { };
        virtual void            unload(Renderer* renderer) { };

        virtual void            register_render_passes(FrameGraph* frame_graph) { };
        virtual void            prepare_draws(Renderer* renderer, StackAllocator* stack_allocator) { };

        virtual void            fill_gpu_data_buffers(float model_scale) { };
        virtual void            submit_draw_task(ImGuiService* imgui, GPUProfiler* gpu_profiler, enki::TaskScheduler* task_scheduler) { };

        Array<GPUMeshlet>       meshlets;
        Array<GPUMeshletVertexPosition> meshlets_vertex_positions;
        Array<GPUMeshletVertexData> meshlets_vertex_data;
        Array<u32>              meshlet_vertex_and_index_indices;

        // Gpu buffers
        BufferHandle            material_data_buffer = k_invalid_buffer; // Contains the material data for opaque meshes and transparent meshes
        BufferHandle            mesh_instances_buffer = k_invalid_buffer;
        BufferHandle            mesh_bounds_buffer = k_invalid_buffer;
        BufferHandle            scene_constant_buffer = k_invalid_buffer;
        BufferHandle            meshlets_buffer = k_invalid_buffer;
        BufferHandle            meshlet_vertex_and_index_indices_buffer = k_invalid_buffer;
        BufferHandle            meshlets_vertex_pos_buffer = k_invalid_buffer;
        BufferHandle            meshlets_vertex_data_buffer = k_invalid_buffer;

        // Indirect data
        BufferHandle            mesh_draw_count_buffers[k_max_frames];
        BufferHandle            mesh_indirect_draw_command_buffers[k_max_frames];

        // Gpu debug draw
        BufferHandle            debug_line_buffer = k_invalid_buffer;
        BufferHandle            debug_line_count_buffer = k_invalid_buffer;
        BufferHandle            debug_line_indirect_command_buffer = k_invalid_buffer;

        GPUSceneData            scene_data;

        DescriptorSetHandle     mesh_shader_descriptor_set[k_max_frames];

        GPUMeshDrawCounts       mesh_draw_counts;

        Renderer*               renderer = nullptr;
    }; // struct Scene

    //
    //
    struct MeshCullingPass : public FrameGraphRenderPass {
        void                    render(CommandBuffer* gpu_commands, Scene* scene) override;

        void                    prepare_draws(Scene& scene, FrameGraph* frame_graph, Allocator* resident_allocator);
        void                    free_gpu_resources();

        Renderer* renderer;

        PipelineHandle          frustum_cull_pipeline;
        DescriptorSetHandle     frustum_cull_descriptor_set[k_max_frames];
        SamplerHandle           depth_pyramid_sampler;
        u32                     depth_pyramid_texture_index;

    }; // struct MeshCullingPass

    //
    //
    struct DepthPrePass : public FrameGraphRenderPass {
        void                render(CommandBuffer* gpu_commands, Scene* scene) override;

        void                init();
        void                prepare_draws(glTFScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator);
        void                free_gpu_resources();

        //Array<MeshInstance> mesh_instances{ };
        u32                 mesh_count;
        Mesh*               meshes;
        Renderer*           renderer;
    }; // struct DepthPrePass

    //
    //
    struct GBufferPass : public FrameGraphRenderPass {
        void                render(CommandBuffer* gpu_commands, Scene* scene) override;

        void                init();
        void                prepare_draws(glTFScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator);
        void                free_gpu_resources();

        u32                 double_sided_mesh_count;
        u32                 mesh_count;
        Mesh*               meshes;
        Renderer*           renderer;
        u32                 meshlet_program_index;
    }; // struct GBufferPass

    //
//
    struct DepthPyramidPass : public FrameGraphRenderPass {
        void                    render(CommandBuffer* gpu_commands, Scene* scene) override;
        void                    on_resize(GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height) override;
        void                    post_render(u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph) override;

        void                    prepare_draws(Scene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator);
        void                    free_gpu_resources();

        void                    create_depth_pyramid_resource(Texture* depth_texture);

        Renderer* renderer;

        PipelineHandle          depth_pyramid_pipeline;
        TextureHandle           depth_pyramid;
        SamplerHandle           depth_pyramid_sampler;
        TextureHandle           depth_pyramid_views[k_max_depth_pyramid_levels];
        DescriptorSetHandle     depth_hierarchy_descriptor_set[k_max_depth_pyramid_levels];

        u32                     depth_pyramid_levels = 0;

        bool                    update_depth_pyramid;
    }; // struct DepthPrePass

    //
    //
    struct LightPass : public FrameGraphRenderPass {
        void                render(CommandBuffer* gpu_commands, Scene* scene) override;

        void                init();
        void                prepare_draws(glTFScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator);
        void                fill_gpu_material_buffer();
        void                free_gpu_resources();

        Mesh                mesh{ };
        Renderer*           renderer;
        bool                use_compute;

        FrameGraphResource* depth_texture;
    }; // struct LightPass

    //
    //
    struct TransparentPass : public FrameGraphRenderPass {
        void                render(CommandBuffer* gpu_commands, Scene* scene) override;

        void                init();
        void                prepare_draws(glTFScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator);
        void                free_gpu_resources();

        u32                 double_sided_mesh_count;
        u32                 mesh_count;
        Mesh*               meshes;
        Renderer*           renderer;
    }; // struct TransparentPass

    //
    //
    struct LightDebugPass : public FrameGraphRenderPass {

        void                    render(CommandBuffer* gpu_commands, Scene* scene) override;

        void                    init();
        void                    prepare_draws(glTFScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator);
        void                    upload_materials() {};
        void                    free_gpu_resources();

        PipelineHandle          light_pipeline;
        DescriptorSetHandle     light_descriptor_set;
        Renderer*               renderer;
    }; // struct DoFPass


    struct glTFScene : public Scene {

        void                    init(Renderer* renderer, Allocator* resident_allocator, FrameGraph* frame_graph, StackAllocator* stack_allocator, AsynchronousLoader* async_loader) override;
        void                    load(cstring filename, cstring path, Allocator* resident_allocator, StackAllocator* temp_allocator, AsynchronousLoader* async_loader) override;
        void                    free_gpu_resources(Renderer* renderer) override;
        void                    unload(Renderer* renderer) override;

        void                    register_render_passes(FrameGraph* frame_graph) override;
        void                    prepare_draws(Renderer* renderer, StackAllocator* stack_allocator) override;
        void                    fill_pbr_material(Renderer& renderer, glTF::Material& material, PBRMaterial& pbr_material);
        u16                     get_material_texture(GpuDevice& gpu, glTF::TextureInfo* texture_info);
        u16                     get_material_texture(GpuDevice& gpu, i32 gltf_texture_index);

        void                    fill_gpu_data_buffers(float model_scale) override;
        void                    submit_draw_task(ImGuiService* imgui, GPUProfiler* gpu_profiler, enki::TaskScheduler* task_scheduler) override;

        void                    draw_mesh(CommandBuffer* gpu_commands, Mesh& mesh);

        void                    destroy_node(NodeHandle handle);

        void                    imgui_draw_node(NodeHandle node_handle);
        void                    imgui_draw_hierarchy();

        void                    imgui_draw_node_property(NodeHandle node_handle);

        Array<Mesh>             opaque_meshes;
        Array<Mesh>             transparent_meshes;

        // All graphics resources used by the scene
        Array<TextureResource>  images; // TODO: Maybe just store the pool index rather than the whole Texture resource
        Array<SamplerResource>  samplers;
        Array<BufferResource>   buffers;

        u32                     current_images_count = 0;
        u32                     current_buffers_count = 0;
        u32                     current_samplers_count = 0;


        glTF::glTF              gltf_scene; // Source gltf scene

        NodePool                node_pool;

        //DepthPrePass            depth_pre_pass;
        MeshCullingPass         mesh_cull_pass;
        GBufferPass             gbuffer_pass;
        LightPass               light_pass;
        TransparentPass         transparent_pass;
        //LightDebugPass          light_debug_pass;

        // Fullscreen data
        Program*                fullscreen_program = nullptr;
        DescriptorSetHandle     fullscreen_ds;
        u32                     fullscreen_texture_index = u32_max;

        NodeHandle              current_node{ };

        FrameGraph*             frame_graph;
        StackAllocator*         scratch_allocator;
        Allocator*              main_allocator;
        AsynchronousLoader*     loader;
        Material*               pbr_material;
        StringBuffer            names;


        // Buffers
        BufferHandle            light_cb;
        TextureResource         light_texture;

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

        void                ExecuteRange(enki::TaskSetPartition range_, u32 threadnum_) override;

    }; // struct DrawTask



} // namespace Helix

