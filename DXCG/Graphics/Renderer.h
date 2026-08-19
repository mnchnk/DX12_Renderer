#pragma once
#include <memory>
#include <unordered_map>
#include <string>
#include <d3d12.h>
#include "Graphics/GraphicsDevice.h"
#include "Graphics/CommandQueue.h"
#include "Graphics/SwapChain.h"
#include "Graphics/FrameResource.h"
#include "SceneGraph/Camera.h"
#include "Core/GameTimer.h"
#include "Graphics/TextureManager.h"
#include "Graphics/ShadowMap.h"
#include "SceneGraph/Scene.h"
#include "Asset/ModelLoader.h"
#include <array>
#include <DirectXCollision.h>

class Renderer
{
private:
	//Graphics Basic 
	std::unique_ptr<GraphicsDevice> mGraphicsDevice;
	std::unique_ptr<CommandQueue> mCommandQueue;
	std::unique_ptr<SwapChain> mSwapChain;

	ComPtr<ID3D12RootSignature> mRootSignature;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;	
	std::unordered_map<std::string, std::vector<D3D12_INPUT_ELEMENT_DESC>> mInputLayouts;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;
	
	D3D12_VIEWPORT mScreenViewport;
	D3D12_RECT mScissorRect;

	UINT mClientWidth = 800;
	UINT mClientHeight = 600;

	HWND mHWnd;

	GameTimer mTimer;

	UINT mSrvDescSize = -1;

	//FrameResource
	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	UINT8 mCurrFrameResourceIndex;
	FrameResource* mCurrFrameResource = nullptr;

	//RenderItem Cache
	std::unordered_map<RenderItemType, std::vector<RenderItem*>> mRenderItemsByType;
	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unique_ptr<TextureManager> mTextureManger;
	LoadedModel mCharacterModel;

	//CB
	PassConstants mPassCB;

	//Camera
	Camera mMainCamera;
	
	//Light, Shadow
	std::unique_ptr<ShadowMap> mShadowMap = nullptr;
	Light* mMainLight = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvHeap;
	ComPtr<ID3D12DescriptorHeap> mShadowDsvHeap;

	UINT mShadowSrvIndex = -1;

	//ImGui
	// 1.92부터 폰트 아틀라스가 동적이라 디스크립터를 여러 개 요구한다.
	// mSrvHeap 뒤쪽에 이만큼을 예약해두고 free list로 나눠준다.
	static const UINT ImGuiSrvCount = 8;
	UINT mImGuiSrvStart = 0;
	std::vector<UINT> mImGuiFreeSlots;   // 아직 안 쓴 슬롯 인덱스

	//Scene
	std::unique_ptr<Scene> mScene;

	// 디버그 UI에서 조작하는 값들
	float mShadowDepthBias = 100000.0f;
	DirectX::XMFLOAT3 mLightDirection = { 0.57735f, -0.57735f, 0.57735f };
public:
	Renderer(HWND hWnd, UINT clientWidth, UINT clientHeight) :mHWnd(hWnd), mClientWidth(clientWidth), mClientHeight(clientHeight) {}
	~Renderer();

	//Initialize
	bool Initialize();
	bool InitializeFrameResource();
	bool InitializeRootSignature();
	bool InitializeDescriptorHeaps();
	bool InitializeShadersAndInputLayout();
	bool InitializePSOs();

	void InitializeShapesGeometry();
	void InitializeMaterials();
	void InitializeRenderItem();
	void InitializeLights();
	void LoadTextures();
	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();

	//Update
	void Update(float dt);
	void SyncTransforms();
	void SyncLights();
	void UpdateObjectConstants();
	void UpdatePassConstants();
	void UpdateMaterialBuffer();

	//ImGui
	bool InitializeImGui();
	void BuildDebugUI();   // 매 프레임 UI를 다시 선언한다 (immediate mode)

	// ImGui가 SRV 슬롯을 요청할 때 쓰는 콜백. InitInfo::UserData로 this를 넘겨 연결한다.
	void AllocImGuiSrv(D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu);
	void FreeImGuiSrv(D3D12_CPU_DESCRIPTOR_HANDLE cpu);

	//Draw
	void Draw();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
	
	//Picking
	void Pick(int sx, int sy);

	//Run
	int Run();
};

