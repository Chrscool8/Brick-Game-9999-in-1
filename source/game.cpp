#include <array>
#include <optional>
#include <unistd.h>
#include "perf.hpp"
#include <utils.hpp>
#include <grid.hpp>
#include <vector>
#include <game.h>
#include "nanovg.h"
#include "nanovg_dk.h"
#include <nanovg/framework/CMemPool.h>
#include <nanovg/framework/CApplication.h>
#include <switch/runtime/devices/socket.h>
#include <grid_sprites.h>
#include <games/game_snake.h>
#include <games/game_menu.h>
#include "audio.h"

static int nxlink_sock = -1;

using namespace std;

vector<std::unique_ptr<subgame>> game_list;

extern "C" void userAppInit(void)
{
	romfsInit();
	socketInitialize(NULL);
	nxlink_sock = nxlinkConnectToHost(true, true);
}

extern "C" void userAppExit(void)
{
	if (nxlink_sock != -1)
		close(nxlink_sock);
	socketExit();
	romfsExit();
}

std::map<std::string, int> sprite_indicies;

void load_sprite(NVGcontext* vg, std::string sprite_name, std::string sprite_path)
{
	sprite_indicies[sprite_name] = nvgCreateImage(vg, sprite_path.c_str(), NVG_IMAGE_NEAREST);
	if (sprite_indicies[sprite_name] == 0)
		printf(("Problem loading " + sprite_name + "\n").c_str());
	else
		printf(("Loaded " + sprite_name + " to index " + std::to_string(sprite_indicies[sprite_name]) + "\n").c_str());
}

bool draw_sprite(NVGcontext* vg, float x, float y, float width, float height, std::string sprite_name)
{
	if (sprite_indicies.count(sprite_name) == 0)
	{
		printf(("Trying to draw unloaded sprite, " + sprite_name + "\n").c_str());
		return false;
	}
	else
	{
		//printf(("Trying to draw loaded sprite, " + sprite_name + "\n").c_str());
		nvgSave(vg);
		nvgScissor(vg, x, y, width, height);
		nvgTranslate(vg, x, y);

		NVGpaint imgPaint = nvgImagePattern(vg, 0, 0, width, height, 0.0f / 180.0f * NVG_PI, sprite_indicies[sprite_name], 1.0f);
		nvgBeginPath(vg);
		nvgRect(vg, 0, 0, width, height);
		nvgFillPaint(vg, imgPaint);
		nvgFill(vg);
		nvgRestore(vg);
		return true;
	}
}

void OutputDkDebug(void* userData, const char* context, DkResult result, const char* message)
{
	printf("Context: %s\nResult: %d\nMessage: %s\n", context, result, message);
}

BrickGameFramework::BrickGameFramework()
{
	FramebufferWidth = 1280;
	FramebufferHeight = 720;
	StaticCmdSize = 0x1000;

	// Create the deko3d device
	device = dk::DeviceMaker{}.setCbDebug(OutputDkDebug).create();

	// Create the main queue
	queue = dk::QueueMaker{ device }.setFlags(DkQueueFlags_Graphics).create();

	// Create the memory pools
	pool_images.emplace(device, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image, 16 * 1024 * 1024);
	pool_code.emplace(device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code, 128 * 1024);
	pool_data.emplace(device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, 1 * 1024 * 1024);

	// Create the static command buffer and feed it freshly allocated memory
	cmdbuf = dk::CmdBufMaker{ device }.create();
	CMemPool::Handle cmdmem = pool_data->allocate(StaticCmdSize);
	cmdbuf.addMemory(cmdmem.getMemBlock(), cmdmem.getOffset(), cmdmem.getSize());
	printf("cmdmem size: %u\n", cmdmem.getSize());
	// Create the framebuffer resources
	createFramebufferResources();

	this->renderer.emplace(FramebufferWidth, FramebufferHeight, this->device, this->queue, *this->pool_images, *this->pool_code, *this->pool_data);
	this->vg = nvgCreateDk(&*this->renderer, NVG_DEBUG);

	initGraph(&fps, GRAPH_RENDER_FPS, "Frame Time");

	// Load Resources
	load_sprite(vg, "spr_cell_selected", "romfs:/images/cell_selected.png");
	load_sprite(vg, "spr_cell_unselected", "romfs:/images/cell_unselected.png");

	for (int i = 0; i < 16; i++)
	{
		std::string num = std::to_string(i);
		if (i < 10)
			num = "0" + num;
		load_sprite(vg, "spr_cells_" + num, "romfs:/images/cells_" + num + ".png");
	}

	int fontIcons = nvgCreateFont(vg, "icons", "romfs:/fonts/entypo.ttf");
	if (fontIcons == -1) {
		printf("Could not add font icons.\n");
	}
	int fontNormal = nvgCreateFont(vg, "sans", "romfs:/fonts/Roboto-Regular.ttf");
	if (fontNormal == -1) {
		printf("Could not add font italic.\n");
	}
	int fontBold = nvgCreateFont(vg, "sans-bold", "romfs:/fonts/Roboto-Bold.ttf");
	if (fontBold == -1) {
		printf("Could not add font bold.\n");
	}
	int fontEmoji = nvgCreateFont(vg, "emoji", "romfs:/fonts/NotoEmoji-Regular.ttf");
	if (fontEmoji == -1) {
		printf("Could not add font emoji.\n");
	}
	int fontSegment = nvgCreateFont(vg, "seg", "romfs:/fonts/DSEG7Classic-Bold.ttf");
	if (fontSegment == -1) {
		printf("Could not add font segment.\n");
	}
	int fontMinecraft = nvgCreateFont(vg, "minecraft", "romfs:/fonts/Minecraft.ttf");
	if (fontMinecraft == -1) {
		printf("Could not add font minecraft.\n");
	}
	int fontKongtext = nvgCreateFont(vg, "kongtext", "romfs:/fonts/kongtext-regular.ttf");
	if (fontKongtext == -1) {
		printf("Could not add font Kongtext.\n");
	}

	nvgAddFallbackFontId(vg, fontNormal, fontEmoji);
	nvgAddFallbackFontId(vg, fontBold, fontEmoji);


	printf("Done loading\n");

	//

	current_game = -1;
	next_game = 0;
	screen_orientation = orientation_normal;

	transition_stage = -1;
	transition_percent = 0;

	padConfigureInput(1, HidNpadStyleSet_NpadStandard);
	padInitializeDefault(&pad);

	game_grid = grid_create(10, 20);

	game_list.push_back(std::make_unique<subgame_menu>(*this));
	game_list.push_back(std::make_unique<subgame_snake>(*this));
}

BrickGameFramework::~BrickGameFramework()
{
	// Destroy the framebuffer resources. This should be done first.
	destroyFramebufferResources();

	// Cleanup vg. This needs to be done first as it relies on the renderer.
	nvgDeleteDk(vg);

	// Destroy the renderer
	this->renderer.reset();
}

void BrickGameFramework::createFramebufferResources()
{
	// Create layout for the depth buffer
	dk::ImageLayout layout_depthbuffer;
	dk::ImageLayoutMaker{ device }
		.setFlags(DkImageFlags_UsageRender | DkImageFlags_HwCompression)
		.setFormat(DkImageFormat_S8)
		.setDimensions(FramebufferWidth, FramebufferHeight)
		.initialize(layout_depthbuffer);

	// Create the depth buffer
	depthBuffer_mem = pool_images->allocate(layout_depthbuffer.getSize(), layout_depthbuffer.getAlignment());
	depthBuffer.initialize(layout_depthbuffer, depthBuffer_mem.getMemBlock(), depthBuffer_mem.getOffset());

	// Create layout for the framebuffers
	dk::ImageLayout layout_framebuffer;
	dk::ImageLayoutMaker{ device }
		.setFlags(DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression)
		.setFormat(DkImageFormat_RGBA8_Unorm)
		.setDimensions(FramebufferWidth, FramebufferHeight)
		.initialize(layout_framebuffer);

	// Create the framebuffers
	std::array<DkImage const*, NumFramebuffers> fb_array;
	uint64_t fb_size = layout_framebuffer.getSize();
	uint32_t fb_align = layout_framebuffer.getAlignment();
	for (unsigned i = 0; i < NumFramebuffers; i++)
	{
		// Allocate a framebuffer
		framebuffers_mem[i] = pool_images->allocate(fb_size, fb_align);
		framebuffers[i].initialize(layout_framebuffer, framebuffers_mem[i].getMemBlock(), framebuffers_mem[i].getOffset());

		// Generate a command list that binds it
		dk::ImageView colorTarget{ framebuffers[i] }, depthTarget{ depthBuffer };
		cmdbuf.bindRenderTargets(&colorTarget, &depthTarget);
		framebuffer_cmdlists[i] = cmdbuf.finishList();

		// Fill in the array for use later by the swapchain creation code
		fb_array[i] = &framebuffers[i];
	}

	// Create the swapchain using the framebuffers
	swapchain = dk::SwapchainMaker{ device, nwindowGetDefault(), fb_array }.create();

	// Generate the main rendering cmdlist
	recordStaticCommands();
}

void BrickGameFramework::destroyFramebufferResources()
{
	// Return early if we have nothing to destroy
	if (!swapchain) return;

	// Make sure the queue is idle before destroying anything
	queue.waitIdle();

	// Clear the static cmdbuf, destroying the static cmdlists in the process
	cmdbuf.clear();

	// Destroy the swapchain
	swapchain.destroy();

	// Destroy the framebuffers
	for (unsigned i = 0; i < NumFramebuffers; i++)
		framebuffers_mem[i].destroy();

	// Destroy the depth buffer
	depthBuffer_mem.destroy();
}

void BrickGameFramework::recordStaticCommands()
{
	// Initialize state structs with deko3d defaults
	dk::RasterizerState rasterizerState;
	dk::ColorState colorState;
	dk::ColorWriteState colorWriteState;
	dk::BlendState blendState;

	// Configure the viewport and scissor
	cmdbuf.setViewports(0, { { 0.0f, 0.0f, (float)FramebufferWidth, (float)FramebufferHeight, 0.0f, 1.0f } });
	cmdbuf.setScissors(0, { { 0, 0, FramebufferWidth, FramebufferHeight } });

	// Clear the color and depth buffers
	cmdbuf.clearColor(0, DkColorMask_RGBA, 109.f / 255.f, 120.f / 255.f, 92.f / 255.f, 1.0f);
	cmdbuf.clearDepthStencil(true, 1.0f, 0xFF, 0);

	// Bind required state
	cmdbuf.bindRasterizerState(rasterizerState);
	cmdbuf.bindColorState(colorState);
	cmdbuf.bindColorWriteState(colorWriteState);

	render_cmdlist = cmdbuf.finishList();
}

void renderGame(NVGcontext* vg, BrickGameFramework& game, float mx, float my, float width, float height, float t)
{
	nvgSave(vg);

	float angle = 0;
	float scale = 1;

	switch (game.screen_orientation)
	{
	case orientation_normal:
		angle = 0;
		scale = 1;
		break;
	case orientation_right_down:
		angle = nvgDegToRad(90);
		scale = 1.5;
		break;
	case orientation_upside_down:
		angle = nvgDegToRad(180);
		scale = 1;
		break;
	case orientation_left_down:
		angle = nvgDegToRad(270);
		scale = 1.5;
		break;
	}

	nvgTranslate(vg, 1280 / 2, 720 / 2);
	nvgScale(vg, scale, scale);
	nvgRotate(vg, angle);

	int cell_width = 31;
	int cell_height = 31;

	int draw_grid_width = grid_width(game.game_grid);
	int draw_grid_height = grid_height(game.game_grid);

	int grid_offset_x = (-(draw_grid_width * cell_width)) / 2.;
	int grid_offset_y = (-(draw_grid_height * cell_height)) / 2.;

	int border_size = 5;

	nvgBeginPath(vg);
	nvgRoundedRect(vg, grid_offset_x - border_size, grid_offset_y - border_size, cell_width * draw_grid_width + border_size * 2, cell_height * draw_grid_height + border_size * 2, border_size);
	nvgFillColor(vg, nvgRGBA(0, 0, 0, 255));
	nvgFill(vg);

	nvgBeginPath(vg);
	nvgRect(vg, grid_offset_x, grid_offset_y, cell_width * draw_grid_width, cell_height * draw_grid_height);
	nvgFillColor(vg, nvgRGBA(109, 120, 92, 255));
	nvgFill(vg);

	for (int i = 0; i < draw_grid_width; i += 2)
	{
		for (int j = 0; j < draw_grid_height; j += 2)
		{
			float x = grid_offset_x + (i)*cell_width;
			float y = grid_offset_y + (j)*cell_height;

			bool ul = grid_get(game.game_grid, i, j);
			bool ur = grid_get(game.game_grid, i + 1, j);
			bool bl = grid_get(game.game_grid, i, j + 1);
			bool br = grid_get(game.game_grid, i + 1, j + 1);

			unsigned int pos = 1 * ul + 2 * ur + 4 * bl + 8 * br;

			std::string num = std::to_string(pos);
			if (pos < 10)
				num = "0" + num;

			if (i + 2 <= draw_grid_width && j + 2 <= draw_grid_height)
			{
				draw_sprite(vg, x, y, cell_width * 2, cell_height * 2, "spr_cells_" + num);
			}
			else
			{
				if (i < draw_grid_width && j < draw_grid_height)
				{
					if (grid_get(game.game_grid, i, j))
						draw_sprite(vg, x, y, cell_width, cell_height, "spr_cell_selected"); // 11% opacity
					else
						draw_sprite(vg, x, y, cell_width, cell_height, "spr_cell_unselected");
				}

				if (i + 1 < draw_grid_width && j < draw_grid_height)
				{
					if (grid_get(game.game_grid, i + 1, j))
						draw_sprite(vg, x + cell_width, y, cell_width, cell_height, "spr_cell_selected"); // 11% opacity
					else
						draw_sprite(vg, x + cell_width, y, cell_width, cell_height, "spr_cell_unselected");
				}

				if (i < draw_grid_width && j + 1 < draw_grid_height)
				{
					if (grid_get(game.game_grid, i, j + 1))
						draw_sprite(vg, x, y + cell_height, cell_width, cell_height, "spr_cell_selected"); // 11% opacity
					else
						draw_sprite(vg, x, y + cell_height, cell_width, cell_height, "spr_cell_unselected");
				}
			}

			//if (grid_get(game.game_grid, i, j))
			//	draw_sprite(vg, x, y, cell_width, cell_height, "spr_cell_selected"); // 11% opacity
			//else
			//	draw_sprite(vg, x, y, cell_width, cell_height, "spr_cell_unselected");
		}
	}

	nvgRestore(vg);
}

void draw_digital_display(NVGcontext* vg, std::string display_string, int x, int y, std::string title, unsigned int length = 8)
{
	nvgSave(vg);

	nvgTranslate(vg, x, y);

	nvgFontFace(vg, "kongtext");
	nvgFontSize(vg, 24);
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
	nvgFillColor(vg, nvgRGBA(0, 0, 0, 255));
	nvgText(vg, 3, 0, title.c_str(), NULL);

	nvgFontFace(vg, "seg");
	nvgFontSize(vg, 40);
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

	std::string temp_score = display_string;

	while (temp_score.size() < length)
		temp_score = " " + temp_score;

	for (unsigned int i = 0; i < length; i++)
	{
		nvgFillColor(vg, nvgRGBA(97, 112, 91, 255));
		nvgText(vg, (i * 30), 28, "8", NULL);
		nvgFillColor(vg, nvgRGBA(0, 0, 0, 255));
		std::string charat(1, temp_score.at(i));
		nvgText(vg, (i * 30), 28, charat.c_str(), NULL);
	}

	nvgRestore(vg);
}

void BrickGameFramework::render(u64 ns)
{
	float time = ns / 1000000000.0;
	float dt = time - prevTime;
	prevTime = time;

	// Acquire a framebuffer from the swapchain (and wait for it to be available)
	int slot = queue.acquireImage(swapchain);

	// Run the command list that attaches said framebuffer to the queue
	queue.submitCommands(framebuffer_cmdlists[slot]);

	// Run the main rendering command list
	queue.submitCommands(render_cmdlist);

	updateGraph(&fps, dt);

	nvgBeginFrame(vg, FramebufferWidth, FramebufferHeight, 1.0f);
	{
		renderGame(vg, *this, 0, 0, FramebufferWidth, FramebufferHeight, time);

		if (show_ui)
		{
			renderGraph(vg, 5, 5, &fps);

			const float size = 25.f;
			nvgFontFace(vg, "minecraft");
			nvgFontSize(vg, size);
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
			nvgFillColor(vg, nvgRGBA(0, 0, 0, 255));
			nvgText(vg, 20, 70, "Welcome to this", NULL);
			nvgText(vg, 20, 70 + size * 1, "extremely early", NULL);
			nvgText(vg, 20, 70 + size * 2, "version!", NULL);
			nvgText(vg, 20, 70 + size * 4, "Minus: Rotate", NULL);
			nvgText(vg, 20, 70 + size * 5, "Y : Menu", NULL);
			nvgText(vg, 20, 70 + size * 6, "X : Snake", NULL);
			nvgText(vg, 20, 70 + size * 8, "A : Wide Screen", NULL);
			nvgText(vg, 20, 70 + size * 9, "B : Classic Screen", NULL);
			nvgText(vg, 20, 70 + size * 11, "L : Toggle This Text", NULL);

			draw_digital_display(vg, highscore_display, 865, 165, "High Score");
			draw_digital_display(vg, score_display, 865, 70, "Score");
		}
	}
	nvgEndFrame(vg);

	// Now that we are done rendering, present it to the screen
	queue.presentImage(swapchain, slot);
}

void transition(std::vector<std::vector<bool>>& grid, double percent)
{
	//printf("%f percent\n", percent);
	if (percent > 0 && percent <= 100)
	{
		for (int y = 0; y < (double)grid_height(grid) * ((double)percent / 100.); y++)
			for (int x = 0; x < grid_width(grid); x++)
			{
				grid_set(grid, x, y, true);
			}
	}
	else if (percent > 100 && percent <= 200)
	{
		for (int y = (double)grid_height(grid) * ((double)(percent - 100) / 100.); y < grid_height(grid); y++)
			for (int x = 0; x < grid_width(grid); x++)
			{
				grid_set(grid, x, y, true);
			}
	}
}

bool BrickGameFramework::onFrame(u64 ns)
{
	padUpdate(&pad);
	u64 keyboard_check_pressed = padGetButtonsDown(&pad);

	if (keyboard_check_pressed & HidNpadButton_Plus)
	{
		return false;
	}

	if (keyboard_check_pressed & HidNpadButton_L)
	{
		show_ui = !show_ui;
	}

	if (keyboard_check_pressed & HidNpadButton_Y)
	{
		next_game = 0;
	}

	if (keyboard_check_pressed & HidNpadButton_X)
	{
		next_game = 1;
	}

	if (keyboard_check_pressed & HidNpadButton_B)
	{
		target_grid_width = 10;
		target_grid_height = 20;
	}

	if (keyboard_check_pressed & HidNpadButton_A)
	{
		target_grid_width = 20;
		target_grid_height = 20;
	}

	if (grid_width(game_grid) < target_grid_width)
	{
		game_grid = grid_create(grid_width(game_grid) + 1, grid_height(game_grid));
	}
	else if (grid_width(game_grid) > target_grid_width)
	{
		game_grid = grid_create(grid_width(game_grid) - 1, grid_height(game_grid));
	}

	if (grid_height(game_grid) < target_grid_height)
	{
		game_grid = grid_create(grid_width(game_grid), grid_height(game_grid) + 1);
	}
	else if (grid_height(game_grid) > target_grid_height)
	{
		game_grid = grid_create(grid_width(game_grid), grid_height(game_grid) - 1);
	}

	grid_clear(game_grid);

	if ((next_game != -1 && next_game != current_game) && transition_stage == -1)
	{
		transition_stage = 0;
		transition_percent = 0;
	}

	if (transition_stage == 0)
	{
		if (transition_percent < 100)
		{
			transition_percent += 1.5;
		}
		else
		{
			transition_stage = 1;
			if (current_game != -1)
			{
				objects.clear();
				game_list.at(current_game)->subgame_exit();
			}

			current_game = next_game;
			next_game = -1;

			game_list.at(current_game)->subgame_init();
		}

		transition(game_grid, transition_percent);
	}
	else if (transition_stage == 1)
	{
		if (transition_percent < 200)
		{
			transition_percent += 1.5;
		}
		else
		{
			transition_percent = 0;
			transition_stage = -1;
		}

		transition(game_grid, transition_percent);
	}

	if (current_game != -1)
	{
		if (transition_stage == -1)
		{
			for (unsigned int i = 0; i < objects.size(); i++)
				objects.at(i)->step_function();

			game_list.at(current_game)->subgame_run();
		}

		for (unsigned int i = 0; i < objects.size(); i++)
			objects.at(i)->draw_function();

		game_list.at(current_game)->subgame_draw();
	}

	if (keyboard_check_pressed & HidNpadButton_Minus)
	{
		screen_orientation += 1;
		screen_orientation = screen_orientation % 4;
		printf("orientation: %i\n", screen_orientation);
	}

	render(ns);
	return true;
}

void BrickGameFramework::setScore(int score)
{
	int hs = highscore;

	if (score > highscore)
	{
		hs = score;
		highscore = score;
	}

	setScore(std::to_string(score));
	setHighScore(std::to_string(hs));
}

void BrickGameFramework::setScore(double score)
{
	if (score > highscore)
		highscore = score;

	setScore(std::to_string(score));
	setHighScore(std::to_string(highscore));
}

void BrickGameFramework::setScore(std::string score)
{
	score_display = score;
}

void BrickGameFramework::setHighScore(std::string score)
{
	highscore_display = score;
}

int main(int argc, char* argv[])
{
	init_audio();
	BrickGameFramework app;
	app.run();
	exit_audio();
	return 0;
}