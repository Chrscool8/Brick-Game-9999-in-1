#pragma once
#ifndef SUBGAME_H
#define SUBGAME_H

#include <string>

class BrickGameFramework;

class subgame
{
public:
	BrickGameFramework& parent;

	std::string name;
	virtual void subgame_init();
	virtual void subgame_step();
	virtual void subgame_draw();
	virtual void subgame_exit();
	virtual void subgame_demo();

	subgame(BrickGameFramework& game_);

};
#endif // SUBGAME