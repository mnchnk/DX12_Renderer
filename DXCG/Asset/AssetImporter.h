#pragma once
#include <string>
#include <unordered_map>

// Forward declared so this header does not leak Assimp into the rest of the engine.
// When the importer is later split into its own executable, this is the seam.
struct aiScene;

// Maps a texture reference as stored inside the model file to the final asset name.
//   key   : "*0" for an embedded texture, or the path the model file recorded
//   value : final base file name without extension, e.g. "Ely_Diffuse"
using TextureNameMap = std::unordered_map<std::string, std::string>;

// Import-time work: anything that produces the same result every run and can
// therefore be done once, ahead of time. No D3D12 device involved.
class AssetImporter
{
public:
    // Writes every embedded texture in the scene to outDir, named
    // <modelName>_<usage>.<ext> - for example "Ely_Diffuse.png".
    // The usage is taken from the material slot that references the texture.
    //
    // Returns a map from the model's internal reference to the final base name,
    // so the runtime loader never has to deal with "*0" style references.
    //
    // Existing files are skipped unless overwrite is true.
    static TextureNameMap ExtractTextures(
        const aiScene* scene,
        const std::string& modelName,
        const std::string& outDir,
        bool overwrite = false);
};
