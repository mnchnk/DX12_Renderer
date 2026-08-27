#include "Graphics/ResourceManager.h"
#include "Graphics/Vertex.h"
#include "Graphics/d3dx12.h"

#include <array>
#include <Windows.h>   // OutputDebugStringA

using namespace DirectX;

bool ResourceManager::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    mTextureManager = std::make_unique<TextureManager>();

    // 순서가 중요하다.
    // 모델을 읽어야 어떤 텍스처가 필요한지 알 수 있고,
    // 텍스처를 로드해야 머티리얼이 SrvHeapIndex를 채울 수 있다.
    LoadModels(device, cmdList);
    BuildGroundGeometry(device, cmdList);
    LoadTextures(device, cmdList);
    BuildMaterials();

    return true;
}

MeshGeometry* ResourceManager::GetGeometry(const std::string& name) const
{
    auto it = mGeometries.find(name);
    return (it != mGeometries.end()) ? it->second.get() : nullptr;
}

Material* ResourceManager::GetMaterial(const std::string& name) const
{
    auto it = mMaterials.find(name);
    return (it != mMaterials.end()) ? it->second.get() : nullptr;
}

void ResourceManager::LoadModels(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    LoadedModel model;
    std::string err;

    if (ModelLoader::Load("Models/Ely By K.Atienza.fbx", device, cmdList, model, err))
    {
        OutputDebugStringA(("[ModelLoader] submeshes=" + std::to_string(model.Submeshes.size()) +
            " materials=" + std::to_string(model.Materials.size()) + "\n").c_str());

        mGeometries[model.Geometry->Name] = std::move(model.Geometry);
        mCharacterModel = std::move(model);   // Submeshes/Materials 정보는 남는다
    }
    else
    {
        OutputDebugStringA(("[ModelLoader] failed: " + err + "\n").c_str());
    }
}

void ResourceManager::BuildGroundGeometry(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    std::array<Vertex, 4> groundVertices =
    {
        Vertex({ XMFLOAT3(-10.0f, -1.0f, -10.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 5.0f) }),
        Vertex({ XMFLOAT3(-10.0f, -1.0f, +10.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) }),
        Vertex({ XMFLOAT3(+10.0f, -1.0f, +10.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(5.0f, 0.0f) }),
        Vertex({ XMFLOAT3(+10.0f, -1.0f, -10.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(5.0f, 5.0f) }),
    };

    std::array<std::uint16_t, 6> groundIndices = { 0, 1, 2,  0, 2, 3 };

    const UINT vbByteSize = (UINT)groundVertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)groundIndices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "groundGeo";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), groundVertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), groundIndices.data(), ibByteSize);

    geo->VertexBufferGPU = CreateDefaultBuffer(device, cmdList,
        groundVertices.data(), vbByteSize, geo->VertexBufferUploader);

    geo->IndexBufferGPU = CreateDefaultBuffer(device, cmdList,
        groundIndices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;   // 정점이 4개뿐이라 16비트로 충분
    geo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)groundIndices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;

    geo->DrawArgs["grid"] = submesh;

    mGeometries[geo->Name] = std::move(geo);
}

void ResourceManager::LoadTextures(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    // AssetImporter가 확장자 없는 이름("ElyByKAtienza_Diffuse")을 넘겨주므로
    // 여기서 .dds를 붙인다. 원본 png는 임포트 단계에서만 쓰인다.
    auto toDds = [](std::string f)
    {
        size_t dot = f.find_last_of('.');
        return (dot == std::string::npos ? f : f.substr(0, dot)) + ".dds";
    };

    for (const LoadedMaterial& mat : mCharacterModel.Materials)
    {
        if (!mat.DiffuseTextureFile.empty())
            mTextureManager->LoadTexture(mat.DiffuseTextureFile,
                "Models/" + toDds(mat.DiffuseTextureFile), device, cmdList);

        if (!mat.NormalTextureFile.empty())
            mTextureManager->LoadTexture(mat.NormalTextureFile,
                "Models/" + toDds(mat.NormalTextureFile), device, cmdList);
    }
}

void ResourceManager::BuildMaterials()
{
    auto plastic = std::make_unique<Material>();
    plastic->Name = "plastic";
    plastic->MatCBIndex = 0;
    plastic->DiffuseAlbedo = XMFLOAT4(0.0f, 0.2f, 0.6f, 1.0f);
    plastic->FresnelR0 = XMFLOAT3(0.04f, 0.04f, 0.04f);
    plastic->Roughness = 0.2f;

    auto wood = std::make_unique<Material>();
    wood->Name = "wood";
    wood->MatCBIndex = 1;
    wood->DiffuseAlbedo = XMFLOAT4(0.4f, 0.2f, 0.0f, 1.0f);
    wood->FresnelR0 = XMFLOAT3(0.04f, 0.04f, 0.04f);
    wood->Roughness = 0.8f;

    auto iron = std::make_unique<Material>();
    iron->Name = "iron";
    iron->MatCBIndex = 2;
    iron->DiffuseAlbedo = XMFLOAT4(0.1f, 0.1f, 0.1f, 1.0f);
    iron->FresnelR0 = XMFLOAT3(0.56f, 0.57f, 0.58f);
    iron->Roughness = 0.4f;

    auto copper = std::make_unique<Material>();
    copper->Name = "copper";
    copper->MatCBIndex = 3;
    copper->DiffuseAlbedo = XMFLOAT4(0.05f, 0.05f, 0.05f, 1.0f);
    copper->FresnelR0 = XMFLOAT3(0.95f, 0.64f, 0.54f);
    copper->Roughness = 0.2f;

    auto gold = std::make_unique<Material>();
    gold->Name = "gold";
    gold->MatCBIndex = 4;
    gold->DiffuseAlbedo = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
    gold->FresnelR0 = XMFLOAT3(1.00f, 0.71f, 0.29f);
    gold->Roughness = 0.1f;

    mMaterials[plastic->Name] = std::move(plastic);
    mMaterials[wood->Name] = std::move(wood);
    mMaterials[iron->Name] = std::move(iron);
    mMaterials[copper->Name] = std::move(copper);
    mMaterials[gold->Name] = std::move(gold);

    // 반드시 위의 하드코딩 머티리얼이 들어간 뒤에 실행해야 한다.
    // MatCBIndex를 mMaterials.size()로 매기기 때문에, 먼저 돌리면 0번을 다시 발급해
    // plastic과 같은 슬롯을 쓰게 되고 둘이 서로를 덮어쓴다.
    for (size_t i = 0; i < mCharacterModel.Materials.size(); ++i)
    {
        const LoadedMaterial& src = mCharacterModel.Materials[i];

        auto mat = std::make_unique<Material>();
        mat->Name = src.Name;
        mat->MatCBIndex = (int)mMaterials.size();
        mat->DiffuseAlbedo = src.DiffuseAlbedo;
        mat->FresnelR0 = src.FresnelR0;
        mat->Roughness = src.Roughness;

        Texture* diffuse = mTextureManager->GetTexture(src.DiffuseTextureFile);
        mat->DiffuseSrvHeapIndex = diffuse ? diffuse->SrvHeapIndex : -1;

        Texture* normal = mTextureManager->GetTexture(src.NormalTextureFile);
        mat->NormalSrvHeapIndex = normal ? normal->SrvHeapIndex : -1;

        mMaterials[mat->Name] = std::move(mat);
    }
}
