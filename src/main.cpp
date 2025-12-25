#include"raylib.h"
#include<vector>
class GameObject {
protected:
	float x = 0, y = 0;
	bool active = true;
public:
	// 虚函数：多态
	virtual void Update(float dt) = 0;
	virtual void Draw() = 0;
	virtual ~GameObject() {}
};

class Player : public GameObject {
public:

	float speed = 200.0f;
	void Update(float dt) override {
		// 键盘控制逻辑...
		if (IsKeyDown(KEY_W)) y -= speed * dt;
		if (IsKeyDown(KEY_S)) y += speed * dt;
		if (IsKeyDown(KEY_A)) x -= speed * dt;
		if (IsKeyDown(KEY_D)) x += speed * dt;
	}
	void Draw() override {
		DrawRectangle(x, y, 40, 40, BLUE);
	}
};

class Enemy : public GameObject {
public:
	Vector2 velocity;        // 当前速度向量
	float speed = 120.0f;    // 标量速度
	float changeTimer = 0.0f;
	float changeInterval = 2.0f; // 每 2 秒换一次方向

	Enemy(float px, float py) {
		x = px;
		y = py;
		RandomizeDirection();
	}

	void Update(float dt) override{
		changeTimer += dt;
		if (changeTimer >= changeInterval) {
			changeTimer = 0.0f;
			RandomizeDirection();
		}

		x += velocity.x * dt;
		y += velocity.y * dt;

		// 边界反弹
		HandleBoundary();
	}
	// 边界反弹
	void HandleBoundary() {
		const float radius = 20.0f;
		const int screenW = GetScreenWidth();
		const int screenH = GetScreenHeight();

		if (x < radius) {
			x = radius;
			velocity.x *= -1;
		}
		if (x > screenW - radius) {
			x = screenW - radius;
			velocity.x *= -1;
		}
		if (y < radius) {
			y = radius;
			velocity.y *= -1;
		}
		if (y > screenH - radius) {
			y = screenH - radius;
			velocity.y *= -1;
		}
	}


	void Draw() override{
		DrawCircle((int)x, (int)y, 20, RED);
	}

private:
	void RandomizeDirection() {
		float angle = GetRandomValue(0, 360) * DEG2RAD;
		velocity.x = cosf(angle) * speed;
		velocity.y = sinf(angle) * speed;
	}

};

int main(){

	InitWindow(2000, 1200, "Rinn"); // 初始化窗口
	SetTargetFPS(60); // 帧率

	std::vector<GameObject*> objects;
	objects.push_back(new Player);
	for (int i = 0; i < 100000; ++i) {
		float x = (float)GetRandomValue(0, GetScreenWidth());
		float y = (float)GetRandomValue(0, GetScreenHeight());
		objects.push_back(new Enemy(x, y));
	}




	while (!WindowShouldClose()) {

		float dt = GetFrameTime();
		for (auto obj : objects) obj->Update(dt);
		BeginDrawing();
		ClearBackground(RAYWHITE);
		for (auto obj : objects) obj->Draw();
		DrawFPS(10, 10);

		EndDrawing();
	}
	CloseWindow();
	return 0;
}