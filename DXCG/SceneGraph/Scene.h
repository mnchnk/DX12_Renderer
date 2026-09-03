#pragma once
#include <vector>
#include <memory>
#include <unordered_map>
#include "SceneGraph/GameObject.h"
#include "Graphics/Light.h"
#include "Graphics/RenderItem.h"
#include "Animation/AnimationPlayer.h"

// 참조로만 받으므로 전방 선언으로 충분하다.
// 헤더에 ResourceManager.h를 포함하면 Scene을 쓰는 모든 곳이
// TextureManager/ModelLoader까지 끌고 오게 된다.
class ResourceManager;

// 씬에 무엇이 존재하는가를 소유한다.
// GPU 리소스는 ResourceManager가, 그리기는 Renderer가 담당한다.
class Scene
{
public:
	Scene() = default;
	~Scene() = default;

	// ResourceManager가 만들어둔 지오메트리/머티리얼을 참조해 씬을 구성한다.
	void Build(ResourceManager& resources);

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

	// 그림자맵을 만드는 기준이 되는 방향광. Renderer가 LightViewProj 계산에 쓴다.
	Light* GetMainLight() const { return mMainLight; }

	// 애니메이션은 "지금 씬이 어떤 상태인가"이므로 Scene이 소유한다.
	// Renderer는 결과 팔레트를 읽어 상수 버퍼로 올리기만 한다.
	AnimationPlayer& GetAnimation() { return mAnimation; }
	const AnimationPlayer& GetAnimation() const { return mAnimation; }

	// 본 팔레트가 필요한 캐릭터 수. FrameResource의 SkinnedCB 크기가 된다.
	UINT GetSkinnedCount() const { return mSkinnedCount; }

private:
	void BuildLights();
	void BuildRenderItems(ResourceManager& resources);

	std::vector<std::unique_ptr<GameObject>> mGameObjects;
	std::vector<std::unique_ptr<RenderItem>> mAllRenderItems;
	std::unordered_map<std::string, std::vector<std::unique_ptr<Light>>> mAllLights;

	Light* mMainLight = nullptr;   // mAllLights가 소유하고, 여기선 참조만 한다

	AnimationPlayer mAnimation;
	UINT mSkinnedCount = 0;
};
