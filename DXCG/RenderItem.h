#pragma once
#include <string>
#include <d3d12.h>
#include <DirectXCollision.h>
#include "FrameResource.h" // Material
#include "Util.h" 

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

