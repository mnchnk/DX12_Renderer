#pragma once
#include <unordered_map>
#include <string>
#include <memory>
#include "DDSTextureLoader.h"

using Microsoft::WRL::ComPtr;

struct Texture
{
	std::string Name;

	std::wstring Filename;

	Microsoft::WRL::ComPtr<ID3D12Resource> Resource = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> UploadHeap = nullptr;

	int SrvHeapIndex = -1;
};

class TextureManager
{
public:
	TextureManager() = default;
	~TextureManager() = default;
	
	void LoadTexture(
		const std::string& name,
		const std::string& filename,
		ID3D12Device* device,
		ID3D12GraphicsCommandList* cmdList);

	void InitializeDescriptor(ID3D12Device* device, CD3DX12_CPU_DESCRIPTOR_HANDLE& hCpu, UINT srvDescSize);

	Texture* GetTexture(const std::string& name);
	const std::unordered_map<std::string, std::unique_ptr<Texture>>& GetAllTextures() const { return mTextures; }
	int GetTextureCount() const { return mTextureCount; }
	ComPtr<ID3D12DescriptorHeap> GetTextureHeap() const { return mTextureHeap; }
private:
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;

	int mTextureCount = 0;
	ComPtr<ID3D12DescriptorHeap> mTextureHeap;
	
};

