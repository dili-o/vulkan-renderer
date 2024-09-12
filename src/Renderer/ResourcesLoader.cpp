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
	static void             parse_gpu_pipeline(nlohmann::json& pipeline, Helix::PipelineCreation& pc, Helix::StringBuffer& path_buffer,
		Helix::StringBuffer& shader_buffer, Helix::Allocator* temp_allocator, Helix::Renderer* renderer, Helix::FrameGraph* frame_graph);

	// ResourcesLoader //////////////////////////////////////////////////
	void ResourcesLoader::init(Helix::Renderer* renderer_, Helix::StackAllocator* temp_allocator_, Helix::FrameGraph* frame_graph_) {
		renderer = renderer_;
		temp_allocator = temp_allocator_;
		frame_graph = frame_graph_;
	}

	void ResourcesLoader::shutdown() {
	}

	void ResourcesLoader::load_gpu_technique(cstring json_path) {

		sizet allocated_marker = temp_allocator->get_marker();

		FileReadResult read_result = file_read_text(json_path, temp_allocator);

		StringBuffer path_buffer;
		path_buffer.init(1024, temp_allocator);

		using json = nlohmann::json;

		json json_data = json::parse(read_result.data);
	}

}// namespace Helix