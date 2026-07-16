#include "TextureManager.h"
#include "Util.h"

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

void TextureManager::InitializeDescriptor(ID3D12Device* device)
{
	if (mTextures.empty())
		return;
	
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = mTextureCount;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mTextureHeap)));

	UINT srvDescSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	D3D12_CPU_DESCRIPTOR_HANDLE hCpuStart = mTextureHeap->GetCPUDescriptorHandleForHeapStart();

	for (auto& kv : mTextures)
	{
		Texture* tex = kv.second.get();

		D3D12_RESOURCE_DESC texDesc = tex->Resource->GetDesc(); // DDS 로더가 이미 알아낸 실제 포맷/밉레벨을 그대로 사용

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = texDesc.Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = texDesc.MipLevels;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(hCpuStart, tex->SrvHeapIndex, srvDescSize);
		device->CreateShaderResourceView(tex->Resource.Get(), &srvDesc, hDescriptor);
	}
}

inline Texture* TextureManager::GetTexture(const std::string& name)
{
	auto it = mTextures.find(name);
	if (it != mTextures.end())
		return it->second.get();

	return nullptr;
}
