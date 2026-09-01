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
	HRESULT hr = DirectX::CreateDDSTextureFromFile12(
		device, cmdList, tex->Filename.c_str(), tex->Resource, tex->UploadHeap);

	if (FAILED(hr))
	{
		// 텍스처 하나 없다고 프로그램 전체가 죽을 이유는 없다.
		// 등록하지 않고 넘어가면 머티리얼의 SrvHeapIndex가 -1이 되고,
		// 셰이더 가드가 그 머티리얼을 상수 색상으로 그린다.
		OutputDebugStringA(("[TextureManager] failed to load: " + filename + "\n").c_str());
		return;
	}
	
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
