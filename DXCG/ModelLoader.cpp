#include "ModelLoader.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <filesystem>
#include <unordered_set>
#include <algorithm>

using namespace DirectX;

namespace
{
    // Makes sure submesh names do not collide.
    // Models with several meshes sharing one name are common, and DrawArgs is a
    // map - a duplicate key would silently overwrite the previous submesh.
    std::string MakeUniqueName(const std::string& base, UINT index, std::unordered_set<std::string>& used)
    {
        std::string name = base.empty() ? ("mesh" + std::to_string(index)) : base;

        if (used.find(name) == used.end())
        {
            used.insert(name);
            return name;
        }

        std::string unique = name + "_" + std::to_string(index);
        used.insert(unique);
        return unique;
    }

    // Keeps only the file name from a texture path.
    // Model files often embed the absolute path from the artist's machine,
    // which will not exist here.
    std::string ExtractFileName(const aiString& path)
    {
        std::string s = path.C_Str();
        if (s.empty()) return {};

        size_t pos = s.find_last_of("/\\");
        return (pos == std::string::npos) ? s : s.substr(pos + 1);
    }

    std::string GetTextureFile(const aiMaterial* mat, aiTextureType type)
    {
        if (mat->GetTextureCount(type) == 0) return {};

        aiString path;
        if (mat->GetTexture(type, 0, &path) != AI_SUCCESS) return {};

        return ExtractFileName(path);
    }
}

bool ModelLoader::Load(
    const std::string& filename,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    LoadedModel& outModel,
    std::string& outError)
{
    Assimp::Importer importer;

    // aiProcess_ConvertToLeftHanded = MakeLeftHanded | FlipUVs | FlipWindingOrder
    //   D3D uses a left-handed system and texture V grows downward. Without this
    //   the model comes in mirrored and the winding is reversed, so back faces
    //   get culled instead of front faces.
    // aiProcess_CalcTangentSpace fills in the tangents needed for normal mapping.
    const unsigned int flags =
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_JoinIdenticalVertices |
        aiProcess_ConvertToLeftHanded;

    const aiScene* scene = importer.ReadFile(filename, flags);

    if (scene == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || scene->mRootNode == nullptr)
    {
        outError = importer.GetErrorString();
        return false;
    }

    // ---------------------------------------------------------------------
    // 1. Merge every aiMesh into one vertex/index buffer, recording each as a
    //    submesh. Sharing one buffer means we never rebind buffers per draw.
    // ---------------------------------------------------------------------
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<SubmeshGeometry> submeshes;

    outModel.Submeshes.clear();
    outModel.Submeshes.reserve(scene->mNumMeshes);
    submeshes.reserve(scene->mNumMeshes);

    std::unordered_set<std::string> usedNames;

    for (UINT m = 0; m < scene->mNumMeshes; ++m)
    {
        const aiMesh* mesh = scene->mMeshes[m];

        SubmeshGeometry submesh;
        submesh.StartIndexLocation = (UINT)indices.size();
        // Where this mesh's vertices begin inside the merged buffer.
        // Indices stay mesh-local (starting at 0); the GPU adds this at draw time.
        submesh.BaseVertexLocation = (INT)vertices.size();

        XMVECTOR vMin = XMVectorReplicate(FLT_MAX);
        XMVECTOR vMax = XMVectorReplicate(-FLT_MAX);

        for (UINT v = 0; v < mesh->mNumVertices; ++v)
        {
            Vertex vertex = {};

            vertex.Pos = { mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z };

            if (mesh->HasNormals())
                vertex.Normal = { mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z };

            // A mesh can carry several UV channels; we only use channel 0.
            if (mesh->HasTextureCoords(0))
                vertex.TexC = { mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y };

            if (mesh->HasTangentsAndBitangents())
                vertex.Tangent = { mesh->mTangents[v].x, mesh->mTangents[v].y, mesh->mTangents[v].z };

            XMVECTOR p = XMLoadFloat3(&vertex.Pos);
            vMin = XMVectorMin(vMin, p);
            vMax = XMVectorMax(vMax, p);

            vertices.push_back(vertex);
        }

        for (UINT f = 0; f < mesh->mNumFaces; ++f)
        {
            const aiFace& face = mesh->mFaces[f];
            // aiProcess_Triangulate guarantees 3 indices per face.
            for (UINT i = 0; i < face.mNumIndices; ++i)
                indices.push_back(face.mIndices[i]);
        }

        submesh.IndexCount = (UINT)indices.size() - submesh.StartIndexLocation;

        // Bounding box for picking and (later) frustum culling.
        XMStoreFloat3(&submesh.Bounds.Center, 0.5f * (vMin + vMax));
        XMStoreFloat3(&submesh.Bounds.Extents, 0.5f * (vMax - vMin));

        submeshes.push_back(submesh);
        outModel.Submeshes.push_back(
            { MakeUniqueName(mesh->mName.C_Str(), m, usedNames), mesh->mMaterialIndex });
    }

    if (vertices.empty() || indices.empty())
    {
        outError = "Model has no vertices or indices.";
        return false;
    }

    // ---------------------------------------------------------------------
    // 2. Collect material info. No GPU resources are created here - that is
    //    the caller's job (TextureManager owns texture loading).
    // ---------------------------------------------------------------------
    outModel.Materials.clear();
    outModel.Materials.reserve(scene->mNumMaterials);

    for (UINT i = 0; i < scene->mNumMaterials; ++i)
    {
        const aiMaterial* aiMat = scene->mMaterials[i];

        LoadedMaterial mat;

        aiString matName;
        if (aiMat->Get(AI_MATKEY_NAME, matName) == AI_SUCCESS)
            mat.Name = matName.C_Str();
        if (mat.Name.empty())
            mat.Name = "material" + std::to_string(i);

        aiColor4D diffuse;
        if (aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS)
            mat.DiffuseAlbedo = { diffuse.r, diffuse.g, diffuse.b, diffuse.a };

        // Rough conversion from Assimp shininess to roughness. There is no
        // exact mapping between the two.
        float shininess = 0.0f;
        if (aiMat->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS && shininess > 0.0f)
            mat.Roughness = std::clamp(1.0f - (shininess / 100.0f), 0.0f, 1.0f);
        
        mat.DiffuseTextureFile = GetTextureFile(aiMat, aiTextureType_DIFFUSE);

        // FBX normally stores normal maps in the NORMALS slot, OBJ in HEIGHT.
        mat.NormalTextureFile = GetTextureFile(aiMat, aiTextureType_NORMALS);
        if (mat.NormalTextureFile.empty())
            mat.NormalTextureFile = GetTextureFile(aiMat, aiTextureType_HEIGHT);

        outModel.Materials.push_back(std::move(mat));
    }

    // ---------------------------------------------------------------------
    // 3. Create the GPU buffers.
    // ---------------------------------------------------------------------
    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint32_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = std::filesystem::path(filename).stem().string();

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->VertexBufferGPU = CreateDefaultBuffer(device, cmdList,
        vertices.data(), vbByteSize, geo->VertexBufferUploader);

    geo->IndexBufferGPU = CreateDefaultBuffer(device, cmdList,
        indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    // Character meshes easily exceed 65535 vertices, so use 32-bit indices.
    // It is fine that the hand-written box/ground still use R16_UINT - the
    // format is stored per MeshGeometry.
    geo->IndexFormat = DXGI_FORMAT_R32_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    for (size_t m = 0; m < submeshes.size(); ++m)
        geo->DrawArgs[outModel.Submeshes[m].Name] = submeshes[m];

    outModel.Geometry = std::move(geo);
    outError.clear();
    return true;
}
