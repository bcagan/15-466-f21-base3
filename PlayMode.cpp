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
#define DEATH_LAYER -10.0f

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

Load< Sound::Sample > mainMusic(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("A-Stellar-Jaunt.wav"));
});

PlayMode::PlayMode() : scene(*hexapod_scene) {
	beatT = 0.0f;
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
	for (size_t c = 0; c < numGems; c++) {
		gemArray[c] = nullptr;
	}
	for (auto &transform : scene.transforms) {
		std::string transformStr = std::string(transform.name);
		size_t whichPlat = 0;
		size_t whichGem = 0;
		if (transformStr.size() > 8 && transformStr.substr(0, 8) == std::string("Platform")) {
			for (size_t ind = transformStr.size() - 1; ind >= 8; ind--) {
				whichPlat += (size_t)((uint8_t)transformStr.at(ind) - 48) * (size_t)pow(10, (transformStr.size() - 1) - ind);
			}
			platformArray[whichPlat] = &transform;
			BBoxStruct useBBox = getBBox(scene, std::string(transform.name));
			(platformArray[whichPlat])->bbox = useBBox;
		}
		if (transformStr.size() > 3 && transformStr.substr(0, 3) == std::string("Gem")) {
			for (size_t ind = transformStr.size() - 1; ind >= 3; ind--) {
				whichGem += (size_t)((uint8_t)transformStr.at(ind) - 48) * (size_t)pow(10, (transformStr.size() - 1) - ind);
			}
			gemArray[whichGem] = &transform;
			BBoxStruct useBBox = getBBox(scene, std::string(transform.name));
			(gemArray[whichGem])->bbox = useBBox;
		}
		else if (transform.name == "Player") {
			player = &transform;
			player->bbox = getBBox(scene, std::string("Player"));
			playerOrigin = *player;
		}
		else if (transform.name == "Goal") {
			goal = &transform;
			goal->bbox = getBBox(scene, std::string("Goal"));
		}
	}
	for (size_t c = 0; c < numPlatforms; c++) {
		std::string errorStr = std::string("Platform").append(std::to_string(c)).append(std::string(" not found."));
		if (platformArray[c] == nullptr) throw std::runtime_error(errorStr.c_str());
	}
	if (player == nullptr) throw std::runtime_error("Platform not found.");

	//get pointer to camera for convenience:
	if (scene.cameras.size() != 1) throw std::runtime_error("Expecting scene to have exactly one camera, but it has " + std::to_string(scene.cameras.size()));
	camera = &scene.cameras.front();

	//start music loop playing:
	// (note: position will be over-ridden in update())
	bg_loop = Sound::loop_3D(*mainMusic, 1.0f, get_player_position(), 10.0f);
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

Collision PlayMode::bboxCollide(Scene::Transform *object, Scene::Transform *stationary) {
	auto distToSurf = [this](glm::vec3 point, glm::vec3 sideCenter) {
		return glm::length(sideCenter - point);
	};
	Collision retCol;
	retCol.sideCenter = glm::vec3(0.0f);

	//Creating bbox strucuts
	BBoxStruct objectStruct; BBoxStruct stationaryStruct;
	glm::vec3 omin = object->bbox.min; glm::vec3 omax = object->bbox.min;
	glm::vec3 smin = stationary->bbox.min; glm::vec3 smax = stationary->bbox.min;
	objectStruct.min = object->make_local_to_world() * glm::vec4(omin.x,omin.y,omin.z, 1.0f);
	objectStruct.max = object->make_local_to_world() * glm::vec4(omax.x,omax.y,omax.z, 1.0f);
	stationaryStruct.min = stationary->make_local_to_world() * glm::vec4(stationary->bbox.min, 1.0f);
	stationaryStruct.max = stationary->make_local_to_world() * glm::vec4(stationary->bbox.max, 1.0f);

	retCol.collides =  bboxIntersect(objectStruct, stationaryStruct);
	glm::vec3 objCenter =object->make_local_to_world() *
		glm::vec4(object->bbox.min + (object->bbox.max - object->bbox.min) / 2.f, 1.0f);
	if (retCol.collides) {
		float minDist = INFINITY;
		glm::vec3 statCenter = stationary->bbox.min + stationary->make_local_to_world() *
			glm::vec4((stationary->bbox.max - stationary->bbox.min) / 2.f, 1.0f);
		float statX = stationary->scale.x*(stationary->bbox.max.x - stationary->bbox.min.x)/2.f;
		float statY = stationary->scale.y * (stationary->bbox.max.y - stationary->bbox.min.y) / 2.f;
		float statZ = stationary->scale.z * (stationary->bbox.max.z - stationary->bbox.min.z) / 2.f;
		glm::vec3 sides[6];
		sides[0] = statCenter + glm::vec3(statX, 0.0f, 0.0f);
		sides[1] = statCenter - glm::vec3(statX, 0.0f, 0.0f);
		sides[2] = statCenter + glm::vec3(0.0f, statY, 0.0f);
		sides[3] = statCenter - glm::vec3(0.0f, statY, 0.0f);
		sides[4] = statCenter + glm::vec3(0.0f, 0.0f, statZ);
		sides[5] = statCenter - glm::vec3(0.0f, 0.0f, statZ);
		for (size_t sideInd = 0; sideInd < 6; sideInd++) {
			glm::vec3 whichSide = sides[sideInd];
			float sideDist = distToSurf(objCenter, whichSide);
			if (sideDist < minDist) {
				minDist = sideDist;
				retCol.sideCenter = whichSide;
			}
		}

		glm::vec3 retAxis = glm::normalize(retCol.sideCenter - statCenter);
		retCol.sideCenter = retAxis;
	}
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
		else if (evt.key.keysym.sym == SDLK_r) {
			r.downs += 1;
			r.pressed = true;
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
		else if (evt.key.keysym.sym == SDLK_r) {
			r.pressed = false;
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

void PlayMode::songUpdate() { //Update to see if the player can jump, and if the blocks should flash
	//Song is 3 blocks of 4, each 8 measures with a beat on the odd measures. So 8*4*3 = 96 measures
	size_t t = bg_loop->i;
	size_t maxT = bg_loop->data.size();
	float currentTime = ((float) t) / ((float) maxT); //Get fractional time stamp

	currentTime *= 96.f;
	int currentInt = (int)currentTime;
	if (currentInt % 2 == 0) canJump = true;
	else canJump = false; //Every other measure can jump

	float inT = currentTime - (float) currentInt;
	if (inT < 0.0f) inT = 0.0f;
	else if (inT > 1.0f) inT = 0.0f;
	if (currentInt % 2 == 1) {
		if (inT <= 0.333) {
			inT *= 3.0f;
			inT = 1.0f - inT;
		}
		else inT = 0.0f;
	}
	beatT = inT; //Gets percentage time of current measure in [0.0,1.0] for flashing blocks light blue with beat
	//For first 1/3 of following measure, interpolate back to texture
}

void PlayMode::resetGame() {
	*player = playerOrigin;

	//Reset gameplay vars
	curPressTime = 0.0f;
	curV0 = glm::vec3(0.0f); //Derived from above, used for velocity from jump, not object velocity
	curVelocity = glm::vec3(0.0f); //Current velocity, used for motion
	grounded = true;
	curJumpTime = 0.0f;//Similar to press variables but for total time in air
	canJump = false; //controlled by beat
	jumpLock = false; //Avoids double jumps
	walled = false; //If colliding with a wall;
	timer = 0.f;
	winBool = false;
	score = 0;
	for (size_t c = 0; c < numGems; c++) {
		gemArray[c]->doDraw = true;
	}

}

void PlayMode::update(float elapsed) {

	auto winCheck = [this]() {
		//Creating bbox strucuts
		BBoxStruct playerStruct; BBoxStruct goalStruct;
		glm::vec3 pmin = player->bbox.min; glm::vec3 pmax = player->bbox.min;
		glm::vec3 gmin = goal->bbox.min; glm::vec3 gmax = goal->bbox.min;
		playerStruct.min = player->make_local_to_world() * glm::vec4(player->bbox.min, 1.0f);
		playerStruct.max = player->make_local_to_world() * glm::vec4(player->bbox.max, 1.0f);
		goalStruct.min = goal->make_local_to_world() * glm::vec4(goal->bbox.min, 1.0f);
		goalStruct.max = goal->make_local_to_world() * glm::vec4(goal->bbox.max, 1.0f);
		return bboxIntersect(playerStruct, goalStruct);
	};

	if(!winBool) timer += elapsed;
	if (timer >= endTime) resetGame();

	songUpdate();

	auto aboveCollision = [this]() {
		bool above = false;
		BBoxStruct newPlayer;
		newPlayer.min = player->make_local_to_world() * glm::vec4(player->bbox.min, 1.0);
		newPlayer.max = player->make_local_to_world() * glm::vec4(player->bbox.max, 1.0);
		for (size_t whichPlatform = 0; whichPlatform < numPlatforms; whichPlatform++) {
			Scene::Transform* whichTransform = platformArray[whichPlatform];
			BBoxStruct newPlatform;
			newPlatform.min = whichTransform->make_local_to_world() * glm::vec4(whichTransform->bbox.min, 1.0);
			newPlatform.max = whichTransform->make_local_to_world() * glm::vec4(whichTransform->bbox.max, 1.0);
			if (newPlayer.min.x >= newPlatform.min.x && newPlayer.max.x < newPlatform.max.x
				&& newPlayer.min.y >= newPlatform.min.y && newPlayer.max.y < newPlatform.max.y) above = true;
		}
		return above;
	};


	//Collision check
	for (size_t whichPlatform = 0; whichPlatform < numPlatforms; whichPlatform++) {
		Scene::Transform* whichTransform = platformArray[whichPlatform];
		Collision collideRes = bboxCollide(player, whichTransform);
		if (collideRes.collides) {
			//Get offset
			glm::vec3 absAxis = glm::vec3(abs(collideRes.sideCenter.x), abs(collideRes.sideCenter.y), abs(collideRes.sideCenter.z));
			glm::vec3 axisDif = whichTransform->scale * collideRes.sideCenter * (whichTransform->bbox.max - whichTransform->bbox.min) / glm::vec3(2.f);
			glm::vec3 offset = whichTransform->make_local_to_world()*glm::vec4(axisDif,1.0f);
			offset = absAxis * offset;
			glm::vec3 playerDif = collideRes.sideCenter * player->scale *(
				(player->bbox.max - player->bbox.min) / glm::vec3(2.f) );

			player->position *= (glm::vec3(1.0f) -absAxis);
			player->position += offset + playerDif;

			if (collideRes.sideCenter.z > 0.0f) {
				grounded = true;
				jumpLock = false;
				curJumpTime = 0.0f;
				curPressTime = 0.0f;
				walled = false;
				jumped = false;
			}
			else {
				walled = true;
			}
		}
	}
	if (!aboveCollision()) { //Checks to see if not above a surface, if so, know not grounded
		walled = false;
		grounded = false;
	}

	//Gem collection check
	for (size_t whichGem = 0; whichGem < numGems; whichGem++) {
		Scene::Transform* whichTransform = gemArray[whichGem];
		Collision collideRes = bboxCollide(player, whichTransform);
		if (collideRes.collides && whichTransform->doDraw) {
			gemArray[whichGem]->doDraw = false;
			score += 200;
		}
	}

	//Death check
	if (player->position.z <= DEATH_LAYER) {
		resetGame();
	}

	//Win check
	if (winCheck()) {
		if(!winBool) score += (size_t)(endTime - timer) * 10;
		winBool = true;
	}
	if (r.pressed && winBool) resetGame();

	if (!winBool) {
		//combine inputs into a move:
		glm::vec2 move = glm::vec2(0.0f);
		if (left.pressed && !right.pressed) move.x = -1.0f;
		if (!left.pressed && right.pressed) move.x = 1.0f;
		if (down.pressed && !up.pressed) move.y = -1.0f;
		if (!down.pressed && up.pressed) move.y = 1.0f;
		constexpr float PlayerSpeed = 5.0f;
		//make it so that moving diagonally doesn't go faster:
		if (move != glm::vec2(0.0f)) move = glm::normalize(move) * PlayerSpeed * elapsed;
		if (!grounded && (!space.pressed || curPressTime >= maxPressTime) && !jumpLock && jumped) jumpLock = true;
		if (!jumpLock && space.pressed && curPressTime < maxPressTime) {//Jump if possible
			if (!grounded || canJump) {
				jumped = true;
				grounded = false;
				curPressTime += elapsed;
				if (curPressTime > maxPressTime) curPressTime = maxPressTime;
				float oldJumpTime = curJumpTime;
				glm::vec3 oldJump = glm::vec3(0.0f, 0.0f, -gAcc / 2.f) * glm::vec3((float)pow(oldJumpTime, 2)) + glm::vec3(oldJumpTime) * curV0;
				curV0.z = curPressTime * jumpFactor;
				curJumpTime += elapsed;
				glm::vec3 totalJump = glm::vec3(0.0f, 0.0f, -gAcc / 2.f) * glm::vec3((float)pow(curJumpTime, 2)) + glm::vec3(curJumpTime) * curV0;
				glm::vec3 jumpDelta = totalJump - oldJump;
				player->position += jumpDelta;
				if (!walled) player->position += glm::vec3(move.x, move.y, 0.0f);
			}
		}
		else  if (!grounded && jumpLock) { //In air
			float oldJumpTime = curJumpTime;
			glm::vec3 oldJump = (glm::vec3(0.0f, 0.0f, -gAcc / 2.f)) * glm::vec3((float)pow(oldJumpTime, 2)) + (curV0)*glm::vec3(oldJumpTime);
			curJumpTime += elapsed;
			glm::vec3 totalJump = glm::vec3(0.0f, 0.0f, -gAcc / 2.f) * glm::vec3((float)pow(curJumpTime, 2)) + glm::vec3(curJumpTime) * curV0;
			glm::vec3 jumpDelta = totalJump - oldJump;
			player->position += jumpDelta;
			if (!walled) player->position += glm::vec3(move.x, move.y, 0.0f);
		}
		else if (!grounded) {//falling
			float oldJumpTime = curJumpTime;
			glm::vec3 oldJump = (glm::vec3(0.0f, 0.0f, -gAcc / 2.f)) * glm::vec3((float)pow(oldJumpTime, 2));
			curJumpTime += elapsed;
			glm::vec3 totalJump = glm::vec3(0.0f, 0.0f, -gAcc / 2.f) * glm::vec3((float)pow(curJumpTime, 2));
			glm::vec3 jumpDelta = totalJump - oldJump;
			player->position += jumpDelta;
			if (!walled) player->position += glm::vec3(move.x, move.y, 0.0f);

		}
		else {//Move player if not jump
			player->position += glm::vec3(move.x, move.y, 0.0f);
		}
		camera->transform->position = player->position + glm::vec3(0.0, -15.0f, 3.0f);
	}


	{ //update listener to camera position:
		glm::mat4x3 frame = camera->transform->make_local_to_parent();
		glm::vec3 right = frame[0];
		glm::vec3 at = frame[3];
		Sound::listener.set_position_right(at, right, 1.0f / 60.0f);
	}

	{//update music position
		bg_loop->set_position(get_player_position(),bg_loop->pan.ramp);
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
	glUseProgram(lit_color_texture_program->program);
	glUniform1i(lit_color_texture_program->LIGHT_TYPE_int, 1);
	glUniform3fv(lit_color_texture_program->LIGHT_DIRECTION_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 0.0f,-1.0f)));
	glUniform3fv(lit_color_texture_program->LIGHT_ENERGY_vec3, 1, glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.95f)));
	scene.t = beatT;
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
		std::string timerStr = (std::string("; Time left: ")).append(std::to_string(int(endTime - timer)));
		std::string scoreStr = (std::string("; Score: ")).append(std::to_string(score + 10*(int)(endTime - timer)));
		std::string useStr = (std::string("Mouse motion rotates camera; WASD moves; escape ungrabs mouse")).append(timerStr).append(scoreStr);
		if (winBool) {
			useStr = (std::string("You won! You got: ")).append(std::to_string(score)).append(std::string(" points! Press R to replay!"));
		}
		lines.draw_text(useStr.c_str(),
			glm::vec3(-aspect + 0.1f * H, -1.0 + 0.1f * H, 0.0),
			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
			glm::u8vec4(0x00, 0x00, 0x00, 0x00));
		float ofs = 2.0f / drawable_size.y;
		lines.draw_text(useStr.c_str(),
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
