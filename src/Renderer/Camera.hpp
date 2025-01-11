#include <Core/Platform.hpp>
#include <vendor/glm/glm/glm.hpp>


namespace Helix {
	struct InputService;


	struct Camera {
		void init(glm::vec3 position, f32 aspect_ratio, f32 z_near = 0.001f, f32 z_far = 1000.f);
		void update(InputService& input_handler, f32 delta_time);
		void on_resize();

		glm::vec3 position;
		glm::vec3 forward = { 0.0f, 0.0, -1.0f };
		glm::vec3 right = { 1.0f, 0.0, 0.0f };
		glm::vec3 up = { 0.0f, 1.0f, 0.0f };

		glm::mat4 view;
		glm::mat4 projection;
		glm::mat4 view_projection;

		f32 aspect_ratio;
		f32 move_speed;
		f32 x_sensitivity;
		f32 y_sensitivity;
		f32 yaw = 0.0f;
		f32 pitch = 0.0f;

		f32 z_near;
		f32 z_far;

		bool mouse_dragging = false;
		bool hovering_viewport = false;

	};
}// Helix