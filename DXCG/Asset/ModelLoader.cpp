#include "Asset/ModelLoader.h"
#include "Asset/AssetImporter.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/config.h>   // AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS

#include <filesystem>
#include <unordered_set>
#include <algorithm>
#include <Windows.h>   // OutputDebugStringA

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

    // Returns the texture reference exactly as the model file recorded it.
    // For an embedded texture that is "*0"; otherwise it is a path, often an
    // absolute one from the artist's machine. AssetImporter turns either form
    // into a clean asset name, so we keep the raw string here as the lookup key.
    std::string GetTextureRef(const aiMaterial* mat, aiTextureType type)
    {
        if (mat->GetTextureCount(type) == 0) return {};

        aiString path;
        if (mat->GetTexture(type, 0, &path) != AI_SUCCESS) return {};

        return path.C_Str();
    }

    // Assimp 행렬 -> DirectXMath 행렬.
    //
    // 전치가 필요하다. Assimp는 열벡터 규약(M * v)으로 행렬을 저장하는데,
    // 우리 셰이더는 행벡터 규약(v * M)을 쓰기 때문이다.
    // (상수 버퍼 업로드 때 하는 전치와는 별개의 이유다)
    DirectX::XMFLOAT4X4 ToXMFloat4x4(const aiMatrix4x4& m)
    {
        return DirectX::XMFLOAT4X4(
            m.a1, m.b1, m.c1, m.d1,
            m.a2, m.b2, m.c2, m.d2,
            m.a3, m.b3, m.c3, m.d3,
            m.a4, m.b4, m.c4, m.d4);
    }

    // 정점 하나에 본 영향을 하나 추가한다.
    // 이미 MaxBoneInfluence개가 차 있으면 가장 약한 것과 비교해 교체한다.
    void AddBoneInfluence(Vertex& v, UINT boneIndex, float weight)
    {
        if (weight <= 0.0f) return;

        int weakest = -1;
        for (int i = 0; i < MaxBoneInfluence; ++i)
        {
            if (v.BoneWeights[i] == 0.0f)   // 빈 자리
            {
                v.BoneIndices[i] = boneIndex;
                v.BoneWeights[i] = weight;
                return;
            }
            if (weakest < 0 || v.BoneWeights[i] < v.BoneWeights[weakest])
                weakest = i;
        }

        // 자리가 없으면 더 큰 영향만 살린다. 버려진 가중치는 아래 정규화에서 보정된다.
        if (weakest >= 0 && weight > v.BoneWeights[weakest])
        {
            v.BoneIndices[weakest] = boneIndex;
            v.BoneWeights[weakest] = weight;
        }
    }

    // 가중치 합이 1이 되도록 맞춘다.
    // 위에서 일부를 버렸을 수 있고, 파일 자체가 정확히 1이 아닌 경우도 흔하다.
    void NormalizeBoneWeights(Vertex& v)
    {
        float sum = 0.0f;
        for (int i = 0; i < MaxBoneInfluence; ++i)
            sum += v.BoneWeights[i];

        if (sum <= 0.0f) return;   // 스키닝 안 되는 정점

        for (int i = 0; i < MaxBoneInfluence; ++i)
            v.BoneWeights[i] /= sum;
    }

    // aiNode 트리를 훑어 본 계층을 만든다.
    // 부모가 자식보다 먼저 오도록 깊이 우선으로 추가하므로,
    // 나중에 런타임에서 배열을 앞에서부터 한 번만 순회해도 월드 행렬이 누적된다.
    void BuildBoneHierarchy(const aiNode* node, int parentIndex, Skeleton& skeleton)
    {
        const std::string name = node->mName.C_Str();
        int myIndex = parentIndex;

        auto it = skeleton.BoneIndexByName.find(name);
        if (it != skeleton.BoneIndexByName.end())
        {
            myIndex = it->second;
            skeleton.Bones[myIndex].ParentIndex = parentIndex;
            skeleton.Bones[myIndex].LocalBindTransform = ToXMFloat4x4(node->mTransformation);
        }

        for (UINT i = 0; i < node->mNumChildren; ++i)
            BuildBoneHierarchy(node->mChildren[i], myIndex, skeleton);
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

    // FBX 피벗 노드를 하나로 합친다.
    //
    // 기본값(true)이면 Assimp가 본 하나를
    //   mixamorig:Hips_$AssimpFbx$_Translation
    //   mixamorig:Hips_$AssimpFbx$_Rotation
    //   mixamorig:Hips_$AssimpFbx$_Scaling
    // 처럼 여러 노드로 쪼갠다. 애니메이션 채널은 그 쪼개진 노드를 가리키는데
    // 본 이름은 "mixamorig:Hips"라서 이름 매칭이 전부 실패한다.
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);

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

    // Import-time step: pull any embedded textures out to real files and give
    // them stable names. Returns the mapping we use below to normalize every
    // texture reference, so nothing downstream ever sees "*0".
    const std::string modelName = std::filesystem::path(filename).stem().string();
    const std::string assetDir = std::filesystem::path(filename).parent_path().string();

    const TextureNameMap textureNames =
        AssetImporter::ExtractTextures(scene, modelName, assetDir.empty() ? "." : assetDir);

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

        // -----------------------------------------------------------------
        // 이 메시가 쓰는 본을 스켈레톤에 등록하고 정점에 가중치를 채운다.
        // 본은 여러 메시가 공유할 수 있으므로 이름으로 중복을 거른다.
        // -----------------------------------------------------------------
        for (UINT b = 0; b < mesh->mNumBones; ++b)
        {
            const aiBone* bone = mesh->mBones[b];
            const std::string boneName = bone->mName.C_Str();

            int boneIndex;
            auto found = outModel.Skeleton.BoneIndexByName.find(boneName);
            if (found == outModel.Skeleton.BoneIndexByName.end())
            {
                boneIndex = (int)outModel.Skeleton.Bones.size();

                BoneInfo info;
                info.Name = boneName;
                info.OffsetMatrix = ToXMFloat4x4(bone->mOffsetMatrix);

                outModel.Skeleton.Bones.push_back(info);
                outModel.Skeleton.BoneIndexByName[boneName] = boneIndex;
            }
            else
            {
                boneIndex = found->second;
            }

            for (UINT w = 0; w < bone->mNumWeights; ++w)
            {
                const aiVertexWeight& vw = bone->mWeights[w];

                // mVertexId는 이 메시 기준 번호다.
                // 우리는 모든 메시를 한 버퍼에 합쳤으므로 시작 위치를 더해준다.
                size_t vi = (size_t)submesh.BaseVertexLocation + vw.mVertexId;
                if (vi < vertices.size())
                    AddBoneInfluence(vertices[vi], (UINT)boneIndex, vw.mWeight);
            }
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

    // ---------------------------------------------------------------------
    // 1-b. 스켈레톤 마무리
    // ---------------------------------------------------------------------
    if (!outModel.Skeleton.IsEmpty())
    {
        // 4개를 넘는 영향은 버렸으므로 합이 1이 아닐 수 있다. 여기서 맞춘다.
        for (Vertex& v : vertices)
            NormalizeBoneWeights(v);

        // 노드 트리를 훑어 부모-자식 관계와 기본 자세를 채운다.
        BuildBoneHierarchy(scene->mRootNode, -1, outModel.Skeleton);

        // FBX는 루트 노드에 단위/축 보정이 들어있는 경우가 많다.
        // 최종 본 행렬에 이 역행렬을 곱해 모델 공간으로 되돌린다.
        aiMatrix4x4 rootInverse = scene->mRootNode->mTransformation;
        rootInverse.Inverse();
        outModel.Skeleton.GlobalInverseTransform = ToXMFloat4x4(rootInverse);
    }

    // ---------------------------------------------------------------------
    // 1-c. 애니메이션 클립
    // ---------------------------------------------------------------------
    outModel.Clips.clear();
    outModel.Clips.reserve(scene->mNumAnimations);

    for (UINT a = 0; a < scene->mNumAnimations; ++a)
    {
        const aiAnimation* anim = scene->mAnimations[a];

        AnimationClip clip;
        clip.Name = anim->mName.C_Str();
        if (clip.Name.empty())
            clip.Name = "clip" + std::to_string(a);

        clip.Duration = (float)anim->mDuration;
        clip.TicksPerSecond = (anim->mTicksPerSecond != 0.0)
            ? (float)anim->mTicksPerSecond
            : 25.0f;   // 파일에 값이 없으면 관례적인 기본값

        // 본 인덱스로 바로 찾을 수 있도록 스켈레톤과 같은 크기로 만들어둔다.
        clip.BoneAnimations.resize(outModel.Skeleton.Bones.size());

        for (UINT c = 0; c < anim->mNumChannels; ++c)
        {
            const aiNodeAnim* channel = anim->mChannels[c];

            // 채널은 노드 이름으로 본을 가리킨다. 본이 아닌 노드의 채널은 건너뛴다.
            int boneIndex = outModel.Skeleton.FindBone(channel->mNodeName.C_Str());
            if (boneIndex < 0)
            {
                // 여기가 많이 찍히면 이름 규칙이 안 맞는 것이다.
                // (피벗 노드가 안 합쳐졌거나, 접두사가 다르거나)
                OutputDebugStringA(
                    (std::string("[ModelLoader] unmatched channel: ") + channel->mNodeName.C_Str() + "\n").c_str());
                continue;
            }

            BoneAnimation& boneAnim = clip.BoneAnimations[boneIndex];

            boneAnim.PositionKeys.reserve(channel->mNumPositionKeys);
            for (UINT k = 0; k < channel->mNumPositionKeys; ++k)
            {
                const aiVectorKey& key = channel->mPositionKeys[k];
                boneAnim.PositionKeys.push_back(
                    { (float)key.mTime, XMFLOAT3(key.mValue.x, key.mValue.y, key.mValue.z) });
            }

            boneAnim.RotationKeys.reserve(channel->mNumRotationKeys);
            for (UINT k = 0; k < channel->mNumRotationKeys; ++k)
            {
                const aiQuatKey& key = channel->mRotationKeys[k];
                // aiQuaternion은 (w,x,y,z) 순서, XMFLOAT4는 (x,y,z,w) 순서다.
                boneAnim.RotationKeys.push_back(
                    { (float)key.mTime, XMFLOAT4(key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w) });
            }

            boneAnim.ScaleKeys.reserve(channel->mNumScalingKeys);
            for (UINT k = 0; k < channel->mNumScalingKeys; ++k)
            {
                const aiVectorKey& key = channel->mScalingKeys[k];
                boneAnim.ScaleKeys.push_back(
                    { (float)key.mTime, XMFLOAT3(key.mValue.x, key.mValue.y, key.mValue.z) });
            }
        }

        outModel.Clips.push_back(std::move(clip));
    }

    // 데이터가 제대로 들어왔는지 확인용. 여기서 0이 나오면 그 뒤 작업은 전부 헛수고다.
    {
        std::string log = "[ModelLoader] bones=" + std::to_string(outModel.Skeleton.Bones.size()) +
            " clips=" + std::to_string(outModel.Clips.size()) + "\n";

        for (const AnimationClip& c : outModel.Clips)
        {
            int animatedBones = 0;
            for (const BoneAnimation& ba : c.BoneAnimations)
                if (!ba.IsEmpty()) ++animatedBones;

            log += "  clip \"" + c.Name + "\" " + std::to_string(c.GetDurationSeconds()) +
                "s, animated bones=" + std::to_string(animatedBones) + "\n";
        }

        OutputDebugStringA(log.c_str());
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
        
        // Turn whatever the model recorded ("*0", an absolute path, ...) into the
        // asset name AssetImporter settled on. An unknown reference resolves to
        // an empty string, which downstream reads as "this material has no map".
        auto resolve = [&textureNames](const std::string& ref) -> std::string
        {
            if (ref.empty()) return {};

            auto it = textureNames.find(ref);
            return (it != textureNames.end()) ? it->second : std::string();
        };

        mat.DiffuseTextureFile = resolve(GetTextureRef(aiMat, aiTextureType_DIFFUSE));

        // FBX normally stores normal maps in the NORMALS slot, OBJ in HEIGHT.
        std::string normalRef = GetTextureRef(aiMat, aiTextureType_NORMALS);
        if (normalRef.empty())
            normalRef = GetTextureRef(aiMat, aiTextureType_HEIGHT);

        mat.NormalTextureFile = resolve(normalRef);

        outModel.Materials.push_back(std::move(mat));
    }

    // ---------------------------------------------------------------------
    // 3. Create the GPU buffers.
    // ---------------------------------------------------------------------
    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint32_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = modelName;

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
