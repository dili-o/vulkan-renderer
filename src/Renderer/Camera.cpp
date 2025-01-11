#include "Camera.hpp"

#include "Application/Input.hpp"

#include <vendor/glm/glm/gtc/matrix_transform.hpp>
#include <vendor/glm/glm/ext/vector_common.hpp>
#include <Core/Numerics.hpp>
#include <SDL_mouse.h>


namespace Helix {
	void Camera::init(glm::vec3 position_, f32 aspect_ratio_, f32 z_near_, f32 z_far_){
		position = position_;
		aspect_ratio = aspect_ratio_;

        z_near = z_near_;
        z_far = z_far_;

        x_sensitivity = 30.f;
        y_sensitivity = 30.f;
	}

	void Camera::update(InputService& input_handler, f32 delta_time){
        if (input_handler.is_mouse_down(MouseButtons::MOUSE_BUTTONS_RIGHT) && hovering_viewport ) {
            if (!mouse_dragging) {
                int unusedX, unusedY;
                SDL_GetRelativeMouseState(&unusedX, &unusedY);
            }
            int dx, dy;
            SDL_GetRelativeMouseState(&dx, &dy);

            yaw += dx * x_sensitivity * delta_time;
            pitch += dy * y_sensitivity * delta_time;

            pitch = glm::clamp(pitch, -60.0f, 60.0f);
            if (yaw > 360.0f) {
                yaw -= 360.0f;
            }

            glm::mat3 rxm = glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(-pitch), { 1.0f, 0.0f, 0.0f }));
            glm::mat3 rym = glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(-yaw), { 0.0f, 1.0f, 0.0f }));

            forward = rxm * glm::vec3(0.0f, 0.0f, -1.0f);
            forward = rym * forward;

            right = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));

            mouse_dragging = true;
        }
        else {
            mouse_dragging = false;
        }

        if (input_handler.is_key_down(Keys::KEY_W)) {
            position += forward * (5.0f * delta_time);
        }
        else if (input_handler.is_key_down(Keys::KEY_S)) {
            position -= forward * (5.0f * delta_time);
        }

        if (input_handler.is_key_down(Keys::KEY_D)) {
            position += right * (5.0f * delta_time);
        }
        else if (input_handler.is_key_down(Keys::KEY_A)) {
            position -= right * (5.0f * delta_time);
        }
        if (input_handler.is_key_down(Keys::KEY_E)) {
            position += up * (5.0f * delta_time);
        }
        else if (input_handler.is_key_down(Keys::KEY_Q)) {
            position -= up * (5.0f * delta_time);
        }

        // Update Matrices
        view = glm::lookAt(position, (position + forward), glm::vec3(0.0f, 1.0f, 0.0f));
        projection = glm::perspective(glm::radians(60.0f), aspect_ratio, z_near, z_far);
        view_projection = projection * view;
	}

	void Camera::on_resize(){

	}
}// Helix