#include "PlayMode.hpp"

#include "LitColorTextureProgram.hpp"

#include "DrawLines.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"
#include "Scene.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <random>
#include <string>

#define AVOID_RECOLLIDE_OFFSET 0.05f

GLuint hexapod_meshes_for_lit_color_texture_program = 0;
Load< MeshBuffer > hexapod_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer const *ret = new MeshBuffer(data_path("platform-space.pnct"));
	hexapod_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
});

Load< Scene > hexapod_scene(LoadTagDefault, []() -> Scene const * {
	return new Scene(data_path("platform-space.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name){
		Mesh const &mesh = hexapod_meshes->lookup(mesh_name);

		scene.drawables.emplace_back(transform);
		Scene::Drawable &drawable = scene.drawables.back();

		drawable.pipeline = lit_color_texture_program_pipeline;

		drawable.pipeline.vao = hexapod_meshes_for_lit_color_texture_program;
		drawable.pipeline.type = mesh.type;
		drawable.pipeline.start = mesh.start;
		drawable.pipeline.count = mesh.count;
		drawable.pipeline.min = mesh.min;
		drawable.pipeline.max = mesh.max;

	});
});

Load< Sound::Sample > dusty_floor_sample(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("dusty-floor.opus"));
});

PlayMode::PlayMode() : scene(*hexapod_scene) {
	auto getBBox = [this](Scene scene, std::string name) {
		BBoxStruct newBBox;
		newBBox.min = glm::vec3(0.0f);
		newBBox.max = glm::vec3(0.0f);
		for (auto whichDrawable : scene.drawables) {
			if (whichDrawable.transform->name == name) {
				newBBox.min = whichDrawable.pipeline.min;
				newBBox.max = whichDrawable.pipeline.max;
			}
		}
		return newBBox;
	};
	//get pointers to leg for convenience:
	for (size_t c = 0; c < numPlatforms; c++) {
		platformArray[c] = nullptr;
	}
	for (auto &transform : scene.transforms) {
		std::string transformStr = std::string(transform.name);
		size_t whichPlat = 0;
		if (transformStr.size() > 7 && transformStr.substr(0, 7).c_str() == "Platform") {
			for (size_t ind = transformStr.size() - 1; ind >= 7; ind--) {
				whichPlat += (size_t)((uint8_t)transformStr.at(8) - 30) * (size_t)pow(10, (transformStr.size() - 1) - ind);
			}
			platformArray[whichPlat] = &transform;
			player->bbox = getBBox(scene, std::string(transform.name));
		}
		if (transform.name == "Player") {
			player = &transform;
			player->bbox = getBBox(scene, std::string("Player"));
		}
	}
	for (size_t c = 0; c < numPlatforms; c++) {
		std::string errorStr = std::string("Platform").append(std::to_string(c + 30)).append(std::string(" not found."));
		if (platformArray[c] == nullptr) throw std::runtime_error(errorStr.c_str());
	}
	if (player == nullptr) throw std::runtime_error("Platform not found.");

	//get pointer to camera for convenience:
	if (scene.cameras.size() != 1) throw std::runtime_error("Expecting scene to have exactly one camera, but it has " + std::to_string(scene.cameras.size()));
	camera = &scene.cameras.front();

	//start music loop playing:
	// (note: position will be over-ridden in update())
	bg_loop = Sound::loop_3D(*dusty_floor_sample, 1.0f, get_player_position(), 10.0f);
}

PlayMode::~PlayMode() {
}


bool PlayMode::bboxIntersect(BBoxStruct object, BBoxStruct stationary) {
	BBoxStruct smallerX = object;
	BBoxStruct largerX = stationary;
	if (object.max.x - object.min.x > stationary.max.x - stationary.min.x) {
		smallerX = stationary;
		largerX = object;
	}
	bool xBool = (smallerX.min.x >= largerX.min.x && smallerX.min.x <= largerX.max.x
		|| smallerX.max.x <= largerX.max.x && smallerX.max.x >= largerX.min.x);
	BBoxStruct smallerY = object;
	BBoxStruct largerY = stationary;
	if (object.max.y - object.min.y > stationary.max.y - stationary.min.y) {
		smallerY = stationary;
		largerY = object;
	}
	bool yBool = (smallerY.min.y >= largerY.min.y && smallerY.min.y <= largerY.max.y
		|| smallerY.max.y <= largerY.max.y && smallerY.max.y >= largerY.min.y);
	BBoxStruct smallerZ = object;
	BBoxStruct largerZ = stationary;
	if (object.max.z - object.min.z > stationary.max.z - stationary.min.z) {
		smallerZ = stationary;
		largerZ = object;
	}
	bool zBool = (smallerZ.min.z >= largerZ.min.z && smallerZ.min.z <= largerZ.max.z
		|| smallerZ.max.z <= largerZ.max.z && smallerZ.max.z >= largerZ.min.z);
	return xBool && yBool && zBool;
}

Collision PlayMode::bboxCollide(BBoxStruct object, BBoxStruct stationary) {
	auto distToSurf = [this](glm::vec3 point, glm::vec3 sideCenter) {
		return (sideCenter - point).length();
	};
	Collision retCol;
	retCol.sideCenter = glm::vec3(0.0f);
	retCol.collides = true;// bboxIntersect(object, stationary);
	glm::vec3 objCenter = object.min + (object.max - object.min) / 2.f;
	/*if (retCol.collides) {
		float minDist = INFINITY;
		glm::vec3 statCenter = stationary.min + (stationary.max - stationary.min) / 2;
		float statX = stationary.max.x - statCenter.x;
		float statY = stationary.max.y - statCenter.y;
		float statZ = stationary.max.z - statCenter.z;
		glm::vec3 sides[6] = {statCenter + glm::vec(statX,0.0f,0.0f).statCenter - glm::vec(statX,0.0f,0.0f) 
		statCenter + glm::vec(0.0f,statY,0.0f) ,statCenter + glm::vec(0.0f,statY,0.0f)
		statCenter + glm::vec(0.0f,0.0f,statZ) ,statCenter + glm::vec(0.0f,0.0f,statZ) }; //TO DO
		for (auto whichSide : sides) {
			float sideDist = distToSurf(objCenter, whichSide);
			if (sideDist < minDist) {
				minDist = sideDist;
				retCol.sideCenter = whichSide;
			}
		}
	}*/
	glm::vec3 statCenter = stationary.min + (stationary.max - stationary.min) / 2.f;
	float statZ = stationary.max.z - statCenter.z;
	retCol.sideCenter = statCenter + glm::vec3(0.0, 0.0, statZ);
	if (objCenter.z > statCenter.z + 0.00005f) retCol.collides = false;
	return retCol;
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_KEYDOWN) {
		if (evt.key.keysym.sym == SDLK_ESCAPE) {
			SDL_SetRelativeMouseMode(SDL_FALSE);
			return true;
		} else if (evt.key.keysym.sym == SDLK_a) {
			left.downs += 1;
			left.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			right.downs += 1;
			right.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			up.downs += 1;
			up.pressed = true;
			return true;
		}
		else if (evt.key.keysym.sym == SDLK_s) {
			down.downs += 1;
			down.pressed = true;
			return true;
		}
		else if (evt.key.keysym.sym == SDLK_SPACE) {
			space.downs += 1;
			space.pressed = true;
			return true;
		}
	} else if (evt.type == SDL_KEYUP) {
		if (evt.key.keysym.sym == SDLK_a) {
			left.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			right.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			up.pressed = false;
			return true;
		}
		else if (evt.key.keysym.sym == SDLK_s) {
			down.pressed = false;
			return true;
		}
		else if (evt.key.keysym.sym == SDLK_SPACE) {
			space.pressed = false;
			return true;
		}
	} else if (evt.type == SDL_MOUSEBUTTONDOWN) {
		if (SDL_GetRelativeMouseMode() == SDL_FALSE) {
			SDL_SetRelativeMouseMode(SDL_TRUE);
			return true;
		}
	} else if (evt.type == SDL_MOUSEMOTION) {
		if (SDL_GetRelativeMouseMode() == SDL_TRUE) {
			glm::vec2 motion = glm::vec2(
				evt.motion.xrel / float(window_size.y),
				-evt.motion.yrel / float(window_size.y)
			);
			camera->transform->rotation = glm::normalize(
				camera->transform->rotation
				* glm::angleAxis(-motion.x * camera->fovy, glm::vec3(0.0f, 1.0f, 0.0f))
				* glm::angleAxis(motion.y * camera->fovy, glm::vec3(1.0f, 0.0f, 0.0f))
			);
			return true;
		}
	}

	return false;
}

void PlayMode::songUpdate() {
	size_t t = bg_loop->i;
	size_t maxT = bg_loop->data.size();
	float currentTime = ((float) t) / ((float) maxT);
	canJump = true;
	std::cout << "current t fract = " << currentTime << std::endl;
}

void PlayMode::update(float elapsed) {

	songUpdate();

	if (!grounded && curPressTime >= maxPressTime) { //In air
		float oldJumpTime = curJumpTime;
		glm::vec3 oldJump = (glm::vec3(0.0f, 0.0f, gAcc)) / glm::vec3(2.f * (float)pow(oldJumpTime, 2)) + (curV0)*glm::vec3(oldJumpTime);
		curJumpTime += (elapsed - lastJump);
		glm::vec3 totalJump = glm::vec3(0.0f, 0.0f, gAcc / 2.f) * glm::vec3((float)pow(curJumpTime, 2)) + glm::vec3(curJumpTime) * curV0;
		glm::vec3 jumpDelta = totalJump - oldJump;
		player->position += jumpDelta;

	}
	else if (space.pressed && curPressTime < maxPressTime) {//Jump if possible
		if (!grounded || canJump) {
			grounded = false;
			float delta = elapsed - pressLast;
			pressLast = elapsed;
			curPressTime += delta;
			if (curPressTime > maxPressTime) curPressTime = maxPressTime;
			float oldJumpTime = curJumpTime;
			glm::vec3 oldJump = glm::vec3(0.0f, 0.0f, gAcc / 2.f) * glm::vec3((float)pow(oldJumpTime, 2)) + glm::vec3(oldJumpTime)* curV0;
			curV0.z = curPressTime * jumpFactor;
			curJumpTime += (elapsed - lastJump);
			glm::vec3 totalJump =  glm::vec3(0.0f, 0.0f, gAcc /2.f) * glm::vec3((float)pow(curJumpTime, 2)) + glm::vec3(curJumpTime) * curV0;
			glm::vec3 jumpDelta = totalJump - oldJump;
			player->position += jumpDelta;
		}
	}
	else{//Move player if not jump

		//combine inputs into a move:
		constexpr float PlayerSpeed = 30.0f;
		glm::vec2 move = glm::vec2(0.0f);
		if (left.pressed && !right.pressed) move.x =-1.0f;
		if (!left.pressed && right.pressed) move.x = 1.0f;
		if (down.pressed && !up.pressed) move.y =-1.0f;
		if (!down.pressed && up.pressed) move.y = 1.0f;

		//make it so that moving diagonally doesn't go faster:
		if (move != glm::vec2(0.0f)) move = glm::normalize(move) * PlayerSpeed * elapsed;
		player->position += glm::vec3(move.x, move.y, 0.0f);
		curV0 = glm::vec3(move.x, move.y, 0.0f);
	}

	//Collision check is last

	for (size_t whichPlatform = 0; whichPlatform < numPlatforms; whichPlatform++) {
		Scene::Transform* whichTransform = platformArray[whichPlatform];
		Collision collideRes = bboxCollide(player->bbox, whichTransform->bbox);
		if (collideRes.collides) {
			if (collideRes.sideCenter.z >= whichTransform->bbox.max.z - 0.000005f) {
				//std::cout << "on top of " << whichTransform->name << "\n";
				grounded = true;
				player->position.z = whichTransform->bbox.max.z + (player->bbox.max.z - player->bbox.min.z) / 2 + AVOID_RECOLLIDE_OFFSET;
			}
		}
	}
	/*
	//move camera:
	{

		//combine inputs into a move:
		constexpr float PlayerSpeed = 30.0f;
		glm::vec2 move = glm::vec2(0.0f);
		if (left.pressed && !right.pressed) move.x =-1.0f;
		if (!left.pressed && right.pressed) move.x = 1.0f;
		if (down.pressed && !up.pressed) move.y =-1.0f;
		if (!down.pressed && up.pressed) move.y = 1.0f;

		//make it so that moving diagonally doesn't go faster:
		if (move != glm::vec2(0.0f)) move = glm::normalize(move) * PlayerSpeed * elapsed;

		/*
		glm::mat4x3 frame = camera->transform->make_local_to_parent();
		glm::vec3 right = frame[0];
		//glm::vec3 up = frame[1];
		glm::vec3 forward = -frame[2];

		camera->transform->position += move.x * right + move.y * forward;
		player->position += glm::vec3(move.x, move.y, 0.0f);

	}*/


	{ //update listener to camera position:
		glm::mat4x3 frame = camera->transform->make_local_to_parent();
		glm::vec3 right = frame[0];
		glm::vec3 at = frame[3];
		Sound::listener.set_position_right(at, right, 1.0f / 60.0f);
	}

	//reset button press counters:
	left.downs = 0;
	right.downs = 0;
	up.downs = 0;
	down.downs = 0;
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	//update camera aspect ratio for drawable:
	camera->aspect = float(drawable_size.x) / float(drawable_size.y);

	//set up light type and position for lit_color_texture_program:
	// TODO: consider using the Light(s) in the scene to do this
	glUseProgram(lit_color_texture_program->program);
	glUniform1i(lit_color_texture_program->LIGHT_TYPE_int, 1);
	glUniform3fv(lit_color_texture_program->LIGHT_DIRECTION_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 0.0f,-1.0f)));
	glUniform3fv(lit_color_texture_program->LIGHT_ENERGY_vec3, 1, glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.95f)));
	glUseProgram(0);

	glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
	glClearDepth(1.0f); //1.0 is actually the default value to clear the depth buffer to, but FYI you can change it.
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS); //this is the default depth comparison function, but FYI you can change it.

	scene.draw(*camera);

	{ //use DrawLines to overlay some text:
		glDisable(GL_DEPTH_TEST);
		float aspect = float(drawable_size.x) / float(drawable_size.y);
		DrawLines lines(glm::mat4(
			1.0f / aspect, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		));

		constexpr float H = 0.09f;
		lines.draw_text("Mouse motion rotates camera; WASD moves; escape ungrabs mouse",
			glm::vec3(-aspect + 0.1f * H, -1.0 + 0.1f * H, 0.0),
			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
			glm::u8vec4(0x00, 0x00, 0x00, 0x00));
		float ofs = 2.0f / drawable_size.y;
		lines.draw_text("Mouse motion rotates camera; WASD moves; escape ungrabs mouse",
			glm::vec3(-aspect + 0.1f * H + ofs, -1.0 + + 0.1f * H + ofs, 0.0),
			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
			glm::u8vec4(0xff, 0xff, 0xff, 0x00));
	}
	GL_ERRORS();
}

glm::vec3 PlayMode::get_player_position() {
	//the vertex position here was read from the model in blender:
	return player->make_local_to_world() * glm::vec4(0.f, 0.f, 0.0f, 1.0f);
}
