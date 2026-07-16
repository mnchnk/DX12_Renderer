#pragma once
#include <memory>
#include <unordered_map>
#include <string>
#include <d3d12.h>
#include "GraphicsDevice.h"
#include "CommandQueue.h"
#include "SwapChain.h"
#include "FrameResource.h"
#include "Camera.h"
#include "GameTimer.h"
#include "TextureManager.h"
#include "ShadowMap.h"
#include <array>
#include <DirectXCollision.h>

enum class RenderItemType
{
	Opaque = 0

};

struct RenderItem
{
	RenderItem() = default;
	RenderItem(const RenderItem& rhs) = delete;

	std::string Name;

	DirectX::XMFLOAT4X4 World;
	DirectX::XMFLOAT4X4 TexTransform;

	UINT ObjectCBIndex = -1;
	UINT8 NumFramesDirty = MaxFrameResource;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;

	DirectX::BoundingBox Bounds;
};

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

	//RenderItem
	std::unordered_map<RenderItemType, std::vector<RenderItem*>> mRenderItemsByType;
	std::vector<std::unique_ptr<RenderItem>> mAllRenderItems;
	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unique_ptr<TextureManager> mTextureManger;
	
	//CB
	PassConstants mPassCB;

	//Camera
	Camera mMainCamera;
	
	//Light, Shadow
	std::unique_ptr<ShadowMap> mShadowMap = nullptr;
	std::unordered_map<std::string, std::vector<std::unique_ptr<Light>>> mAllLights;
	Light* mMainLight = nullptr;

	ComPtr<ID3D12DescriptorHeap> mShadowSrvHeap;
	ComPtr<ID3D12DescriptorHeap> mShadowDsvHeap;

public:
	Renderer(HWND hWnd, UINT clientWidth, UINT clientHeight) :mHWnd(hWnd), mClientWidth(clientWidth), mClientHeight(clientHeight) {}
	~Renderer() = default;

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
	void UpdateObjectConstants();
	void UpdatePassConstants();
	void UpdateMaterialBuffer();

	//Draw
	void Draw();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
	
	//Picking
	void Pick(int sx, int sy);

	//Run
	int Run();
};

