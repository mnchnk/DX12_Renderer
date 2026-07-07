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

	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(
		device, cmdList, tex->Filename.c_str(), tex->Resource, tex->UploadHeap));

	mTextures[name] = std::move(tex);
}

inline Texture* TextureManager::GetTexture(const std::string& name)
{
	if (mTextures.find(name) != mTextures.end())
		return mTextures[name].get();

	return nullptr;
}
