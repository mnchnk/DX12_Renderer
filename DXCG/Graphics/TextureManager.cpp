#include "Graphics/TextureManager.h"
#include "Graphics/Util.h"

void TextureManager::LoadTexture(const std::string& name, const std::string& filename, ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
	if (mTextures.find(name) != mTextures.end())
		return;

	auto tex = std::make_unique<Texture>();
	tex->Name = name;
	std::wstring tempFilename(filename.begin(), filename.end());
	tex->Filename = tempFilename;
	tex->SrvHeapIndex = mTextureCount;
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(
		device, cmdList, tex->Filename.c_str(), tex->Resource, tex->UploadHeap));
	
	mTextures[name] = std::move(tex);
	mTextureCount++;
}

void TextureManager::InitializeDescriptor(ID3D12Device* device, CD3DX12_CPU_DESCRIPTOR_HANDLE& hCpu, UINT srvDescSize)
{
	for (auto& kv : mTextures)
	{
		Texture* tex = kv.second.get();

		// Reuse the format and mip count the DDS loader already figured out.
		D3D12_RESOURCE_DESC texDesc = tex->Resource->GetDesc();

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = texDesc.Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = texDesc.MipLevels;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		// Absolute position from the caller's base - never rely on map order.
		CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(hCpu, tex->SrvHeapIndex, srvDescSize);
		device->CreateShaderResourceView(tex->Resource.Get(), &srvDesc, hDescriptor);
	}
}

Texture* TextureManager::GetTexture(const std::string& name)
{
	auto it = mTextures.find(name);
	if (it != mTextures.end())
		return it->second.get();

	return nullptr;
}
