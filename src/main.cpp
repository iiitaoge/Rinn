#include"raylib.h"
#include<vector>

// 数据导向
struct Player {
	float x, y;
	float speed;
	int color; // 0xRRGGBBAA
};

// 接受一个 Player 的引用，这样才能修改它
void InputSystem(Player& player) {
	// 你的任务：
	// 1. 检测 IsKeyDown(KEY_W) -> y 减小
	// 2. 检测 IsKeyDown(KEY_S) -> y 增加
	// ... A 和 D 同理

	// 注意：一定要乘 GetFrameTime()
	// 否则方块在 144Hz 显示器上会比 60Hz 快两倍。
	float dt = GetFrameTime();
	if (IsKeyDown(KEY_W)) player.y -= player.speed * dt;
	// ... 继续写完
	if (IsKeyDown(KEY_S)) player.y += player.speed * dt;
	if (IsKeyDown(KEY_A)) player.x -= player.speed * dt;
	if (IsKeyDown(KEY_D)) player.x += player.speed * dt;
}

// 注意这里用了 const，因为渲染不应该修改数据
void RenderSystem(const Player& player) {
	DrawRectangle(
		(int)player.x,
		(int)player.y,
		50, 50, // 宽 50 高 50
		RED
	);
}

int main(){
	InitWindow(1600, 800, "Rinn"); // 初始化窗口
	SetTargetFPS(60); // 帧率

	// 1. 初始化实体 (Entity Creation)
	Player myPlayer = { 400.0f, 300.0f, 200.0f, 0xFF0000FF }; // 位置 400,300，速度 200

	while (!WindowShouldClose()) {

		InputSystem(myPlayer);

		BeginDrawing();
		ClearBackground(RAYWHITE);
		DrawFPS(10, 10);

		RenderSystem(myPlayer);
		EndDrawing();
	}
	CloseWindow();
	return 0;
}