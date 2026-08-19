#pragma once

#include <string>
#include "SceneGraph/Transform.h"

struct RenderItem;
struct Light;

class GameObject
{
public:
	explicit GameObject(const std::string& name) : mName(name) {}

	Transform& GetTransform() { return mTransform; }
	const std::string& GetName() const { return mName; }

	RenderItem* Render = nullptr;
	Light* LightData = nullptr;

private:
	std::string mName;
	Transform mTransform;
};

