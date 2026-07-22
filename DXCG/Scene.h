#pragma once
#include <vector>
#include <memory>
#include "GameObject.h"

class Scene
{
public:
	GameObject* CreateGameObject(const std::string& name)
	{
		mGameObjects.push_back(std::make_unique<GameObject>(name));
		return mGameObjects.back().get();
	}

	const std::vector<std::unique_ptr<GameObject>>& GetGameObjects() const { return mGameObjects; }
private:
	std::vector<std::unique_ptr<GameObject>> mGameObjects;
};