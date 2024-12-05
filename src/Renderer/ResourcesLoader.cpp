#include "Renderer/ResourcesLoader.hpp"
#include "Renderer/FrameGraph.hpp"

#include "Core/File.hpp"

#include "vendor/json.hpp"

//#define STB_IMAGE_IMPLEMENTATION
#include "vendor/imgui/stb_image.h"

namespace Helix {

	// Utility methods ////////////////////////////////////////////////////////
	static void             shader_concatenate(cstring filename, Helix::StringBuffer& path_buffer, Helix::StringBuffer& shader_buffer, Helix::Allocator* temp_allocator);
	static VkBlendFactor    get_blend_factor(const std::string factor);
	static VkBlendOp        get_blend_op(const std::string op);
	static void             parse_gpu_pipeline(
		StringBuffer& resource_name_buffer,
		nlohmann::json& pipeline,
		PipelineCreation& pc, 
		StringBuffer& path_buffer,
		StringBuffer& shader_buffer,
		Allocator* temp_allocator,
		Renderer* renderer,
		FrameGraph* frame_graph,
		StringBuffer& path_name_buffer);

	// ResourcesLoader //////////////////////////////////////////////////
	void ResourcesLoader::init(Helix::Renderer* renderer_, Helix::StackAllocator* temp_allocator_, Helix::FrameGraph* frame_graph_) {
		renderer = renderer_;
		temp_allocator = temp_allocator_;
		frame_graph = frame_graph_;
		resource_name_buffer.init(hkilo(64), &MemoryService::instance()->system_allocator);
	}

	void ResourcesLoader::shutdown() {
		resource_name_buffer.shutdown();
	}

	void ResourcesLoader::load_program(cstring json_path) {

		sizet allocated_marker = temp_allocator->get_marker();

		FileReadResult read_result = file_read_text(json_path, temp_allocator);

		StringBuffer path_buffer;
		path_buffer.init(1024, temp_allocator);

		StringBuffer shader_code_buffer;
		shader_code_buffer.init(hmega(2), temp_allocator);

		StringBuffer pass_name_buffer;
		pass_name_buffer.init(hkilo(2), temp_allocator);

		using json = nlohmann::json;

		json json_data = json::parse(read_result.data);

		json name = json_data["name"];
		std::string name_string;
		if (name.is_string()) {
			name.get_to(name_string);
			HINFO("Parsing Program {}", name_string.c_str());
		}

		ProgramCreation program_creation;
		program_creation.name = resource_name_buffer.append_use_f("%s", name_string.c_str()); //name_string.c_str();

		json pipelines = json_data["pipelines"];
		if (pipelines.is_array()) {
			u32 pipeline_size = (u32)pipelines.size();
			for (u32 i = 0; i < pipeline_size; i++) {
				json pipeline = pipelines[i];
				PipelineCreation pipeline_creation{};
				pipeline_creation.reset();

				std::string pipeline_name;
				pipeline["name"].get_to(pipeline_name);

				pipeline_creation.name = resource_name_buffer.append_use_f("%s", pipeline_name.c_str());

				json inherit_from = pipeline["inherit_from"];
				if (inherit_from.is_string()) {
					std::string inherited_name;
					inherit_from.get_to(inherited_name);

					for (u32 ii = 0; ii < pipeline_size; ++ii) {
						json pipeline_i = pipelines[ii];
						std::string name;
						pipeline_i["name"].get_to(name);

						if (name == inherited_name) {
							parse_gpu_pipeline(resource_name_buffer, pipeline_i, pipeline_creation, path_buffer, shader_code_buffer, temp_allocator, renderer, frame_graph, pass_name_buffer);
							break;
						}
					}
				}
				// TODO: Properly get the name of each shader
				parse_gpu_pipeline(resource_name_buffer, pipeline, pipeline_creation, path_buffer, shader_code_buffer, temp_allocator, renderer, frame_graph, pass_name_buffer);

				program_creation.add_pipeline(pipeline_creation);
			}
		}

		renderer->create_program(program_creation);

		temp_allocator->free_marker(allocated_marker);
	}

	void parse_gpu_pipeline(
		StringBuffer& resource_name_buffer,
		nlohmann::json& pipeline,
		PipelineCreation& pc,
		StringBuffer& path_buffer,
		StringBuffer& shader_code_buffer,
		Allocator* temp_allocator,
		Renderer* renderer,
		FrameGraph* frame_graph,
		StringBuffer& pass_name_buffer) {

		using json = nlohmann::json;

		json json_name = pipeline["name"];
		if (json_name.is_string()) {
			std::string name;
			json_name.get_to(name);

			pc.name = pass_name_buffer.append_use_f("%s", name.c_str());
		}

		bool compute_shader_pass = false;

		// Parse Shaders
		json shaders = pipeline["shaders"];
		if (!shaders.is_null()) {
			for (sizet s = 0; s < shaders.size(); ++s) {
				json shader = shaders[s];
				std::string name;

				path_buffer.clear();
				
				// Read file
				cstring code = shader_code_buffer.current();
				json includes = shader["includes"];

				// Add the includes
				if (includes.is_array()) {
					for (sizet in = 0; in < includes.size(); ++in) {
						includes[in].get_to(name);
						shader_concatenate(name.c_str(), path_buffer, shader_code_buffer, temp_allocator);
					}
				}

				shader["shader"].get_to(name);
				shader_concatenate(name.c_str(), path_buffer, shader_code_buffer, temp_allocator);

				//size_t pos = name.find(".");
				//std::string res = name.substr(0, pos);
				pc.shader_state_creation.name = pc.name;

				shader_code_buffer.close_current_string();

				shader["stage"].get_to(name);
#if NVIDIA
				VkShaderStageFlagBits mesh_stage = VK_SHADER_STAGE_MESH_BIT_NV;
				VkShaderStageFlagBits task_stage = VK_SHADER_STAGE_TASK_BIT_NV;
#else
				VkShaderStageFlagBits mesh_stage = VK_SHADER_STAGE_MESH_BIT_EXT;
				VkShaderStageFlagBits task_stage = VK_SHADER_STAGE_TASK_BIT_EXT;
#endif // NVIDIA

				if (name == "vertex")
					pc.shader_state_creation.add_stage(code, strlen(code), VK_SHADER_STAGE_VERTEX_BIT);
				else if (name == "fragment")
					pc.shader_state_creation.add_stage(code, strlen(code), VK_SHADER_STAGE_FRAGMENT_BIT);
				else if (name == "geometry")
					pc.shader_state_creation.add_stage(code, strlen(code), VK_SHADER_STAGE_GEOMETRY_BIT);
				else if (name == "compute") {
					pc.shader_state_creation.add_stage(code, strlen(code), VK_SHADER_STAGE_COMPUTE_BIT);
					compute_shader_pass = true;
				}
				else if (name == "mesh") {
					if (!renderer->gpu->gpu_device_features & GpuDeviceFeature_MESH_SHADER)
						HASSERT_MSG(false, "No mesh shader support");
					pc.shader_state_creation.add_stage(code, strlen(code), mesh_stage);
				}else if (name == "task") {
					if (!renderer->gpu->gpu_device_features & GpuDeviceFeature_MESH_SHADER)
						HASSERT_MSG(false, "No task shader support");
					pc.shader_state_creation.add_stage(code, strlen(code), task_stage);
				}

				else
					HASSERT_MSG(false, "Unknown shader stage");
			}
		}

		// Parse Vertex Inputs
		json vertex_inputs = pipeline["vertex_input"];
		if (vertex_inputs.is_array()) {

			pc.vertex_input_creation.num_vertex_attributes = 0;
			pc.vertex_input_creation.num_vertex_streams = 0;

			for (sizet v = 0; v < vertex_inputs.size(); ++v) {
				VertexAttribute attribute{};

				json vertex_input = vertex_inputs[v];

				attribute.location = (u16)vertex_input.value("attribute_location", 0u);
				attribute.binding = (u16)vertex_input.value("attribute_binding", 0u);
				attribute.offset = vertex_input.value("attribute_offset", 0u);

				json attribute_format = vertex_input["attribute_format"];
				if (attribute_format.is_string()) {
					std::string name;
					attribute_format.get_to(name);

					for (u32 e = 0; e < VertexComponentFormat::Count; ++e) {
						VertexComponentFormat::Enum enum_value = (VertexComponentFormat::Enum)e;
						if (name == VertexComponentFormat::ToString(enum_value)) {
							attribute.format = enum_value;
							break;
						}
					}
				}
				else {
					HCRITICAL("Invalid vertex attribute format");
				}
				pc.vertex_input_creation.add_vertex_attribute(attribute);

				VertexStream vertex_stream{};

				vertex_stream.binding = (u16)vertex_input.value("stream_binding", 0u);
				vertex_stream.stride = (u16)vertex_input.value("stream_stride", 0u);

				json stream_rate = vertex_input["stream_rate"];
				if (stream_rate.is_string()) {
					std::string name;
					stream_rate.get_to(name);

					if (name == "Vertex") {
						vertex_stream.input_rate = VertexInputRate::PerVertex;
					}
					else if (name == "Instance") {
						vertex_stream.input_rate = VertexInputRate::PerInstance;
					}
					else {
						HCRITICAL("Invalid vertex stream rate");
					}
				}

				pc.vertex_input_creation.add_vertex_stream(vertex_stream);
			}
		}

		// Parse Depth
		json depth = pipeline["depth"];
		if (!depth.is_null()) {
			pc.depth_stencil_creation.depth_enable = 1;
			pc.depth_stencil_creation.depth_write_enable = depth.value("write", false);

			json comparison = depth["test"];
			if (comparison.is_string()) {
				std::string name;
				comparison.get_to(name);

				if (name == "less_or_equal") {
					pc.depth_stencil_creation.depth_comparison = VK_COMPARE_OP_LESS_OR_EQUAL;
				}
				else if (name == "equal") {
					pc.depth_stencil_creation.depth_comparison = VK_COMPARE_OP_EQUAL;
				}
				else if (name == "never") {
					pc.depth_stencil_creation.depth_comparison = VK_COMPARE_OP_NEVER;
				}
				else if (name == "always") {
					pc.depth_stencil_creation.depth_comparison = VK_COMPARE_OP_ALWAYS;
				}
				else {
					HCRITICAL("Invalid depth comparison test");
				}
			}
		}

		// Parse Blend States
		json blend_states = pipeline["blend"];
		if (!blend_states.is_null()) {

			for (sizet b = 0; b < blend_states.size(); ++b) {
				json blend = blend_states[b];

				std::string enabled = blend.value("enable", "");
				std::string src_colour = blend.value("src_colour", "");
				std::string dst_colour = blend.value("dst_colour", "");
				std::string blend_op = blend.value("op", "");

				BlendState& blend_state = pc.blend_state_creation.add_blend_state();
				blend_state.blend_enabled = (enabled == "true");
				blend_state.set_color(get_blend_factor(src_colour), get_blend_factor(dst_colour), get_blend_op(blend_op));
			}
		}

		json cull = pipeline["cull"];
		if (cull.is_string()) {
			std::string name;
			cull.get_to(name);

			if (name == "back") {
				pc.rasterization_creation.cull_mode = VK_CULL_MODE_BACK_BIT;
			}
			else if (name == "front") {
				pc.rasterization_creation.cull_mode = VK_CULL_MODE_FRONT_BIT;
			}
			else if (name == "none") {
				pc.rasterization_creation.cull_mode = VK_CULL_MODE_NONE;
			}
			else {
				HCRITICAL("Invalid cull mode");
			}
		}

		// Parse Render pass
		json render_pass = pipeline["render_pass"];
		if (render_pass.is_string()) {
			std::string name;
			render_pass.get_to(name);

			FrameGraphNode* node = frame_graph->get_node(name.c_str());

			if (node) {

				// TODO: handle better
				if (name == "swapchain") {
					pc.render_pass = renderer->gpu->get_swapchain_output();
				} else if (compute_shader_pass) {
					pc.render_pass = renderer->gpu->get_swapchain_output();
				} else {
					const RenderPass* render_pass = renderer->gpu->access_render_pass(node->render_pass);

					pc.render_pass = render_pass->output;
				}
			}
			else {
				HWARN("Cannot find render pass {}. Defaulting to swapchain", name.c_str());
				pc.render_pass = renderer->gpu->get_swapchain_output();
			}
		}
	}

	void shader_concatenate(cstring filename, StringBuffer& path_buffer, StringBuffer& shader_buffer, Allocator* temp_allocator) {

		// Read file and concatenate it
		path_buffer.clear();
		cstring shader_path = path_buffer.append_use_f("%s%s", HELIX_SHADER_FOLDER, filename);
		FileReadResult shader_read_result = file_read_text(shader_path, temp_allocator);
		if (shader_read_result.data) {
			// Append without null termination and add termination later.
			shader_buffer.append_m(shader_read_result.data, strlen(shader_read_result.data));
		}
		else {
			HERROR("Cannot read file {}", shader_path);
		}
	}

	VkBlendFactor get_blend_factor(const std::string factor) {
		if (factor == "ZERO") {
			return VK_BLEND_FACTOR_ZERO;
		}
		if (factor == "ONE") {
			return VK_BLEND_FACTOR_ONE;
		}
		if (factor == "SRC_COLOR") {
			return VK_BLEND_FACTOR_SRC_COLOR;
		}
		if (factor == "ONE_MINUS_SRC_COLOR") {
			return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
		}
		if (factor == "DST_COLOR") {
			return VK_BLEND_FACTOR_DST_COLOR;
		}
		if (factor == "ONE_MINUS_DST_COLOR") {
			return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
		}
		if (factor == "SRC_ALPHA") {
			return VK_BLEND_FACTOR_SRC_ALPHA;
		}
		if (factor == "ONE_MINUS_SRC_ALPHA") {
			return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		}
		if (factor == "DST_ALPHA") {
			return VK_BLEND_FACTOR_DST_ALPHA;
		}
		if (factor == "ONE_MINUS_DST_ALPHA") {
			return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
		}
		if (factor == "CONSTANT_COLOR") {
			return VK_BLEND_FACTOR_CONSTANT_COLOR;
		}
		if (factor == "ONE_MINUS_CONSTANT_COLOR") {
			return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
		}
		if (factor == "CONSTANT_ALPHA") {
			return VK_BLEND_FACTOR_CONSTANT_ALPHA;
		}
		if (factor == "ONE_MINUS_CONSTANT_ALPHA") {
			return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
		}
		if (factor == "SRC_ALPHA_SATURATE") {
			return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
		}
		if (factor == "SRC1_COLOR") {
			return VK_BLEND_FACTOR_SRC1_COLOR;
		}
		if (factor == "ONE_MINUS_SRC1_COLOR") {
			return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
		}
		if (factor == "SRC1_ALPHA") {
			return VK_BLEND_FACTOR_SRC1_ALPHA;
		}
		if (factor == "ONE_MINUS_SRC1_ALPHA") {
			return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
		}

		return VK_BLEND_FACTOR_ONE;
	}

	VkBlendOp get_blend_op(const std::string op) {
		if (op == "ADD") {
			VK_BLEND_OP_ADD;
		}
		if (op == "SUBTRACT") {
			VK_BLEND_OP_SUBTRACT;
		}
		if (op == "REVERSE_SUBTRACT") {
			VK_BLEND_OP_REVERSE_SUBTRACT;
		}
		if (op == "MIN") {
			VK_BLEND_OP_MIN;
		}
		if (op == "MAX") {
			VK_BLEND_OP_MAX;
		}

		return VK_BLEND_OP_ADD;
	}

}// namespace Helix