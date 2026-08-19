#include "Asset/AssetImporter.h"

#include <assimp/scene.h>
#include <assimp/material.h>
#include <assimp/texture.h>

#include <Windows.h>   // OutputDebugStringA
#include <filesystem>
#include <cstdio>
#include <cctype>

namespace
{
    // Pairs an Assimp texture slot with the word we want in the file name.
    // Add a row here to start extracting another map type.
    struct TextureSlot
    {
        aiTextureType Type;
        const char* Usage;
    };

    const TextureSlot kSlots[] =
    {
        // Classic slots
        { aiTextureType_DIFFUSE,            "Diffuse"   },
        { aiTextureType_NORMALS,            "Normal"    },
        { aiTextureType_HEIGHT,             "Normal"    },   // OBJ stores normal maps here
        { aiTextureType_SPECULAR,           "Specular"  },
        { aiTextureType_EMISSIVE,           "Emissive"  },
        { aiTextureType_OPACITY,            "Opacity"   },
        // PBR slots - newer exporters use these instead
        { aiTextureType_BASE_COLOR,         "Diffuse"   },
        { aiTextureType_NORMAL_CAMERA,      "Normal"    },
        { aiTextureType_METALNESS,          "Metalness" },
        { aiTextureType_DIFFUSE_ROUGHNESS,  "Roughness" },
        { aiTextureType_AMBIENT_OCCLUSION,  "AO"        },
    };

    // "C:/art/hero_d.png" -> "hero_d"
    std::string StemFromPath(const std::string& path)
    {
        size_t slash = path.find_last_of("/\\");
        std::string file = (slash == std::string::npos) ? path : path.substr(slash + 1);

        size_t dot = file.find_last_of('.');
        return (dot == std::string::npos) ? file : file.substr(0, dot);
    }

    // Asset names end up on the command line (texconv) and in file paths, so
    // strip anything awkward. "Ely By K.Atienza" -> "ElyByKAtienza"
    std::string SanitizeName(const std::string& name)
    {
        std::string out;
        out.reserve(name.size());

        for (char c : name)
        {
            unsigned char uc = (unsigned char)c;
            if (std::isalnum(uc) || c == '_' || c == '-')
                out.push_back(c);
        }

        return out.empty() ? "Asset" : out;
    }

    void Log(const std::string& msg)
    {
        OutputDebugStringA(("[AssetImporter] " + msg + "\n").c_str());
    }
}

TextureNameMap AssetImporter::ExtractTextures(
    const aiScene* scene,
    const std::string& modelName,
    const std::string& outDir,
    bool overwrite)
{
    TextureNameMap result;

    if (scene == nullptr)
        return result;

    const std::string safeModelName = SanitizeName(modelName);

    // -----------------------------------------------------------------
    // 1. Walk the materials to learn what each texture is used for.
    //    A material slot tells us "this reference is the normal map", which is
    //    the only way to give the textures meaningful names.
    // -----------------------------------------------------------------
    std::unordered_map<std::string, std::string> refToUsage;

    for (unsigned int m = 0; m < scene->mNumMaterials; ++m)
    {
        const aiMaterial* mat = scene->mMaterials[m];

        for (const TextureSlot& slot : kSlots)
        {
            if (mat->GetTextureCount(slot.Type) == 0)
                continue;

            aiString path;
            if (mat->GetTexture(slot.Type, 0, &path) != AI_SUCCESS)
                continue;

            const std::string ref = path.C_Str();

            // emplace, not [] - the first slot that claims a reference wins,
            // so HEIGHT does not overwrite a usage already set by NORMALS.
            refToUsage.emplace(ref, slot.Usage);

            // Also register the bare file name. Some exporters reference an
            // embedded texture by its original path while aiTexture stores only
            // the file name (or vice versa).
            refToUsage.emplace(StemFromPath(ref), slot.Usage);

            Log("material " + std::to_string(m) + " " + slot.Usage + " -> \"" + ref + "\"");
        }
    }

    if (refToUsage.empty())
        Log("no texture references found on any material");

    // -----------------------------------------------------------------
    // 2. Write the embedded textures out with those names.
    // -----------------------------------------------------------------
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);

    std::unordered_map<std::string, int> nameCount;   // guards against collisions

    for (unsigned int i = 0; i < scene->mNumTextures; ++i)
    {
        const aiTexture* tex = scene->mTextures[i];

        const std::string indexRef = "*" + std::to_string(i);
        const std::string fileRef = tex->mFilename.C_Str();   // often set for FBX
        const std::string fileStem = StemFromPath(fileRef);

        Log("embedded " + std::to_string(i) + " filename=\"" + fileRef + "\"");

        // Try every form the material might have used to point at this texture.
        std::string usage;
        for (const std::string& key : { indexRef, fileRef, fileStem })
        {
            if (key.empty()) continue;

            auto it = refToUsage.find(key);
            if (it != refToUsage.end())
            {
                usage = it->second;
                break;
            }
        }

        if (usage.empty())
            usage = "Tex" + std::to_string(i);

        std::string baseName = safeModelName + "_" + usage;

        // Two materials can both have a Diffuse map; number the later ones.
        int& count = nameCount[baseName];
        std::string uniqueName = (count == 0) ? baseName : baseName + std::to_string(count);
        ++count;

        // Register every form so the caller can resolve whichever one the
        // material recorded.
        result[indexRef] = uniqueName;
        if (!fileRef.empty())  result[fileRef] = uniqueName;
        if (!fileStem.empty()) result[fileStem] = uniqueName;

        // mHeight == 0 means the texture is stored in a compressed container
        // (png / jpg) exactly as it appeared in the model file. In that case
        // mWidth is a byte count, not a pixel count.
        if (tex->mHeight != 0)
        {
            Log("texture " + std::to_string(i) + " is raw ARGB, not written (unsupported)");
            continue;
        }

        std::string ext = tex->achFormatHint;
        if (ext.empty()) ext = "bin";

        std::filesystem::path outPath = std::filesystem::path(outDir) / (uniqueName + "." + ext);

        if (!overwrite && std::filesystem::exists(outPath))
        {   
            Log("skip (already exists): " + outPath.string());
            continue;
        }

        FILE* fp = nullptr;
        if (fopen_s(&fp, outPath.string().c_str(), "wb") != 0 || fp == nullptr)
        {
            Log("failed to open for write: " + outPath.string());
            continue;
        }

        fwrite(tex->pcData, 1, tex->mWidth, fp);
        fclose(fp);

        Log("wrote " + outPath.string());
    }

    // -----------------------------------------------------------------
    // 3. Textures that live in separate files already have a usable name;
    //    pass them through so the caller can resolve every reference here.
    // -----------------------------------------------------------------
    for (const auto& kv : refToUsage)
    {
        if (!kv.first.empty() && kv.first[0] == '*')
            continue;   // embedded, handled above

        result.emplace(kv.first, StemFromPath(kv.first));
    }

    return result;
}
