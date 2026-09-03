#pragma once
#include <string>
#include <d3d12.h>
#include <DirectXCollision.h>
#include "Graphics/Material.h"        // Material
#include "Graphics/GraphicsCommon.h" // MaxFrameResource
#include "Graphics/Util.h"           // MeshGeometry

// PSO가 다르면 타입을 나눈다. Renderer가 타입별로 PSO를 걸고 그린다.
enum class RenderItemType
{
	Opaque = 0,
	SkinnedOpaque
};

struct RenderItem
{
	RenderItem() = default;
	RenderItem(const RenderItem& rhs) = delete;

	std::string Name;

	DirectX::XMFLOAT4X4 World;
	DirectX::XMFLOAT4X4 TexTransform;

	UINT ObjectCBIndex = -1;

	// 본 팔레트 상수 버퍼의 슬롯. -1이면 스키닝하지 않는 정적 메시.
	// 같은 캐릭터의 서브메시들은 스켈레톤을 공유하므로 같은 값을 가진다.
	int SkinnedCBIndex = -1;

	UINT8 NumFramesDirty = MaxFrameResource;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;

	DirectX::BoundingBox Bounds;
};

