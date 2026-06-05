#include "Scene/PuppetFinalDisplayBuilder.hpp"

#include "SpecTexs.hpp"
#include "Utils/Logging.h"

#include <algorithm>
#include <limits>
#include <sstream>

namespace wallpaper
{
namespace
{

std::vector<float> debugVec3(const Eigen::Vector3f& value)
{
    return { value.x(), value.y(), value.z() };
}

debug::EffectCaptureTransformInfo debugNodeTransform(const SceneNode& node)
{
    return {
        .origin = debugVec3(node.Translate()),
        .scale = debugVec3(node.Scale()),
        .angles = debugVec3(node.Rotation()),
    };
}

debug::EffectCaptureMeshBoundsInfo debugMeshBounds(const SceneMesh& mesh)
{
    debug::EffectCaptureMeshBoundsInfo info;
    info.vertexArrayCount = static_cast<int>(mesh.VertexCount());
    info.indexArrayCount = static_cast<int>(mesh.IndexCount());

    for (usize i = 0; i < mesh.IndexCount(); ++i) {
        const auto& indices = mesh.GetIndexArray(i);
        info.indexDataCount += static_cast<int>(indices.DataCount());
        info.indexRenderDataCount += static_cast<int>(indices.RenderDataCount());
    }

    std::array<float, 3> positionMin {
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
    };
    std::array<float, 3> positionMax {
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };
    bool hasPosition = false;

    for (usize i = 0; i < mesh.VertexCount(); ++i) {
        const auto& vertex = mesh.GetVertexArray(i);
        info.vertexCount += static_cast<int>(vertex.VertexCount());

        const auto attrs = vertex.GetAttrOffsetMap();
        const auto posIt = attrs.find(std::string(WE_IN_POSITION));
        if (posIt == attrs.end()) {
            continue;
        }
        if (SceneVertexArray::TypeCount(posIt->second.attr.type) < 3) {
            continue;
        }

        const float* raw = vertex.Data();
        if (raw == nullptr) {
            continue;
        }

        const usize strideFloats = vertex.OneSize();
        const usize positionOffsetFloats = posIt->second.offset / sizeof(float);
        for (usize vertexIndex = 0; vertexIndex < vertex.VertexCount(); ++vertexIndex) {
            const float* position = raw + vertexIndex * strideFloats + positionOffsetFloats;
            for (usize axis = 0; axis < 3; ++axis) {
                positionMin[axis] = std::min(positionMin[axis], position[axis]);
                positionMax[axis] = std::max(positionMax[axis], position[axis]);
            }
            hasPosition = true;
        }
    }

    if (hasPosition) {
        info.positionMin = { positionMin[0], positionMin[1], positionMin[2] };
        info.positionMax = { positionMax[0], positionMax[1], positionMax[2] };
    }

    return info;
}

void genCardMesh(SceneMesh& mesh, const std::array<uint16_t, 2> size)
{
    const float left = -(size[0] / 2.0f);
    const float right = size[0] / 2.0f;
    const float bottom = -(size[1] / 2.0f);
    const float top = size[1] / 2.0f;
    const float z = 0.0f;

    const std::array pos = {
        left, bottom, z,
        left, top, z,
        right, bottom, z,
        right, top, z,
    };
    const std::array texCoord = {
        0.0f, 1.0f,
        0.0f, 0.0f,
        1.0f, 1.0f,
        1.0f, 0.0f,
    };

    SceneVertexArray vertex(
        {
            { WE_IN_POSITION.data(), VertexType::FLOAT3 },
            { WE_IN_TEXCOORD.data(), VertexType::FLOAT2 },
        },
        4);
    vertex.SetVertex(WE_IN_POSITION, pos);
    vertex.SetVertex(WE_IN_TEXCOORD, texCoord);
    mesh.AddVertexArray(std::move(vertex));
}

std::string joinActiveSlots(const std::vector<uint32_t>& activeSlots)
{
    std::ostringstream stream;
    for (std::size_t i = 0; i < activeSlots.size(); ++i) {
        if (i != 0) {
            stream << ", ";
        }
        stream << activeSlots[i];
    }
    return stream.str();
}

} // namespace

BlendMode decidePuppetFinalDisplayBlend()
{
    return BlendMode::PremultipliedTranslucent;
}

PuppetFinalDisplayBuildResult buildPuppetFinalDisplay(
    const PuppetFinalDisplayBuildInput& input)
{
    PuppetFinalDisplayBuildResult result;
    result.sourceTexture = input.finalEffectTexture;
    result.finalBlendMode = static_cast<int>(decidePuppetFinalDisplayBlend());
    result.finalMeshKind = input.routePlan.standaloneFinalMeshKind;
    result.attachMode = input.routePlan.standaloneDisplayAttachMode;
    result.nodeOrdinal = static_cast<int>(input.existingStandaloneDisplayNodeCount);

    if (input.scene == nullptr || input.puppet == nullptr) {
        LOG_ERROR("build standalone puppet final display missing scene or puppet: %s",
                  input.imageName.c_str());
        return result;
    }

    auto spFinalNode = std::make_shared<SceneNode>();
    wpscene::WPMaterial finalSourceMaterial = input.imageMaterial;
    if (finalSourceMaterial.textures.empty()) {
        finalSourceMaterial.textures.resize(1);
    }
    finalSourceMaterial.textures[0] = input.finalEffectTexture;
    if (input.usePuppetChannelMapPrepass && !input.sourceMaterial.textures.empty() &&
        !input.sourceMaterial.textures[0].empty()) {
        finalSourceMaterial.textures.resize(std::max<usize>(finalSourceMaterial.textures.size(), 3));
        finalSourceMaterial.textures[1] = input.sourceMaterial.textures[0];
        if (input.sourceMaterial.textures.size() > 1 && !input.sourceMaterial.textures[1].empty()) {
            finalSourceMaterial.textures[2] = input.sourceMaterial.textures[1];
        } else if (!input.imageMaterial.textures.empty()) {
            finalSourceMaterial.textures[2] = input.imageMaterial.textures[0];
        }
        finalSourceMaterial.combos["YAKKAI_CHANNELMAP_ALPHA_MASK"] = 1;
        LOG_INFO("native puppet final display applying channelmap/base alpha masks: image=%s channelMask=%s baseMask=%s",
                 input.imageName.c_str(),
                 finalSourceMaterial.textures[1].c_str(),
                 finalSourceMaterial.textures.size() > 2 ? finalSourceMaterial.textures[2].c_str() : "");
    }

    SceneMaterial finalMaterial;
    WPShaderValueData finalSvData;
    WPShaderInfo finalShaderInfo;
    finalShaderInfo.baseConstSvs = input.baseConstSvs;
    if (input.routePlan.finalMaterialUsesPuppetSkinning) {
        WPMdlParser::AddPuppetMatInfo(finalSourceMaterial, *input.puppet);
        WPMdlParser::AddPuppetShaderInfo(finalShaderInfo, *input.puppet);
    }
    if (!input.loadMaterial ||
        !input.loadMaterial(finalSourceMaterial,
                            spFinalNode.get(),
                            &finalMaterial,
                            &finalSvData,
                            &finalShaderInfo)) {
        LOG_ERROR("load standalone puppet final material failed: %s", input.imageName.c_str());
        return result;
    }

    if (input.loadConstValues) {
        input.loadConstValues(finalMaterial, finalSourceMaterial, finalShaderInfo);
    }
    finalMaterial.blenmode = decidePuppetFinalDisplayBlend();
    finalSvData.parallaxDepth = input.parallaxDepth;
    if (input.routePlan.finalMaterialUsesPuppetSkinning) {
        finalSvData.puppet_layer = WPPuppetLayer(input.puppet->puppet);
        auto renderPuppetLayers = input.renderPuppetLayers;
        finalSvData.puppet_layer.prepared(renderPuppetLayers);
    }

    auto spFinalMesh = std::make_shared<SceneMesh>();
    bool usingFilteredOverlayMesh = false;
    if (input.usePuppetChannelMapPrepass && !input.activePuppetChannelBlendSlots.empty()) {
        usingFilteredOverlayMesh =
            WPMdlParser::GenPuppetImageSpaceMesh(*spFinalMesh,
                                                 *input.puppet,
                                                 input.imageSize,
                                                 input.activePuppetChannelBlendSlots);
    }
    if (!usingFilteredOverlayMesh) {
        if (input.usePuppetChannelMapPrepass) {
            WPMdlParser::GenPuppetMesh(*spFinalMesh, *input.puppet);
        } else if (input.routePlan.standaloneFinalMeshKind == "layer-card") {
            genCardMesh(*spFinalMesh,
                        { static_cast<uint16_t>(input.effectViewport.width),
                          static_cast<uint16_t>(input.effectViewport.height) });
        } else {
            WPMdlParser::GenPuppetImageSpaceMesh(*spFinalMesh,
                                                 *input.puppet,
                                                 input.imageSize);
        }
    }

    if (input.debugEffectCaptures && input.effectCaptureInfo != nullptr) {
        input.effectCaptureInfo->publish.standaloneFinalMaterialBlendMode =
            static_cast<int>(finalMaterial.blenmode);
        input.effectCaptureInfo->publish.standaloneFinalTexture = input.finalEffectTexture;
        input.effectCaptureInfo->publish.standaloneFinalMeshBounds =
            debugMeshBounds(*spFinalMesh);
        input.effectCaptureInfo->publish.standaloneDisplayNodeOrdinal =
            static_cast<int>(input.existingStandaloneDisplayNodeCount);
    }

    spFinalMesh->AddMaterial(std::move(finalMaterial));
    spFinalNode->AddMesh(spFinalMesh);
    if (input.standaloneDisplayTransform != nullptr) {
        spFinalNode->CopyTrans(*input.standaloneDisplayTransform);
    }

    if (input.debugEffectCaptures && input.effectCaptureInfo != nullptr) {
        input.effectCaptureInfo->publish.standaloneDisplayLocalTransform =
            debugNodeTransform(*spFinalNode);
        input.effectCaptureInfo->publish.standaloneDisplayParentId = input.imageParentId;
        input.effectCaptureInfo->publish.standaloneDisplayHasParsedParentNode =
            input.imageParentParsed;
        const std::string finalDisplayBoundarySuffix =
            input.nodeAddr + "_" +
            std::to_string(input.effectCaptureInfo->publish.standaloneDisplayNodeOrdinal);
        const auto finalDisplayBoundaryTargets =
            debug::registerFinalDisplayBoundaryCapture(*input.scene,
                                                       *input.effectCaptureInfo,
                                                       *spFinalNode,
                                                       finalDisplayBoundarySuffix);
        input.effectCaptureInfo->publish.finalDisplayBoundaryCaptureTiming =
            finalDisplayBoundaryTargets.beforeTarget.empty()
                ? std::string()
                : "render-graph-copy-around-final-display-node";
        input.effectCaptureInfo->publish.finalDisplayBeforeRenderTarget =
            finalDisplayBoundaryTargets.beforeTarget;
        input.effectCaptureInfo->publish.finalDisplayAfterRenderTarget =
            finalDisplayBoundaryTargets.afterTarget;
    }

    result.success = true;
    result.node = spFinalNode;
    result.mesh = spFinalMesh;
    result.shaderValueData = std::move(finalSvData);
    result.standaloneFinalMeshBounds = debugMeshBounds(*spFinalMesh);
    result.standaloneDisplayLocalTransform = debugNodeTransform(*spFinalNode);
    result.finalDisplayBeforeRenderTarget =
        input.effectCaptureInfo != nullptr
            ? input.effectCaptureInfo->publish.finalDisplayBeforeRenderTarget
            : std::string();
    result.finalDisplayAfterRenderTarget =
        input.effectCaptureInfo != nullptr
            ? input.effectCaptureInfo->publish.finalDisplayAfterRenderTarget
            : std::string();
    result.finalDisplayBoundaryCaptureTiming =
        input.effectCaptureInfo != nullptr
            ? input.effectCaptureInfo->publish.finalDisplayBoundaryCaptureTiming
            : std::string();
    result.usingFilteredOverlayMesh = usingFilteredOverlayMesh;

    LOG_INFO("native puppet standalone final display enabled: image=%s tex0=%s authoredEffects=%d",
             input.imageName.c_str(),
             input.finalEffectTexture.c_str(),
             input.authoredEffectCount);
    if (usingFilteredOverlayMesh) {
        LOG_INFO("native puppet final stage using filtered image-space overlay mesh: image=%s size=%.1fx%.1f",
                 input.imageName.c_str(),
                 input.imageSize[0],
                 input.imageSize[1]);
        LOG_INFO("native puppet final overlay restricted to active blend slots: image=%s indices=[%s]",
                 input.imageName.c_str(),
                 joinActiveSlots(input.activePuppetChannelBlendSlots).c_str());
    } else if (!input.usePuppetChannelMapPrepass &&
               input.routePlan.standaloneFinalMeshKind == "layer-card") {
        LOG_INFO("native puppet final stage using layer-card effect mesh: image=%s size=%dx%d",
                 input.imageName.c_str(),
                 input.effectViewport.width,
                 input.effectViewport.height);
    } else if (!input.usePuppetChannelMapPrepass) {
        LOG_INFO("native puppet final stage using image-space effect mesh: image=%s size=%.1fx%.1f",
                 input.imageName.c_str(),
                 input.imageSize[0],
                 input.imageSize[1]);
    } else {
        LOG_INFO("native puppet final stage using authored puppet UV mesh: image=%s size=%.1fx%.1f",
                 input.imageName.c_str(),
                 input.imageSize[0],
                 input.imageSize[1]);
    }

    return result;
}

} // namespace wallpaper
