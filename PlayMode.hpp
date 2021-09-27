#include "Mode.hpp"

#include "Scene.hpp"
#include "Sound.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <deque>

struct Collision{
	bool collides = false;
	glm::vec3 sideCenter; //Which side is closest to the object
};

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----

	//input tracking:
	struct Button {
		uint8_t downs = 0;
		uint8_t pressed = 0;
	} left, right, down, up, space,r;

	//local copy of the game scene (so code can change it during gameplay):
	Scene scene;

	//Player, goal, and platforms
	size_t numPlatforms = 26;
	Scene::Transform *platformArray[26];
	glm::quat platform_rotationArray[26];
	Scene::Transform *player = nullptr;
	glm::quat player_rotation;
	Scene::Transform playerOrigin;
	Scene::Transform* goal = nullptr;
	glm::quat goal_rotation;
	size_t numGems = 3;
	Scene::Transform* gemArray[3];
	glm::quat gem_rotationArray[3];
	float wobble = 0.0f;

	//Gameplay
	float maxPressTime = 0.13f; //Maximal # of seconds that jump press can increase
	float curPressTime = 0.0f;
	glm::vec3 curV0 = glm::vec3(0.0f); //Derived from above, used for velocity from jump, not object velocity
	glm::vec3 curVelocity = glm::vec3(0.0f); //Current velocity, used for motion
	float gAcc = 9.81f; //9.81 m/s^2
	bool grounded = true;
	float jumpFactor = 45.0f;//How much 1 second of jump adds to velocity (holding space)
	float curJumpTime = 0.0f;//Similar to press variables but for total time in air
	bool canJump = false; //controlled by beat
	bool jumpLock = false; //Avoids double jumps
	bool walled = false; //If colliding with a wall;
	float timer = 0;
	float endTime = 80.f;
	bool winBool = false;
	bool jumped = false; //Has a jump occured
	size_t score = 0;

	bool badSpace = false;// Player tries to jump during off-beat

	void resetGame();

	void songUpdate();

	glm::vec3 get_player_position();

	//music coming from the tip of the leg (as a demonstration):
	std::shared_ptr< Sound::PlayingSample > bg_loop;
	
	//camera:
	Scene::Camera *camera = nullptr;

	//Meshes:
	bool bboxIntersect(BBoxStruct object, BBoxStruct stationary); //Intersect bboxes and return true if collision
	Collision bboxCollide(Scene::Transform* object, Scene::Transform* stationary);

	//Shader
	float beatT = 0.0f;

};
