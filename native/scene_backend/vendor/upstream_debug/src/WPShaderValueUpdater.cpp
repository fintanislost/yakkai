#include "WPShaderValueUpdater.hpp"
#include "Eigen/src/Core/Matrix.h"
#include "Eigen/src/Geometry/Transform.h"
#include "Scene/SceneCamera.h"
#include "Scene/SceneNode.h"
#include "Scene/Scene.h"
#include "SpriteAnimation.hpp"
#include "SpecTexs.hpp"
#include "Core/ArrayHelper.hpp"
#include "Utils/Algorism.h"
#include "Utils/Logging.h"

#include <Eigen/Dense>
#include <Eigen/Geometry>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <chrono>
#include <ctime>
#include <numeric>
#include <cmath>
#include <unordered_set>
#include <limits>
#include <sstream>

using namespace wallpaper;
using namespace Eigen;

namespace
{
std::array<float, 3> LerpVec3(const std::array<float, 3>& a, const std::array<float, 3>& b, double t) {
    const float ft = static_cast<float>(t);
    return std::array<float, 3> {
        static_cast<float>(algorism::lerp(ft, a[0], b[0])),
        static_cast<float>(algorism::lerp(ft, a[1], b[1])),
        static_cast<float>(algorism::lerp(ft, a[2], b[2])),
    };
}

Eigen::Vector3f ComputeCameraRotation(const std::array<float, 3>& eye,
                                      const std::array<float, 3>& center,
                                      const std::array<float, 3>& up) {
    const Eigen::Vector3f eyeVec(eye.data());
    const Eigen::Vector3f centerVec(center.data());
    Eigen::Vector3f       dir = centerVec - eyeVec;
    if (dir.squaredNorm() <= 1.0e-12f) {
        return Eigen::Vector3f::Zero();
    }

    dir.normalize();
    Eigen::Vector3f upVec(up.data());
    if (upVec.squaredNorm() <= 1.0e-12f) {
        upVec = Eigen::Vector3f::UnitY();
    } else {
        upVec.normalize();
    }

    Eigen::Vector3f right = dir.cross(upVec);
    if (right.squaredNorm() <= 1.0e-12f) {
        right = dir.cross(Eigen::Vector3f::UnitY());
    }
    if (right.squaredNorm() <= 1.0e-12f) {
        right = Eigen::Vector3f::UnitX();
    } else {
        right.normalize();
    }

    Eigen::Vector3f correctedUp = right.cross(dir);
    if (correctedUp.squaredNorm() <= 1.0e-12f) {
        correctedUp = Eigen::Vector3f::UnitY();
    } else {
        correctedUp.normalize();
    }

    Eigen::Matrix3f rotationMatrix;
    rotationMatrix.col(0) = right;
    rotationMatrix.col(1) = correctedUp;
    rotationMatrix.col(2) = -dir;

    const Eigen::Vector3f zyx = rotationMatrix.eulerAngles(2, 1, 0);
    return Eigen::Vector3f(zyx[2], zyx[1], zyx[0]);
}

bool SampleCameraPath(const WPCameraPathAnimation& animation,
                      double                       elapsed,
                      std::array<float, 3>&       eye,
                      std::array<float, 3>&       center,
                      std::array<float, 3>&       up) {
    if (! animation.valid || animation.segments.empty()) {
        return false;
    }

    double localTime = elapsed;
    if (animation.totalDuration > 1.0e-6) {
        localTime = std::fmod(localTime, animation.totalDuration);
        if (localTime < 0.0) {
            localTime += animation.totalDuration;
        }
    } else {
        localTime = 0.0;
    }

    const WPCameraPathSegment* segment = nullptr;
    double                     segmentTime = localTime;
    double                     accumulated = 0.0;
    for (const auto& candidate : animation.segments) {
        const double duration = candidate.duration;
        if (&candidate == &animation.segments.back() || localTime < accumulated + duration) {
            segment = &candidate;
            segmentTime = localTime - accumulated;
            break;
        }
        accumulated += duration;
    }

    if (segment == nullptr || segment->keyframes.empty()) {
        return false;
    }

    if (segment->keyframes.size() == 1) {
        eye = segment->keyframes.front().eye;
        center = segment->keyframes.front().center;
        up = segment->keyframes.front().up;
        return true;
    }

    if (segmentTime <= segment->keyframes.front().timestamp) {
        eye = segment->keyframes.front().eye;
        center = segment->keyframes.front().center;
        up = segment->keyframes.front().up;
        return true;
    }

    for (size_t i = 1; i < segment->keyframes.size(); ++i) {
        const auto& previous = segment->keyframes[i - 1];
        const auto& next = segment->keyframes[i];
        if (segmentTime > next.timestamp && i + 1 < segment->keyframes.size()) {
            continue;
        }

        const double span = std::max(1.0e-6, next.timestamp - previous.timestamp);
        const double t = std::clamp((segmentTime - previous.timestamp) / span, 0.0, 1.0);
        eye = LerpVec3(previous.eye, next.eye, t);
        center = LerpVec3(previous.center, next.center, t);
        up = LerpVec3(previous.up, next.up, t);
        return true;
    }

    eye = segment->keyframes.back().eye;
    center = segment->keyframes.back().center;
    up = segment->keyframes.back().up;
    return true;
}

template <size_t N>
std::string FormatBounds(const std::array<float, N>& minValues,
                         const std::array<float, N>& maxValues) {
    std::ostringstream oss;
    oss << "min=(";
    for (size_t i = 0; i < N; ++i) {
        if (i != 0) oss << ", ";
        oss << minValues[i];
    }
    oss << ") max=(";
    for (size_t i = 0; i < N; ++i) {
        if (i != 0) oss << ", ";
        oss << maxValues[i];
    }
    oss << ")";
    return oss.str();
}

struct TriangleStats {
    size_t largeCount { 0 };
    float  maxWidth { 0.0f };
    float  maxHeight { 0.0f };
    float  maxDiagonal { 0.0f };
};

TriangleStats ComputeTriangleStats(const std::vector<Eigen::Vector3f>& positions,
                                   const SceneIndexArray&             indices) {
    TriangleStats stats;
    const auto*   raw = reinterpret_cast<const uint16_t*>(indices.Data());
    const size_t  triangleCount = (indices.RenderDataCount() * 2) / 3;
    for (size_t triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex) {
        const size_t base = triangleIndex * 3;
        const uint16_t ia = raw[base + 0];
        const uint16_t ib = raw[base + 1];
        const uint16_t ic = raw[base + 2];
        if (ia >= positions.size() || ib >= positions.size() || ic >= positions.size()) {
            continue;
        }

        const auto& a = positions[ia];
        const auto& b = positions[ib];
        const auto& c = positions[ic];
        const float minX = std::min({ a.x(), b.x(), c.x() });
        const float maxX = std::max({ a.x(), b.x(), c.x() });
        const float minY = std::min({ a.y(), b.y(), c.y() });
        const float maxY = std::max({ a.y(), b.y(), c.y() });
        const float width = maxX - minX;
        const float height = maxY - minY;
        const float diagonal = std::hypot(width, height);
        stats.maxWidth = std::max(stats.maxWidth, width);
        stats.maxHeight = std::max(stats.maxHeight, height);
        stats.maxDiagonal = std::max(stats.maxDiagonal, diagonal);
        if (width > 512.0f || height > 512.0f || diagonal > 700.0f) {
            stats.largeCount++;
        }
    }
    return stats;
}
} // namespace

void WPShaderValueUpdater::FrameBegin() {
    /*
        using namespace std::chrono;
        auto nowTime = system_clock::to_time_t(system_clock::now());
        auto cTime   = std::localtime(&nowTime);
        m_dayTime =
            (((cTime->tm_hour * 60) + cTime->tm_min) * 60 + cTime->tm_sec) / (24.0f * 60.0f
       * 60.0f);
    */
    double new_time    = m_mouseDelayedTime + m_scene->frameTime;
    new_time           = new_time > m_parallax.delay ? m_parallax.delay : new_time;
    m_mouseDelayedTime = new_time;
    double t           = new_time / m_parallax.delay;
    m_mousePos         = std::array { (float)algorism::lerp(t, m_mousePos[0], m_mousePosInput[0]),
                              (float)algorism::lerp(t, m_mousePos[1], m_mousePosInput[1]) };
    UpdatePerspectiveCameraPath();
}

void WPShaderValueUpdater::FrameEnd() {}

void WPShaderValueUpdater::MouseInput(double x, double y) {
    if (std::abs(static_cast<float>(x) - m_mousePosInput[0]) <= 1.0e-6f &&
        std::abs(static_cast<float>(y) - m_mousePosInput[1]) <= 1.0e-6f) {
        return;
    }

    m_mousePosInput[0] = (float)x;
    m_mousePosInput[1] = (float)y;
}

MouseParallaxDebugSnapshot WPShaderValueUpdater::mouseParallaxDebugSnapshot() const
{
    using Eigen::Scaling;
    using Eigen::Vector2f;

    const Vector2f effective(&m_mousePos[0]);
    const Vector2f parallax =
        Vector2f { 0.5f, 0.5f } +
        (Scaling(1.0f, -1.0f) * effective - Vector2f { 0.5f, 0.5f }) *
            m_parallax.mouseinfluence;
    return {
        .inputPosition = m_mousePosInput,
        .effectivePosition = m_mousePos,
        .parallaxUniformPosition = { parallax[0], parallax[1] },
        .cameraEnabled = m_parallax.enable,
        .cameraAmount = m_parallax.amount,
        .cameraDelay = m_parallax.delay,
        .cameraMouseInfluence = m_parallax.mouseinfluence,
    };
}

void WPShaderValueUpdater::InitUniforms(SceneNode* pNode, const ExistsUniformOp& existsOp) {
    m_nodeUniformInfoMap[pNode] = WPUniformInfo();
    auto& info                  = m_nodeUniformInfoMap[pNode];
    info.has_MI                 = existsOp(G_MI);
    info.has_M                  = existsOp(G_M);
    info.has_AM                 = existsOp(G_AM);
    info.has_MVP                = existsOp(G_MVP);
    info.has_MVPI               = existsOp(G_MVPI);
    info.has_ETVP               = existsOp(G_ETVP);
    info.has_ETVPI              = existsOp(G_ETVPI);

    info.has_VP = existsOp(G_VP);
    info.has_EYE = existsOp(G_EYE);
    info.has_NML = existsOp(G_NML);
    info.has_LCR = existsOp(G_LCR);
    info.has_LAC = existsOp(G_LAC);
    info.has_LSC = existsOp(G_LSC);

    info.has_BONES            = existsOp(G_BONES);
    info.has_TIME             = existsOp(G_TIME);
    info.has_DAYTIME          = existsOp(G_DAYTIME);
    info.has_POINTERPOSITION  = existsOp(G_POINTERPOSITION);
    info.has_PARALLAXPOSITION = existsOp(G_PARALLAXPOSITION);
    info.has_TEXELSIZE        = existsOp(G_TEXELSIZE);
    info.has_TEXELSIZEHALF    = existsOp(G_TEXELSIZEHALF);
    info.has_SCREEN           = existsOp(G_SCREEN);
    info.has_LP               = existsOp(G_LP);

    std::accumulate(begin(info.texs), end(info.texs), 0, [&existsOp](uint index, auto& value) {
        value.has_resolution = existsOp(WE_GLTEX_RESOLUTION_NAMES[index]);
        value.has_mipmap     = existsOp(WE_GLTEX_MIPMAPINFO_NAMES[index]);
        return index + 1;
    });
}

void WPShaderValueUpdater::UpdateUniforms(SceneNode* pNode, sprite_map_t& sprites,
                                          const UpdateUniformOp& updateOp) {
    if (! pNode->Mesh()) return;

    pNode->UpdateTrans();

    const SceneCamera* camera;
    std::string_view   cam_name = pNode->Camera();
    if (! pNode->Camera().empty()) {
        camera = m_scene->cameras.at(cam_name.data()).get();
    } else
        camera = m_scene->activeCamera;

    if (! camera) return;

    auto* material = pNode->Mesh()->Material();
    if (! material) return;
    // auto& shadervs = material->customShader.updateValueList;
    // const auto& valueSet = material->customShader.valueSet;

    assert(exists(m_nodeUniformInfoMap, pNode));
    const auto& info = m_nodeUniformInfoMap[pNode];

    if (material->customShader.shader &&
        (material->customShader.shader->name == "genericimage4" ||
         material->customShader.shader->name == "puppettexturechannels")) {
        const std::string shaderName = material->customShader.shader->name;
        static std::unordered_set<const SceneNode*> loggedDiagnosticNodes;
        if (! loggedDiagnosticNodes.count(pNode)) {
            const bool hasNodeData = exists(m_nodeDataMap, pNode);
            const bool hasPuppetLayer =
                hasNodeData && m_nodeDataMap.at(pNode).puppet_layer.hasPuppet();
            LOG_INFO("%s runtime uniforms: hasNodeData=%d hasPuppetLayer=%d has_BONES=%d tex0=%s",
                     shaderName.c_str(),
                     hasNodeData ? 1 : 0,
                     hasPuppetLayer ? 1 : 0,
                     info.has_BONES ? 1 : 0,
                     material->textures.empty() ? "" : material->textures.front().c_str());

            if (shaderName == "puppettexturechannels") {
                std::ostringstream textureLog;
                textureLog << "[";
                for (size_t texIndex = 0; texIndex < material->textures.size(); ++texIndex) {
                    if (texIndex > 0) {
                        textureLog << ", ";
                    }
                    textureLog << texIndex << ":" << material->textures[texIndex];
                }
                textureLog << "]";
                LOG_INFO("puppettexturechannels runtime textures: count=%zu values=%s",
                         material->textures.size(),
                         textureLog.str().c_str());

                std::vector<std::string> constKeys;
                constKeys.reserve(material->customShader.constValues.size());
                for (const auto& el : material->customShader.constValues) {
                    constKeys.push_back(el.first);
                }
                std::sort(constKeys.begin(), constKeys.end());

                std::ostringstream constKeyLog;
                constKeyLog << "[";
                for (size_t i = 0; i < constKeys.size(); ++i) {
                    if (i > 0) {
                        constKeyLog << ", ";
                    }
                    constKeyLog << constKeys[i];
                }
                constKeyLog << "]";
                LOG_INFO("puppettexturechannels runtime const keys: count=%zu values=%s",
                         constKeys.size(),
                         constKeyLog.str().c_str());

                const auto blendIt = material->customShader.constValues.find("g_BlendMap");
                if (blendIt != material->customShader.constValues.end()) {
                    std::ostringstream blendLog;
                    blendLog << "[";
                    const size_t valueCount = std::min<size_t>(blendIt->second.size(), 16);
                    for (size_t i = 0; i < valueCount; ++i) {
                        if (i > 0) {
                            blendLog << ", ";
                        }
                        blendLog << blendIt->second[i];
                    }
                    if (blendIt->second.size() > valueCount) {
                        blendLog << ", ...";
                    }
                    blendLog << "]";
                    LOG_INFO("puppettexturechannels runtime g_BlendMap: count=%zu values=%s",
                             blendIt->second.size(),
                             blendLog.str().c_str());
                } else {
                    LOG_INFO("puppettexturechannels runtime g_BlendMap missing");
                }
            }

            if (pNode->Mesh()->VertexCount() > 0) {
                const auto& vertex = pNode->Mesh()->GetVertexArray(0);
                const auto  attrs  = vertex.GetAttrOffsetMap();
                const std::string positionAttrName(WE_IN_POSITION);
                const std::string texcoordAttrName(WE_IN_TEXCOORD);
                const std::string texcoordVec4AttrName(WE_IN_TEXCOORDVEC4);
                const std::string blendIndexAttrName(WE_IN_BLENDINDICES);
                if (shaderName == "puppettexturechannels") {
                    std::vector<std::string> attrKeys;
                    attrKeys.reserve(attrs.size());
                    for (const auto& el : attrs) {
                        attrKeys.push_back(el.first);
                    }
                    std::sort(attrKeys.begin(), attrKeys.end());

                    std::ostringstream attrLog;
                    attrLog << "[";
                    for (size_t i = 0; i < attrKeys.size(); ++i) {
                        if (i > 0) {
                            attrLog << ", ";
                        }
                        const auto& attr = attrs.at(attrKeys[i]);
                        attrLog << attrKeys[i]
                                << "@"
                                << attr.offset
                                << "/"
                                << static_cast<int>(SceneVertexArray::TypeCount(attr.attr.type));
                    }
                    attrLog << "]";
                    LOG_INFO("puppettexturechannels runtime attrs: stride=%zu values=%s",
                             vertex.OneSize(),
                             attrLog.str().c_str());
                }
                if (exists(attrs, positionAttrName) && exists(attrs, texcoordAttrName)) {
                    const auto& posAttr = attrs.at(positionAttrName);
                    const auto& uvAttr  = attrs.at(texcoordAttrName);
                    const size_t strideFloats = vertex.OneSize();
                    const size_t posOffsetFloats = posAttr.offset / sizeof(float);
                    const size_t uvOffsetFloats  = uvAttr.offset / sizeof(float);
                    const float* raw = vertex.Data();
                    const size_t vertexCount = vertex.VertexCount();

                    std::array<float, 3> rawPosMin {
                        std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::infinity(),
                    };
                    std::array<float, 3> rawPosMax {
                        -std::numeric_limits<float>::infinity(),
                        -std::numeric_limits<float>::infinity(),
                        -std::numeric_limits<float>::infinity(),
                    };
                    std::array<float, 2> rawUvMin {
                        std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::infinity(),
                    };
                    std::array<float, 2> rawUvMax {
                        -std::numeric_limits<float>::infinity(),
                        -std::numeric_limits<float>::infinity(),
                    };

                    for (size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
                        const float* posBase = raw + vertexIndex * strideFloats + posOffsetFloats;
                        const float* uvBase  = raw + vertexIndex * strideFloats + uvOffsetFloats;
                        for (size_t i = 0; i < 3; ++i) {
                            rawPosMin[i] = std::min(rawPosMin[i], posBase[i]);
                            rawPosMax[i] = std::max(rawPosMax[i], posBase[i]);
                        }
                        for (size_t i = 0; i < 2; ++i) {
                            rawUvMin[i] = std::min(rawUvMin[i], uvBase[i]);
                            rawUvMax[i] = std::max(rawUvMax[i], uvBase[i]);
                        }
                    }

                    LOG_INFO("%s runtime mesh bounds: positions=%s texcoords=%s",
                             shaderName.c_str(),
                             FormatBounds(rawPosMin, rawPosMax).c_str(),
                             FormatBounds(rawUvMin, rawUvMax).c_str());

                    if (pNode->Mesh()->IndexCount() > 0) {
                        std::vector<Eigen::Vector3f> rawPositions(vertexCount);
                        for (size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
                            const float* posBase = raw + vertexIndex * strideFloats + posOffsetFloats;
                            rawPositions[vertexIndex] = Eigen::Vector3f(posBase[0], posBase[1], posBase[2]);
                        }
                        const TriangleStats rawStats =
                            ComputeTriangleStats(rawPositions, pNode->Mesh()->GetIndexArray(0));
                        LOG_INFO("%s runtime triangle stats: large=%zu maxWidth=%.1f maxHeight=%.1f maxDiagonal=%.1f",
                                 shaderName.c_str(),
                                 rawStats.largeCount,
                                 rawStats.maxWidth,
                                 rawStats.maxHeight,
                                 rawStats.maxDiagonal);
                    }
                } else if (shaderName == "puppettexturechannels" &&
                           exists(attrs, positionAttrName) &&
                           exists(attrs, texcoordVec4AttrName)) {
                    const auto& posAttr = attrs.at(positionAttrName);
                    const auto& uv4Attr = attrs.at(texcoordVec4AttrName);
                    const size_t strideFloats = vertex.OneSize();
                    const size_t posOffsetFloats = posAttr.offset / sizeof(float);
                    const size_t uv4OffsetFloats = uv4Attr.offset / sizeof(float);
                    const float* raw = vertex.Data();
                    const size_t vertexCount = vertex.VertexCount();

                    std::array<float, 3> rawPosMin {
                        std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::infinity(),
                    };
                    std::array<float, 3> rawPosMax {
                        -std::numeric_limits<float>::infinity(),
                        -std::numeric_limits<float>::infinity(),
                        -std::numeric_limits<float>::infinity(),
                    };
                    std::array<float, 2> blendUvMin {
                        std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::infinity(),
                    };
                    std::array<float, 2> blendUvMax {
                        -std::numeric_limits<float>::infinity(),
                        -std::numeric_limits<float>::infinity(),
                    };
                    std::array<float, 2> baseUvMin {
                        std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::infinity(),
                    };
                    std::array<float, 2> baseUvMax {
                        -std::numeric_limits<float>::infinity(),
                        -std::numeric_limits<float>::infinity(),
                    };
                    uint32_t minBlendIndex = std::numeric_limits<uint32_t>::max();
                    uint32_t maxBlendIndex = 0;

                    const bool hasBlendIndices = exists(attrs, blendIndexAttrName);
                    const size_t blendIndexOffsetFloats =
                        hasBlendIndices ? attrs.at(blendIndexAttrName).offset / sizeof(float) : 0;

                    for (size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
                        const float* posBase = raw + vertexIndex * strideFloats + posOffsetFloats;
                        const float* uv4Base = raw + vertexIndex * strideFloats + uv4OffsetFloats;
                        for (size_t i = 0; i < 3; ++i) {
                            rawPosMin[i] = std::min(rawPosMin[i], posBase[i]);
                            rawPosMax[i] = std::max(rawPosMax[i], posBase[i]);
                        }
                        for (size_t i = 0; i < 2; ++i) {
                            blendUvMin[i] = std::min(blendUvMin[i], uv4Base[i]);
                            blendUvMax[i] = std::max(blendUvMax[i], uv4Base[i]);
                            baseUvMin[i] = std::min(baseUvMin[i], uv4Base[i + 2]);
                            baseUvMax[i] = std::max(baseUvMax[i], uv4Base[i + 2]);
                        }
                        if (hasBlendIndices) {
                            std::array<uint32_t, 4> blendIndices {};
                            const float* indexBase =
                                raw + vertexIndex * strideFloats + blendIndexOffsetFloats;
                            memcpy(blendIndices.data(), indexBase, sizeof(blendIndices));
                            minBlendIndex = std::min(minBlendIndex, blendIndices[0]);
                            maxBlendIndex = std::max(maxBlendIndex, blendIndices[0]);
                        }
                    }

                    LOG_INFO("puppettexturechannels runtime mesh bounds: positions=%s blendUV=%s baseUV=%s blendIndexRange=[%u,%u]",
                             FormatBounds(rawPosMin, rawPosMax).c_str(),
                             FormatBounds(blendUvMin, blendUvMax).c_str(),
                             FormatBounds(baseUvMin, baseUvMax).c_str(),
                             minBlendIndex == std::numeric_limits<uint32_t>::max() ? 0u : minBlendIndex,
                             maxBlendIndex);

                    if (pNode->Mesh()->IndexCount() > 0) {
                        std::vector<Eigen::Vector3f> rawPositions(vertexCount);
                        for (size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
                            const float* posBase = raw + vertexIndex * strideFloats + posOffsetFloats;
                            rawPositions[vertexIndex] = Eigen::Vector3f(posBase[0], posBase[1], posBase[2]);
                        }
                        const TriangleStats rawStats =
                            ComputeTriangleStats(rawPositions, pNode->Mesh()->GetIndexArray(0));
                        LOG_INFO("puppettexturechannels runtime triangle stats: large=%zu maxWidth=%.1f maxHeight=%.1f maxDiagonal=%.1f",
                                 rawStats.largeCount,
                                 rawStats.maxWidth,
                                 rawStats.maxHeight,
                                 rawStats.maxDiagonal);
                    }
                } else if (shaderName == "puppettexturechannels") {
                    LOG_INFO("puppettexturechannels runtime mesh bounds unsupported attr layout: hasPosition=%d hasTexcoord=%d hasTexcoordVec4=%d hasBlendIndices=%d",
                             exists(attrs, positionAttrName) ? 1 : 0,
                             exists(attrs, texcoordAttrName) ? 1 : 0,
                             exists(attrs, texcoordVec4AttrName) ? 1 : 0,
                             exists(attrs, blendIndexAttrName) ? 1 : 0);
                }
            }
            loggedDiagnosticNodes.insert(pNode);
        }
    }

    bool hasNodeData = exists(m_nodeDataMap, pNode);
    if (hasNodeData) {
        auto& nodeData = m_nodeDataMap.at(pNode);
        for (const auto& el : nodeData.renderTargets) {
            if (m_scene->renderTargets.count(el.second) == 0) continue;
            const auto& rt = m_scene->renderTargets[el.second];

            const auto& unifrom_tex = info.texs[el.first];

            if (unifrom_tex.has_resolution) {
                std::array<i32, 4> resolution_uint({ rt.width, rt.height, rt.width, rt.height });
                updateOp(WE_GLTEX_RESOLUTION_NAMES[el.first],
                         ShaderValue(array_cast<float>(resolution_uint)));
            }
            if (unifrom_tex.has_mipmap) {
                updateOp(WE_GLTEX_MIPMAPINFO_NAMES[el.first], (float)rt.mipmap_level);
            }
        }
        if (nodeData.puppet_layer.hasPuppet() && info.has_BONES) {
            auto data = nodeData.puppet_layer.genFrame(m_scene->frameTime);
            if (material->customShader.shader &&
                (material->customShader.shader->name == "genericimage4" ||
                 material->customShader.shader->name == "puppettexturechannels")) {
                const std::string shaderName = material->customShader.shader->name;
                static std::unordered_set<const SceneNode*> loggedBoneUploads;
                if (! loggedBoneUploads.count(pNode)) {
                    const auto& vertex = pNode->Mesh()->GetVertexArray(0);
                    const auto  attrs  = vertex.GetAttrOffsetMap();
                    const std::string positionAttrName(WE_IN_POSITION);
                    const std::string indexAttrName(WE_IN_BLENDINDICES);
                    const std::string weightAttrName(WE_IN_BLENDWEIGHTS);
                    if (exists(attrs, positionAttrName) &&
                        exists(attrs, indexAttrName) &&
                        exists(attrs, weightAttrName)) {
                        const auto& posAttr = attrs.at(positionAttrName);
                        const auto& indexAttr = attrs.at(indexAttrName);
                        const auto& weightAttr = attrs.at(weightAttrName);
                        const size_t strideFloats = vertex.OneSize();
                        const size_t posOffsetFloats = posAttr.offset / sizeof(float);
                        const size_t indexOffsetFloats = indexAttr.offset / sizeof(float);
                        const size_t weightOffsetFloats = weightAttr.offset / sizeof(float);
                        const float* raw = vertex.Data();
                        const size_t vertexCount = vertex.VertexCount();

                        std::array<float, 3> skinnedMin {
                            std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::infinity(),
                        };
                        std::array<float, 3> skinnedMax {
                            -std::numeric_limits<float>::infinity(),
                            -std::numeric_limits<float>::infinity(),
                            -std::numeric_limits<float>::infinity(),
                        };
                        std::vector<Eigen::Vector3f> skinnedPositions(vertexCount);

                        for (size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
                            const float* posBase = raw + vertexIndex * strideFloats + posOffsetFloats;
                            const float* indexBase = raw + vertexIndex * strideFloats + indexOffsetFloats;
                            const float* weightBase = raw + vertexIndex * strideFloats + weightOffsetFloats;

                            std::array<uint32_t, 4> blendIndices {};
                            memcpy(blendIndices.data(), indexBase, sizeof(blendIndices));

                            Eigen::Vector3f skinned = Eigen::Vector3f::Zero();
                            const Eigen::Vector3f pos(posBase[0], posBase[1], posBase[2]);
                            for (size_t i = 0; i < 4; ++i) {
                                const float weight = weightBase[i];
                                const size_t boneIndex = static_cast<size_t>(blendIndices[i]);
                                if (weight <= 1.0e-6f || boneIndex >= data.size()) {
                                    continue;
                                }
                                skinned += (data[boneIndex] * pos) * weight;
                            }
                            skinnedPositions[vertexIndex] = skinned;
                            for (size_t i = 0; i < 3; ++i) {
                                skinnedMin[i] = std::min(skinnedMin[i], skinned[i]);
                                skinnedMax[i] = std::max(skinnedMax[i], skinned[i]);
                            }
                        }

                        LOG_INFO("%s runtime skinned bounds: %s",
                                 shaderName.c_str(),
                                 FormatBounds(skinnedMin, skinnedMax).c_str());
                        if (pNode->Mesh()->IndexCount() > 0) {
                            const TriangleStats skinnedStats =
                                ComputeTriangleStats(skinnedPositions, pNode->Mesh()->GetIndexArray(0));
                            LOG_INFO("%s runtime skinned triangle stats: large=%zu maxWidth=%.1f maxHeight=%.1f maxDiagonal=%.1f",
                                     shaderName.c_str(),
                                     skinnedStats.largeCount,
                                     skinnedStats.maxWidth,
                                     skinnedStats.maxHeight,
                                     skinnedStats.maxDiagonal);
                        }
                    }
                    LOG_INFO("%s runtime uploading g_Bones: boneMatrices=%zu tex0=%s",
                             shaderName.c_str(),
                             data.size(),
                             material->textures.empty() ? "" : material->textures.front().c_str());
                    loggedBoneUploads.insert(pNode);
                }
            }
            updateOp(G_BONES, std::span<const float> { data[0].data(), data.size() * 16 });
        }
    }

    bool reqMI    = info.has_MI;
    bool reqM     = info.has_M;
    bool reqAM    = info.has_AM;
    bool reqMVP   = info.has_MVP;
    bool reqMVPI  = info.has_MVPI;
    bool reqETVP  = info.has_ETVP;
    bool reqETVPI = info.has_ETVPI;

    Matrix4d viewProTrans = camera->GetViewProjectionMatrix();

    if (info.has_VP) {
        updateOp(G_VP, ShaderValue::fromMatrix(viewProTrans));
    }
    if (info.has_EYE) {
        const Eigen::Vector3f eye = camera->GetPosition().cast<float>();
        updateOp(G_EYE, std::array { eye.x(), eye.y(), eye.z() });
    }
    if (reqM || reqMVP || reqMI || reqMVPI) {
        Matrix4d modelTrans = pNode->ModelTrans();
        if (hasNodeData && cam_name != "effect") {
            const auto& nodeData = m_nodeDataMap.at(pNode);
            if (m_parallax.enable) {
                Vector2f depth(&nodeData.parallaxDepth[0]);
                Vector2f ortho { (float)m_scene->ortho[0], (float)m_scene->ortho[1] };
                // Wallpaper Engine parallax is a mouse-relative offset, not a permanent
                // translation derived from the node's authored world position.
                Vector2f mouseVec =
                    Scaling(1.0f, -1.0f) * (Vector2f { 0.5f, 0.5f } - Vector2f(&m_mousePos[0]));
                mouseVec = mouseVec.cwiseProduct(ortho) * m_parallax.mouseinfluence;
                Vector2f paraVec = mouseVec.cwiseProduct(depth) * m_parallax.amount;
                modelTrans =
                    Affine3d(Translation3d(Vector3d(paraVec.x(), paraVec.y(), 0.0f))).matrix() *
                    modelTrans;
            }
        }

        if (reqM) updateOp(G_M, ShaderValue::fromMatrix(modelTrans));
        if (reqAM) updateOp(G_AM, ShaderValue::fromMatrix(modelTrans));
        if (reqMI) updateOp(G_MI, ShaderValue::fromMatrix(modelTrans.inverse()));
        if (info.has_NML) {
            Eigen::Matrix3f normalTrans = Eigen::Matrix3f::Identity();
            const Eigen::Matrix3d upper = modelTrans.topLeftCorner<3, 3>();
            const double         det    = upper.determinant();
            if (std::abs(det) > 1.0e-10) {
                normalTrans = upper.inverse().transpose().cast<float>();
            }
            updateOp(G_NML, ShaderValue::fromMatrix(normalTrans));
        }
        if (reqMVP) {
            Matrix4d mvpTrans = viewProTrans * modelTrans;
            updateOp(G_MVP, ShaderValue::fromMatrix(mvpTrans));
            if (reqMVPI) updateOp(G_MVPI, ShaderValue::fromMatrix(mvpTrans.inverse()));
        }
        if (reqETVP || reqETVPI) {
            /*
            Vector3d nodePos = pNode->Translate().cast<double>();
            nodePos.z()      = 1.0f;
            Matrix4d etvpTrans =
                viewProTrans * modelTrans * Affine3d(Eigen::Scaling(nodePos)).matrix();
            if (reqETVPI) updateOp(G_ETVP, ShaderValue::fromMatrix(etvpTrans));
            if (reqETVPI) updateOp(G_ETVPI, ShaderValue::fromMatrix(etvpTrans.inverse()));
            */
        }
    }

    //	g_EffectTextureProjectionMatrix
    // shadervs.push_back({"g_EffectTextureProjectionMatrixInverse",
    // ShaderValue::ValueOf(Eigen::Matrix4f::Identity())});
    if (info.has_TIME) updateOp(G_TIME, (float)m_scene->elapsingTime);

    if (info.has_DAYTIME) updateOp(G_DAYTIME, (float)m_dayTime);

    if (info.has_POINTERPOSITION) updateOp(G_POINTERPOSITION, m_mousePos);

    if (info.has_TEXELSIZE) updateOp(G_TEXELSIZE, m_texelSize);

    if (info.has_TEXELSIZEHALF)
        updateOp(G_TEXELSIZEHALF, std::array { m_texelSize[0] / 2.0f, m_texelSize[1] / 2.0f });

    if (info.has_SCREEN)
        updateOp(G_SCREEN,
                 std::array<float, 3> {
                     m_screen_size[0], m_screen_size[1], m_screen_size[0] / m_screen_size[1] });

    if (info.has_PARALLAXPOSITION) {
        Vector2f para =
            Vector2f { 0.5f, 0.5f } +
            (Scaling(1.0f, -1.0f) * (Vector2f(&m_mousePos[0])) - Vector2f { 0.5f, 0.5f }) *
                m_parallax.mouseinfluence;
        updateOp(G_PARALLAXPOSITION, std::array { para[0], para[1] });
    }

    for (auto& [i, sp] : sprites) {
        const auto& f      = sp.GetAnimateFrame(m_scene->frameTime);
        auto        grot   = WE_GLTEX_ROTATION_NAMES[i];
        auto        gtrans = WE_GLTEX_TRANSLATION_NAMES[i];
        updateOp(grot, std::array { f.xAxis[0], f.xAxis[1], f.yAxis[0], f.yAxis[1] });
        updateOp(gtrans, std::array { f.x, f.y });
    }

    if (info.has_LP) {
        std::array<float, 16> lights { 0 };
        std::array<float, 12> lights_color { 0 };
        std::array<float, 16> lights_color_radius { 0 };
        uint                  i = 0;
        for (auto& l : m_scene->lights) {
            if (i == 4) break;
            assert(l->node() != nullptr);
            const auto& trans = l->node()->Translate();
            std::copy(trans.begin(), trans.end(), lights.begin() + i * 4);
            const Eigen::Vector3f color_linear = l->color() * l->intensity();
            const Eigen::Vector3f color_pre    = l->premultipliedColor();
            if (i < 3) std::copy(color_pre.begin(), color_pre.end(), lights_color.begin() + i * 4);
            std::copy(color_linear.begin(), color_linear.end(), lights_color_radius.begin() + i * 4);
            lights_color_radius[i * 4 + 3] = l->radius();
            i++;
        }
        updateOp(G_LP, lights);
        updateOp(G_LCP, lights_color);
        if (info.has_LCR) updateOp(G_LCR, lights_color_radius);
    }
    if (info.has_LAC) {
        auto it = material->customShader.constValues.find(std::string(G_LAC));
        if (it != material->customShader.constValues.end()) {
            updateOp(G_LAC, it->second);
        }
    }
    if (info.has_LSC) {
        auto it = material->customShader.constValues.find(std::string(G_LSC));
        if (it != material->customShader.constValues.end()) {
            updateOp(G_LSC, it->second);
        }
    }
}

void WPShaderValueUpdater::SetNodeData(void* nodeAddr, const WPShaderValueData& data) {
    m_nodeDataMap[nodeAddr] = data;
}

void WPShaderValueUpdater::SetTexelSize(float x, float y) { m_texelSize = { x, y }; }

void WPShaderValueUpdater::SetPerspectiveCameraPath(const std::shared_ptr<SceneCamera>& camera,
                                                    const std::shared_ptr<SceneNode>&   node,
                                                    WPCameraPathAnimation               animation) {
    m_perspectiveCamera = camera;
    m_perspectiveCameraNode = node;
    m_perspectiveCameraPath = std::move(animation);
    if (m_perspectiveCameraPath.valid) {
        LOG_INFO("camera path animation enabled: segments=%zu duration=%.3f",
                 m_perspectiveCameraPath.segments.size(),
                 m_perspectiveCameraPath.totalDuration);
        UpdatePerspectiveCameraPath();
    }
}

void WPShaderValueUpdater::UpdatePerspectiveCameraPath() {
    if (! m_perspectiveCameraPath.valid || ! m_perspectiveCamera || ! m_perspectiveCameraNode) {
        return;
    }

    std::array<float, 3> eye;
    std::array<float, 3> center;
    std::array<float, 3> up;
    if (! SampleCameraPath(m_perspectiveCameraPath, m_scene->elapsingTime, eye, center, up)) {
        return;
    }

    m_perspectiveCameraNode->SetTranslate(Eigen::Vector3f(eye.data()));
    m_perspectiveCameraNode->SetRotation(ComputeCameraRotation(eye, center, up));
    m_perspectiveCamera->Update();
}
