#pragma once
#include <vector>
#include <memory>
#include <unordered_map>
#include "GameObject.h"
#include "FrameResource.h"
#include "RenderItem.h"

class Scene
{
public:
	Scene() = default;
	~Scene() = default;
	
	GameObject* CreateGameObject(const std::string& name)
	{
		mGameObjects.push_back(std::make_unique<GameObject>(name));
		return mGameObjects.back().get();
	}

	RenderItem* CreateRenderItem(std::unique_ptr<RenderItem>& rItem)
	{
		mAllRenderItems.push_back(std::move(rItem));
		return mAllRenderItems.back().get();
	}

	Light* CreateLight(const std::string& name, std::unique_ptr<Light>& light)
	{
		mAllLights[name].push_back(std::move(light));
		return mAllLights[name].back().get();
	}

	const std::vector<std::unique_ptr<GameObject>>& GetGameObjects() const { return mGameObjects; }
	const std::vector<std::unique_ptr<RenderItem>>& GetAllRenderItems() const { return mAllRenderItems; }
	const std::unordered_map<std::string, std::vector<std::unique_ptr<Light>>>& GetAllLights() const { return mAllLights; }

private:
	std::vector<std::unique_ptr<GameObject>> mGameObjects;
	std::vector<std::unique_ptr<RenderItem>> mAllRenderItems;
	std::unordered_map<std::string, std::vector<std::unique_ptr<Light>>> mAllLights;

};