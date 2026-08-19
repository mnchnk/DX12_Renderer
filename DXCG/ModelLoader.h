#pragma once
#include <string>
#include <vector>
#include <memory>
#include <d3d12.h>
#include "Util.h"          // MeshGeometry, SubmeshGeometry, CreateDefaultBuffer
#include "FrameResource.h" // Vertex

// One aiMesh. Pairs with a key in MeshGeometry::DrawArgs.
struct LoadedSubmesh
{
    std::string Name;           // key used in DrawArgs
    UINT MaterialIndex = 0;     // index into LoadedModel::Materials
};

// One aiMaterial. Not a GPU resource yet - just a description of what to build.
struct LoadedMaterial
{
    std::string Name;
    DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 FresnelR0 = { 0.04f, 0.04f, 0.04f };
    float Roughness = 0.5f;

    // Texture file names referenced by the model file.
    // Empty string means that slot has no texture.
    std::string DiffuseTextureFile;
    std::string NormalTextureFile;
};

// Result of loading one model file.
// All meshes are merged into a single vertex/index buffer; each original
// mesh becomes a submesh inside MeshGeometry::DrawArgs.
struct LoadedModel
{
    std::unique_ptr<MeshGeometry> Geometry;
    std::vector<LoadedSubmesh> Submeshes;
    std::vector<LoadedMaterial> Materials;
};

class ModelLoader
{
public:
    // Loads FBX / OBJ / glTF and anything else Assimp supports.
    // Returns false on failure and fills outError with Assimp's message.
    static bool Load(
        const std::string& filename,
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        LoadedModel& outModel,
        std::string& outError);
};
