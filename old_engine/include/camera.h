#pragma once
#include "vk_types.h"
#include <SDL2/SDL_events.h>

class Camera {
public:
	glm::vec3 velocity;
	glm::vec3 position;
	float pitch{ 0.0f };
	float yaw{ 0.0f };

	bool w_down = false;
	bool a_down = false;
	bool s_down = false;
	bool d_down = false;

	bool shouldClose = false;
	bool shouldUnfocus = false;

	glm::mat4 getViewMatrix();
	glm::mat4 getRotationMatrix();

	void processSDLEvent(SDL_Event& e);

	void update();
};
