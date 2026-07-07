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

	inline Texture* GetTexture(const std::string& name);
private:
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
};

