#pragma once
#ifndef CONTROLS_H
#define CONTROLS_H

#include <game.h>

bool keyboard_check_right(BrickGameFramework& game);
bool keyboard_check_up(BrickGameFramework& game);
bool keyboard_check_left(BrickGameFramework& game);
bool keyboard_check_down(BrickGameFramework& game);

#endif // !CONTROLS_H