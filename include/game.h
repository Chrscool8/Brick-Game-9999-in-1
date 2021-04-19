#pragma once
#ifndef GAME_H
#define GAME_H


#include <string>
#include <array>
#include <optional>
#include <unistd.h>
#include "perf.hpp"
#include <utils.hpp>
#include <grid.hpp>
#include <vector>
#include <nanovg/framework/CMemPool.h>
#include <nanovg/framework/CApplication.h>
#include <nanovg/dk_renderer.hpp>
#include <games/subgame.h>

#define NumFramebuffers 2

class BrickGameFramework;

enum enum_orientation {
	orientation_normal,
	orientation_left_down,
	orientation_upside_down,
	orientation_right_down
};

extern vector<std::unique_ptr<subgame>> game_list;

class BrickGameFramework : public CApplication
{
private:
	int current_game;
	int next_game;
	std::string current_game_name;

	uint32_t FramebufferWidth;
	uint32_t FramebufferHeight;
	unsigned StaticCmdSize;

	dk::UniqueDevice device;
	dk::UniqueQueue queue;

	std::optional<CMemPool> pool_images;
	std::optional<CMemPool> pool_code;
	std::optional<CMemPool> pool_data;

	dk::UniqueCmdBuf cmdbuf;

	CMemPool::Handle depthBuffer_mem;
	CMemPool::Handle framebuffers_mem[NumFramebuffers];

	dk::Image depthBuffer;
	dk::Image framebuffers[NumFramebuffers];
	DkCmdList framebuffer_cmdlists[NumFramebuffers];
	dk::UniqueSwapchain swapchain;

	DkCmdList render_cmdlist;

	std::optional<nvg::DkRenderer> renderer;
	NVGcontext* vg;

	PerfGraph fps;
	float prevTime;

	double score = 0;
	double highscore = 0;

	std::string highscore_display = "";
	std::string score_display = "";

public:
	bool running;

	BrickGameFramework();
	~BrickGameFramework();
	void createFramebufferResources();
	void destroyFramebufferResources();
	void recordStaticCommands();
	void render(u64 ns);
	bool onFrame(u64 ns) override;

	vector<vector<bool>> game_grid;
	PadState pad;
	char screen_orientation;
	int transition_stage;
	double transition_percent;

	int target_grid_width = 10;
	int target_grid_height = 20;

	bool show_ui = true;

	bool debug_text = true;

	void setScore(int score);
	void setScore(double score);
	void setScore(std::string score);
	void incrementScore(int amount);
	void setHighScore(std::string score);

	void SwitchToGame(int i);
};

static bool fast_forward;
double fast_forwarder();

void renderGame(NVGcontext* vg, BrickGameFramework& game, float mx, float my, float width, float height, float t);
void load_sprite(NVGcontext* vg, std::string sprite_name, std::string sprite_path);

#endif // !GAME_H