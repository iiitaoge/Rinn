#include"raylib.h"
#include<vector>
class GameObject {
public:
	float x = 0, y = 0;
	bool active = true;
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

	void Update(float dt) override {
		if (!active) return; // 已“删除”的敌人不再处理

		changeTimer += dt;
		if (changeTimer >= changeInterval) {
			changeTimer = 0.0f;
			RandomizeDirection();
		}

		x += velocity.x * dt;
		y += velocity.y * dt;

		// 边界删除：圆心 + 半径判断（你的半径是 20）
		const float r = 20.0f;
		const float sw = (float)GetScreenWidth();
		const float sh = (float)GetScreenHeight();

		// 完全离开屏幕后才“删除”（更自然）
		if (x + r < 0 || x - r > sw || y + r < 0 || y - r > sh) {
			active = false;
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
	for (int i = 0; i < 10; ++i) {
		float x = (float)GetRandomValue(0, GetScreenWidth());
		float y = (float)GetRandomValue(0, GetScreenHeight());
		objects.push_back(new Enemy(x, y));
	}




	while (!WindowShouldClose()) {

		float dt = GetFrameTime();
		for (auto obj : objects) obj->Update(dt);
		// --- 这是一个陷阱 ---
		// 试图清理死掉的对象
		for (auto it = objects.begin(); it != objects.end(); ++it) {
			GameObject* obj = *it;
			if (!obj->active) {
				// 1. 释放内存 (这一步是安全的)
				delete obj;

				// 2. 从数组移除 (这一步是致命的！！！)
				// 你的迭代器 'it' 在这一瞬间失效了。
				// 但你的 for 循环头部的 '++it' 马上就要执行。
				objects.erase(it);
			}
		}
		BeginDrawing();
		ClearBackground(RAYWHITE);
		for (auto obj : objects) obj->Draw();
		DrawFPS(10, 10);

		EndDrawing();
	}
	CloseWindow();
	return 0;
}