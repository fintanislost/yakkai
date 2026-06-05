#pragma once
#include <string>
#include <string_view>
#include <cstdint>
#include <array>
#include <vector>
#include <memory>
#include <span>
#include <Eigen/Dense>

#include "WPPuppet.hpp"

namespace wallpaper
{

class WPShaderInfo;

namespace wpscene
{
class WPMaterial;
};
namespace fs
{
class VFS;
};

struct WPMdl {
    i32 mdlv { 13 };
    i32 mdls { 1 };
    i32 mdla { 1 };

    std::string mat_json_file;
    struct Vertex {
        std::array<float, 3>    position { 0.0f, 0.0f, 0.0f };
        std::array<float, 3>    normal { 0.0f, 0.0f, 1.0f };
        std::array<float, 4>    tangent { 1.0f, 0.0f, 0.0f, 1.0f };
        std::array<float, 2>    channelmap_texcoord { 0.0f, 0.0f };
        std::array<uint32_t, 4> blend_indices { 0, 0, 0, 0 };
        std::array<float, 4>    weight { 1.0f, 0.0f, 0.0f, 0.0f };
        std::array<float, 2>    texcoord { 0.0f, 0.0f };
        std::array<float, 2>    texcoord2 { 0.0f, 0.0f };
    };
    struct Submesh {
        std::string                               mat_json_file;
        std::vector<Vertex>                       vertexs;
        std::vector<std::array<uint16_t, 3>>      indices;
    };
    std::vector<Vertex>                  vertexs;
    std::vector<std::array<uint16_t, 3>> indices;
    std::vector<Submesh>                 submeshes;

    // std::vector<Eigen::Matrix<float, 3, 4>> bones;
    std::shared_ptr<WPPuppet> puppet;
    // combo
    // SKINNING = 1
    // BONECOUNT

    // input
    // uvec4 a_BlendIndices
    // vec4 a_BlendWeights
    // uniform mat4x3 g_Bones[BONECOUNT]
};

class SceneMesh;

class WPMdlParser {
public:
    static bool Parse(std::string_view path, fs::VFS&, WPMdl&);
    static WPPuppet::Bone::SimulationMetadata ParseBoneSimulationMetadata(std::string_view raw);

    static void AddPuppetShaderInfo(WPShaderInfo& info, const WPMdl& mdl);
    static void AddPuppetMatInfo(wpscene::WPMaterial& mat, const WPMdl& mdl);
    static std::vector<uint32_t> ExpandPuppetActiveBlendSlots(
        const WPMdl&                mdl,
        std::span<const uint32_t>   activeBlendSlots);

    static void GenPuppetMesh(SceneMesh& mesh, const WPMdl& mdl);
    static void GenPuppetMesh(SceneMesh&                  mesh,
                              const WPMdl&                mdl,
                              const std::array<float, 2>& textureMapRate);
    static bool GenPuppetMesh(SceneMesh&                    mesh,
                              const WPMdl&                  mdl,
                              std::span<const uint32_t>     activePrimaryBlendSlots,
                              bool                          includeFullyActiveTriangles = true,
                              const std::array<float, 2>&   textureMapRate = { 1.0f, 1.0f });
    static void GenPuppetChannelMapMesh(SceneMesh& mesh, const WPMdl& mdl);
    static void GenPuppetChannelMapBaseUvMesh(SceneMesh&                  mesh,
                                              const WPMdl&                mdl,
                                              const std::array<float, 2>& imageSize);
    static void GenPuppetChannelMapMesh(SceneMesh&                       mesh,
                                        const WPMdl&                     mdl,
                                        std::span<const Eigen::Affine3f> bone_affines);
    static void GenPuppetMesh(SceneMesh& mesh,
                              const WPMdl::Submesh& mdl,
                              const Eigen::Matrix3f& basis = Eigen::Matrix3f::Identity(),
                              const std::array<float, 2>& textureMapRate = { 1.0f, 1.0f });
    static void GenPuppetImageSpaceMesh(SceneMesh&                    mesh,
                                        const WPMdl&                  mdl,
                                        const std::array<float, 2>&   imageSize);
    static bool GenPuppetImageSpaceMesh(SceneMesh&                    mesh,
                                        const WPMdl&                  mdl,
                                        const std::array<float, 2>&   imageSize,
                                        std::span<const uint32_t>     activePrimaryBlendSlots,
                                        bool                          includeFullyActiveTriangles = true);
    static void GenStaticMesh(SceneMesh& mesh,
                              const WPMdl::Submesh& mdl,
                              bool                  useNormalMap,
                              bool                  useLightmap,
                              const Eigen::Matrix3f& basis = Eigen::Matrix3f::Identity());
};

} // namespace wallpaper
