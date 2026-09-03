#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <d3d12.h>

#include "Graphics/Util.h"            // MeshGeometry
#include "Graphics/Material.h"
#include "Graphics/TextureManager.h"
#include "Asset/ModelLoader.h"        // LoadedModel

// GPU 리소스(지오메트리, 텍스처, 머티리얼)를 소유한다.
// "무엇이 존재하는가"는 Scene이, "어떻게 그리는가"는 Renderer가 맡고,
// 여기는 "그리는 데 필요한 재료"를 담당한다.
//
// 디스크립터 힙은 여기서 만들지 않는다. 그림자맵/ImGui 슬롯과 한 힙을 공유해야 해서
// Renderer가 소유하고, TextureManager는 그 힙의 시작 핸들만 받아 SRV를 채운다.
class ResourceManager
{
public:
    ResourceManager() = default;
    ~ResourceManager() = default;
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    // 순서 의존성이 있어서 한 함수로 묶었다.
    // 모델 로드 -> 텍스처 이름 확보 -> 텍스처 로드 -> 머티리얼(SrvHeapIndex) 구성
    bool Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);

    MeshGeometry* GetGeometry(const std::string& name) const;
    Material* GetMaterial(const std::string& name) const;

    TextureManager* GetTextureManager() const { return mTextureManager.get(); }
    const LoadedModel& GetCharacterModel() const { return mCharacterModel; }

    // 캐릭터 지오메트리를 이름 대신 이걸로 가져온다.
    // 모델 파일을 바꿔도 호출부를 안 고쳐도 되고, 못 찾으면 nullptr이다.
    MeshGeometry* GetCharacterGeometry() const { return GetGeometry(mCharacterGeoName); }

    // Renderer가 MaterialBuffer를 채울 때 순회한다.
    const std::unordered_map<std::string, std::unique_ptr<Material>>& GetAllMaterials() const
    {
        return mMaterials;
    }

    UINT GetMaterialCount() const { return (UINT)mMaterials.size(); }

private:
    void LoadModels(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
    void BuildGroundGeometry(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
    void LoadTextures(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
    void BuildMaterials();

    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
    std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
    std::unique_ptr<TextureManager> mTextureManager;

    LoadedModel mCharacterModel;
    std::string mCharacterGeoName;   // mGeometries에서의 키. 모델 로드 시 정해진다
};
