#include "CustomShaderPass.hpp"
#include "Debug/EffectCaptureDebug.hpp"
#include "Scene/Scene.h"
#include "Scene/SceneShader.h"

#include "SpecTexs.hpp"
#include "Vulkan/Shader.hpp"
#include "Utils/Logging.h"
#include "Utils/AutoDeletor.hpp"
#include "Resource.hpp"
#include "PassCommon.hpp"
#include "Interface/IImageParser.h"

#include "Core/ArrayHelper.hpp"

#include <cassert>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>

using namespace wallpaper::vulkan;

namespace
{
bool IsArsenalModelDiagnosticPass(const CustomShaderPass::Desc& desc) {
    if (desc.node == nullptr || desc.node->Mesh() == nullptr) {
        return false;
    }
    if (desc.node->Camera() != "global_perspective") {
        return false;
    }
    for (const auto& tex : desc.textures) {
        if (tex.find("pistols/") != std::string::npos) {
            return true;
        }
    }
    return false;
}

const char* LoadOpText(VkAttachmentLoadOp loadOp) {
    switch (loadOp) {
        case VK_ATTACHMENT_LOAD_OP_LOAD: return "LOAD";
        case VK_ATTACHMENT_LOAD_OP_CLEAR: return "CLEAR";
        case VK_ATTACHMENT_LOAD_OP_DONT_CARE: return "DONT_CARE";
        default: return "UNKNOWN";
    }
}

std::string ColorMaskText(VkColorComponentFlags mask)
{
    std::string value;
    if ((mask & VK_COLOR_COMPONENT_R_BIT) != 0) value.push_back('R');
    if ((mask & VK_COLOR_COMPONENT_G_BIT) != 0) value.push_back('G');
    if ((mask & VK_COLOR_COMPONENT_B_BIT) != 0) value.push_back('B');
    if ((mask & VK_COLOR_COMPONENT_A_BIT) != 0) value.push_back('A');
    return value.empty() ? "none" : value;
}

uint32_t ColorMaskBits(VkColorComponentFlags mask)
{
    uint32_t value = 0;
    if ((mask & VK_COLOR_COMPONENT_R_BIT) != 0) value |= 1u;
    if ((mask & VK_COLOR_COMPONENT_G_BIT) != 0) value |= 2u;
    if ((mask & VK_COLOR_COMPONENT_B_BIT) != 0) value |= 4u;
    if ((mask & VK_COLOR_COMPONENT_A_BIT) != 0) value |= 8u;
    return value;
}

bool IsInterestingModelUniform(std::string_view name) {
    return name == "g_ModelViewProjectionMatrix" || name == "g_Time" ||
           name == "g_Texture0Rotation" || name == "g_Texture0Translation" ||
           name == "g_ScrollX" || name == "g_ScrollY" || name == "g_Brightness" ||
           name == "g_UserAlpha" || name == "g_Power" ||
           name == "g_Light" || name == "g_Metallic" || name == "g_Roughness" ||
           name == "g_LightAmbientColor" || name == "g_LightSkylightColor" ||
           name == "g_LightsColorRadius" || name == "g_LightsPosition";
}

std::string FormatFloatList(std::span<const float> values) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(4) << "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            stream << ", ";
        }
        stream << values[i];
    }
    stream << "]";
    return stream.str();
}

void LogInterestingUniformValue(const char* phase, const std::string& name, const wallpaper::ShaderValue& value) {
    if (!IsInterestingModelUniform(name)) {
        return;
    }
    LOG_INFO("model pass uniform value: phase=%s name=%s values=%s",
             phase,
             name.c_str(),
             FormatFloatList(std::span<const float>(value.data(), value.size())).c_str());
}

std::string FormatPackedBounds(const std::array<float, 4>& values, size_t componentCount) {
    return FormatFloatList(std::span<const float>(values.data(), componentCount));
}

bool IsMainSceneColorTarget(std::string_view output) {
    return output == wallpaper::SpecTex_Default || output == wallpaper::WE_REFLECTION_BUFFER;
}

std::vector<float> DebugVec3(const Eigen::Vector3f& value)
{
    return {value.x(), value.y(), value.z()};
}

wallpaper::debug::EffectCaptureTransformInfo DebugNodeTransform(const wallpaper::SceneNode* node)
{
    if (node == nullptr) {
        return {};
    }
    return {
        .origin = DebugVec3(node->Translate()),
        .scale = DebugVec3(node->Scale()),
        .angles = DebugVec3(node->Rotation()),
    };
}

wallpaper::debug::EffectCaptureMeshBoundsInfo DebugMeshBounds(const wallpaper::SceneMesh* mesh)
{
    wallpaper::debug::EffectCaptureMeshBoundsInfo info;
    if (mesh == nullptr) {
        return info;
    }

    info.vertexArrayCount = static_cast<int>(mesh->VertexCount());
    info.indexArrayCount = static_cast<int>(mesh->IndexCount());

    for (std::size_t i = 0; i < mesh->IndexCount(); ++i) {
        const auto& indices = mesh->GetIndexArray(i);
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

    for (std::size_t i = 0; i < mesh->VertexCount(); ++i) {
        const auto& vertex = mesh->GetVertexArray(i);
        info.vertexCount += static_cast<int>(vertex.VertexCount());

        const auto attrs = vertex.GetAttrOffsetMap();
        const auto posIt = attrs.find(std::string(wallpaper::WE_IN_POSITION));
        if (posIt == attrs.end()) {
            continue;
        }
        if (wallpaper::SceneVertexArray::TypeCount(posIt->second.attr.type) < 3) {
            continue;
        }

        const float* raw = vertex.Data();
        if (raw == nullptr) {
            continue;
        }

        const std::size_t strideFloats = vertex.OneSize();
        const std::size_t positionOffsetFloats = posIt->second.offset / sizeof(float);
        for (std::size_t vertexIndex = 0; vertexIndex < vertex.VertexCount(); ++vertexIndex) {
            const float* position = raw + vertexIndex * strideFloats + positionOffsetFloats;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                positionMin[axis] = std::min(positionMin[axis], position[axis]);
                positionMax[axis] = std::max(positionMax[axis], position[axis]);
            }
            hasPosition = true;
        }
    }

    if (hasPosition) {
        info.positionMin = {positionMin[0], positionMin[1], positionMin[2]};
        info.positionMax = {positionMax[0], positionMax[1], positionMax[2]};
    }

    return info;
}

std::vector<float> DebugWorldBounds(wallpaper::SceneNode* node,
                                    const wallpaper::debug::EffectCaptureMeshBoundsInfo& meshBounds)
{
    if (node == nullptr ||
        meshBounds.positionMin.size() < 3 ||
        meshBounds.positionMax.size() < 3) {
        return {};
    }

    node->UpdateTrans();
    const auto transform = node->ModelTrans();
    const std::array<Eigen::Vector4d, 4> corners {{
        {meshBounds.positionMin[0], meshBounds.positionMin[1], 0.0, 1.0},
        {meshBounds.positionMax[0], meshBounds.positionMin[1], 0.0, 1.0},
        {meshBounds.positionMax[0], meshBounds.positionMax[1], 0.0, 1.0},
        {meshBounds.positionMin[0], meshBounds.positionMax[1], 0.0, 1.0},
    }};

    double minX = std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();
    for (const auto& corner : corners) {
        const auto world = transform * corner;
        minX = std::min(minX, world.x());
        minY = std::min(minY, world.y());
        maxX = std::max(maxX, world.x());
        maxY = std::max(maxY, world.y());
    }
    if (!std::isfinite(minX) || !std::isfinite(minY) ||
        !std::isfinite(maxX) || !std::isfinite(maxY)) {
        return {};
    }

    return {
        static_cast<float>(minX),
        static_cast<float>(minY),
        static_cast<float>(maxX),
        static_cast<float>(maxY),
    };
}
} // namespace

CustomShaderPass::CustomShaderPass(const Desc& desc) {
    m_desc.node        = desc.node;
    m_desc.textures    = desc.textures;
    m_desc.output      = desc.output;
    m_desc.sprites_map = desc.sprites_map;
    m_desc.preserve_output = desc.preserve_output;
};
CustomShaderPass::~CustomShaderPass() {}

VkSubpassDependency wallpaper::vulkan::customShaderPassExternalDependency(bool useDepth) {
    VkAccessFlags dstAccess = VK_ACCESS_SHADER_READ_BIT |
                              VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    if (useDepth) {
        dstAccess |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                     VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }
    VkSubpassDependency dependency {
        .srcSubpass    = VK_SUBPASS_EXTERNAL,
        .dstSubpass    = 0,
        .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = dstAccess,
    };
    return dependency;
}

VkImageMemoryBarrier wallpaper::vulkan::customShaderPassTextureReadBarrier(
    VkImage image, VkImageSubresourceRange range) {
    return VkImageMemoryBarrier {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext            = nullptr,
        .srcAccessMask    = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask    = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .image            = image,
        .subresourceRange = range,
    };
}

std::optional<vvk::RenderPass> CreateRenderPass(const vvk::Device& device, VkFormat format,
                                                VkAttachmentLoadOp loadOp,
                                                VkImageLayout      finalLayout,
                                                bool               useDepth = false,
                                                VkAttachmentLoadOp depthLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE) {
    std::array<VkAttachmentDescription, 2> attachments {};
    attachments[0] = VkAttachmentDescription {
        .format         = format,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = loadOp,
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = finalLayout,
    };

    if (loadOp == VK_ATTACHMENT_LOAD_OP_LOAD) {
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    VkAttachmentReference colorAttachmentRef {
        .attachment = 0,
        .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkAttachmentReference depthAttachmentRef {
        .attachment = 1,
        .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    if (useDepth) {
        attachments[1] = VkAttachmentDescription {
            .format         = VK_FORMAT_D32_SFLOAT,
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = depthLoadOp,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };

        if (depthLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD) {
            attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }
    }

    VkSubpassDescription subpass {
        .pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &colorAttachmentRef,
        .pDepthStencilAttachment = useDepth ? &depthAttachmentRef : nullptr,
    };

    VkSubpassDependency dependency = customShaderPassExternalDependency(useDepth);

    VkRenderPassCreateInfo creatinfo {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = useDepth ? 2u : 1u,
        .pAttachments    = attachments.data(),
        .subpassCount    = 1,
        .pSubpasses      = &subpass,
        .dependencyCount = 1,
        .pDependencies   = &dependency,
    };
    vvk::RenderPass pass;
    if (auto res = device.CreateRenderPass(creatinfo, pass); res == VK_SUCCESS) {
        return pass;
    } else {
        VVK_CHECK(res);
        return std::nullopt;
    }
}

static void UpdateUniform(StagingBuffer* buf, const StagingBufferRef& bufref,
                          const ShaderReflected::Block& block, std::string_view name,
                          const wallpaper::ShaderValue& value) {
    using namespace wallpaper;
    std::span<uint8_t> value_u8 { (uint8_t*)value.data(),
                                  value.size() * sizeof(ShaderValue::value_type) };
    auto               uni = block.member_map.find(name);
    if (uni == block.member_map.end()) {
        // log
        return;
    }

    size_t offset    = uni->second.offset;
    size_t type_size = uni->second.size;
    if (value_u8.size() > type_size) {
        LOG_INFO("uniform upload size mismatch: block=%s name=%.*s expected=%zu actual=%zu",
                 block.name.c_str(),
                 static_cast<int>(name.size()),
                 name.data(),
                 type_size,
                 value_u8.size());
    }

    const size_t write_size = std::min(type_size, value_u8.size());
    if (write_size == 0) {
        return;
    }
    buf->writeToBuf(bufref, value_u8.first(write_size), offset);
}

void CustomShaderPass::prepare(Scene& scene, const Device& device, RenderingResources& rr) {
    const bool diagPass = IsArsenalModelDiagnosticPass(m_desc);
    if (diagPass) {
        const char* shaderName =
            (m_desc.node != nullptr && m_desc.node->Mesh() != nullptr &&
             m_desc.node->Mesh()->Material() != nullptr)
                ? m_desc.node->Mesh()->Material()->name.c_str()
                : "<null>";
        LOG_INFO("model pass prepare: node=%d shader=%s output=%s textures=%zu",
                 m_desc.node ? m_desc.node->ID() : -1,
                 shaderName,
                 m_desc.output.c_str(),
                 m_desc.textures.size());
    }

    m_desc.vk_textures.resize(m_desc.textures.size());
    for (usize i = 0; i < m_desc.textures.size(); i++) {
        auto& tex_name = m_desc.textures[i];
        if (tex_name.empty()) continue;

        ImageSlotsRef img_slots;
        if (IsSpecTex(tex_name)) {
            if (scene.renderTargets.count(tex_name) == 0) continue;
            auto& rt  = scene.renderTargets.at(tex_name);
            auto  opt = device.tex_cache().Query(tex_name, ToTexKey(rt), ! rt.allowReuse);
            if (! opt.has_value()) continue;
            img_slots.slots = { opt.value() };
        } else {
            auto image = scene.imageParser->Parse(tex_name);
            if (image) {
                img_slots = device.tex_cache().CreateTex(*image);
                if (image->video_decoder) {
                    device.tex_cache().RegisterVideoTexture(tex_name, image->video_decoder);
                }
                if (diagPass) {
                    const auto createdSlots = img_slots.slots.size();
                    const auto parsedSlots  = image->slots.size();
                    const auto extentText =
                        createdSlots > 0
                            ? std::to_string(img_slots.slots.front().extent.width) + "x" +
                                  std::to_string(img_slots.slots.front().extent.height)
                            : std::string("none");
                    LOG_INFO("model pass texture upload: tex=%s parsedSlots=%zu createdSlots=%zu format=%d extent=%s",
                             tex_name.c_str(),
                             parsedSlots,
                             createdSlots,
                             static_cast<int>(image->header.format),
                             extentText.c_str());
                }
            } else {
                LOG_ERROR("parse tex \"%s\" failed", tex_name.c_str());
            }
        }
        if (diagPass && img_slots.slots.empty()) {
            LOG_ERROR("model pass texture has no GPU slots: tex=%s", tex_name.c_str());
        }
        m_desc.vk_textures[i] = img_slots;
    }
    {
        auto& tex_name = m_desc.output;
        assert(IsSpecTex(tex_name));
        assert(scene.renderTargets.count(tex_name) > 0);
        auto& rt = scene.renderTargets.at(tex_name);
        if (auto opt = device.tex_cache().Query(tex_name, ToTexKey(rt), ! rt.allowReuse);
            opt.has_value()) {
            m_desc.vk_output = opt.value();
        } else
            return;
    }
    m_desc.use_depth =
        m_desc.node != nullptr && m_desc.node->Camera() == "global_perspective";
    if (m_desc.use_depth) {
        TextureKey depthKey {
            .width  = static_cast<i32>(m_desc.vk_output.extent.width),
            .height = static_cast<i32>(m_desc.vk_output.extent.height),
            .usage  = TexUsage::DEPTH,
            .format = TextureFormat::RGBA8,
        };
        const std::string depthKeyName = m_desc.output + "_depth";
        if (auto opt = device.tex_cache().Query(depthKeyName, depthKey, true);
            opt.has_value()) {
            m_desc.vk_depth = opt.value();
        } else {
            return;
        }
    }

    SceneMesh& mesh = *(m_desc.node->Mesh());

    std::vector<Uni_ShaderSpv> spvs;
    DescriptorSetInfo          descriptor_info;
    ShaderReflected            ref;
    {
        SceneShader& shader = *(mesh.Material()->customShader.shader);

        if (! GenReflect(shader.codes, spvs, ref)) {
            LOG_ERROR("gen spv reflect failed, %s", shader.name.c_str());
            return;
        }

        auto& bindings = descriptor_info.bindings;
        bindings.resize(ref.binding_map.size());

        /*
        LOG_INFO("----shader------");
        LOG_INFO("%s", shader.name.c_str());
        LOG_INFO("--inputs:");
        for (auto& i : ref.input_location_map) {
            LOG_INFO("%d %s", i.second, i.first.c_str());
        }
        LOG_INFO("--bindings:");
        */

        std::transform(
            ref.binding_map.begin(), ref.binding_map.end(), bindings.begin(), [](auto& item) {
                // LOG_INFO("%d %s", item.second.binding, item.first.c_str());
                return item.second;
            });

        for (usize i = 0; i < m_desc.vk_textures.size(); i++) {
            i32 binding { -1 };
            if (exists(ref.binding_map, WE_GLTEX_NAMES[i]))
                binding = (i32)ref.binding_map.at(WE_GLTEX_NAMES[i]).binding;
            m_desc.vk_tex_binding.push_back(binding);
            if (diagPass) {
                const auto& texName = i < m_desc.textures.size() ? m_desc.textures[i] : std::string();
                LOG_INFO("model pass texture binding: tex=%s slot=%zu binding=%d",
                         texName.c_str(),
                         i,
                         binding);
            }
        }

        if (diagPass) {
            for (const auto& item : ref.input_location_map) {
                LOG_INFO("model pass shader input: name=%s location=%u format=%u",
                         item.first.c_str(),
                         item.second.location,
                         static_cast<unsigned>(item.second.format));
            }
            for (const auto& block : ref.blocks) {
                LOG_INFO("model pass uniform block: name=%s binding=%u size=%u members=%zu",
                         block.name.c_str(),
                         block.binding,
                         block.size,
                         block.member_map.size());
                for (const auto& member : block.member_map) {
                    LOG_INFO("model pass uniform member: block=%s name=%s offset=%u size=%zu num=%zu",
                             block.name.c_str(),
                             member.first.c_str(),
                             member.second.offset,
                             member.second.size,
                             member.second.num);
                }
            }
        }
    }

    m_desc.draw_count = 0;
    std::vector<VkVertexInputBindingDescription>   bind_descriptions;
    std::vector<VkVertexInputAttributeDescription> attr_descriptions;
    {
        m_desc.dyn_vertex = mesh.Dynamic();
        m_desc.vertex_bufs.resize(mesh.VertexCount());

        for (uint i = 0; i < mesh.VertexCount(); i++) {
            const auto& vertex    = mesh.GetVertexArray(i);
            auto        attrs_map = vertex.GetAttrOffsetMap();

            VkVertexInputBindingDescription bind_desc {
                .binding   = i,
                .stride    = (uint32_t)vertex.OneSizeOf(),
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
            };
            bind_descriptions.push_back(bind_desc);

            for (auto& item : ref.input_location_map) {
                auto& name   = item.first;
                auto& input  = item.second;
                usize offset = exists(attrs_map, name) ? attrs_map[name].offset : 0;

                VkVertexInputAttributeDescription attr_desc {
                    .location = input.location,
                    .binding  = i,
                    .format   = input.format,
                    .offset   = (u32)offset,
                };
                attr_descriptions.push_back(attr_desc);

                if (diagPass) {
                    if (!exists(attrs_map, name)) {
                        LOG_ERROR("model pass missing vertex attribute: name=%s binding=%u fallbackOffset=0",
                                  name.c_str(),
                                  i);
                    } else {
                        const auto& attr = attrs_map.at(name);
                        const size_t componentCount = SceneVertexArray::TypeCount(attr.attr.type);
                        const size_t strideFloats = vertex.OneSize();
                        const size_t strideBytes = vertex.OneSizeOf();
                        const size_t offsetFloats = attr.offset / sizeof(float);
                        std::array<float, 4> firstValues { 0.0f, 0.0f, 0.0f, 0.0f };
                        std::array<float, 4> minValues {
                            std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::infinity(),
                        };
                        std::array<float, 4> maxValues {
                            -std::numeric_limits<float>::infinity(),
                            -std::numeric_limits<float>::infinity(),
                            -std::numeric_limits<float>::infinity(),
                            -std::numeric_limits<float>::infinity(),
                        };

                        const float* raw = vertex.Data();
                        const size_t vertexCount = vertex.VertexCount();
                        for (size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
                            const float* attrBase = raw + vertexIndex * strideFloats + offsetFloats;
                            for (size_t componentIndex = 0; componentIndex < componentCount; ++componentIndex) {
                                const float component = attrBase[componentIndex];
                                if (vertexIndex == 0) {
                                    firstValues[componentIndex] = component;
                                }
                                minValues[componentIndex] = std::min(minValues[componentIndex], component);
                                maxValues[componentIndex] = std::max(maxValues[componentIndex], component);
                            }
                        }

                        LOG_INFO("model pass vertex attr: name=%s binding=%u offset=%zu stride=%zu comps=%zu first=%s min=%s max=%s",
                                 name.c_str(),
                                 i,
                                 attr.offset,
                                 strideBytes,
                                 componentCount,
                                 FormatPackedBounds(firstValues, componentCount).c_str(),
                                 FormatPackedBounds(minValues, componentCount).c_str(),
                                 FormatPackedBounds(maxValues, componentCount).c_str());
                    }
                }
            }
            {
                auto& buf = m_desc.vertex_bufs[i];
                if (! m_desc.dyn_vertex) {
                    if (! rr.vertex_buf->allocateSubRef(vertex.CapacitySizeOf(), buf)) return;
                    if (! rr.vertex_buf->writeToBuf(buf, { (uint8_t*)vertex.Data(), buf.size }))
                        return;
                } else {
                    if (! rr.dyn_buf->allocateSubRef(vertex.CapacitySizeOf(), buf)) return;
                }
            }
            m_desc.draw_count += (u32)(vertex.DataSize() / vertex.OneSize());
        }

        if (mesh.IndexCount() > 0) {
            auto&  indice     = mesh.GetIndexArray(0);
            size_t count      = (indice.DataCount() * 2) / 3;
            m_desc.draw_count = (u32)count * 3;
            auto& buf         = m_desc.index_buf;
            if (! m_desc.dyn_vertex) {
                if (! rr.vertex_buf->allocateSubRef(indice.CapacitySizeof(), buf)) return;
                if (! rr.vertex_buf->writeToBuf(buf, { (uint8_t*)indice.Data(), buf.size })) return;
            } else {
                if (! rr.dyn_buf->allocateSubRef(indice.CapacitySizeof(), buf)) return;
            }
        }

        if (diagPass) {
            LOG_INFO("model pass geometry: vertexArrays=%zu indexArrays=%zu drawCount=%u",
                     mesh.VertexCount(),
                     mesh.IndexCount(),
                     m_desc.draw_count);
        }
    }
    {
        VkPipelineColorBlendAttachmentState color_blend;
        VkAttachmentLoadOp                  loadOp { VK_ATTACHMENT_LOAD_OP_DONT_CARE };
        VkAttachmentLoadOp                  depthLoadOp { VK_ATTACHMENT_LOAD_OP_DONT_CARE };
        wallpaper::BlendMode                blendmode { wallpaper::BlendMode::Normal };
        {
            VkColorComponentFlags colorMask =
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
            bool alpha =
                ! (m_desc.node->Camera().empty() || sstart_with(m_desc.node->Camera(), "global"));

            if (alpha) colorMask |= VK_COLOR_COMPONENT_A_BIT;
            color_blend.colorWriteMask = colorMask;

            blendmode = mesh.Material()->blenmode;
            SetBlend(blendmode, color_blend);
            m_desc.blending = color_blend.blendEnable;

            SetAttachmentLoadOp(blendmode, loadOp);
            if (m_desc.preserve_output) {
                loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            } else if (IsSpecTex(m_desc.output)) {
                // The first write to a temp render target should begin from a defined clear.
                loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            }
            if (m_desc.use_depth) {
                depthLoadOp = m_desc.preserve_output ? VK_ATTACHMENT_LOAD_OP_LOAD
                                                     : VK_ATTACHMENT_LOAD_OP_CLEAR;
            }

            if (diagPass) {
                LOG_INFO("model pass target state: output=%s preserve=%d loadOp=%s depthLoadOp=%s depth=%d colorMask=0x%x",
                         m_desc.output.c_str(),
                         m_desc.preserve_output ? 1 : 0,
                         LoadOpText(loadOp),
                         LoadOpText(depthLoadOp),
                         m_desc.use_depth ? 1 : 0,
                         static_cast<unsigned>(color_blend.colorWriteMask));
            }
        }
        const auto passMeshBounds = DebugMeshBounds(&mesh);
        wallpaper::debug::recordEffectPassState(scene, {
            .output = m_desc.output,
            .loadOp = LoadOpText(loadOp),
            .depthLoadOp = LoadOpText(depthLoadOp),
            .colorMask = ColorMaskText(color_blend.colorWriteMask),
            .colorMaskBits = ColorMaskBits(color_blend.colorWriteMask),
            .blendMode = std::to_string(static_cast<int>(blendmode)),
            .blendEnabled = color_blend.blendEnable == VK_TRUE,
            .preserveOutput = m_desc.preserve_output,
            .usesDepth = m_desc.use_depth,
            .camera = m_desc.node ? m_desc.node->Camera() : "",
            .nodeId = m_desc.node ? m_desc.node->ID() : -1,
            .materialName = mesh.Material() ? mesh.Material()->name : "",
            .debugPurpose = "effect-pass",
            .localTransform = DebugNodeTransform(m_desc.node),
            .meshBounds = passMeshBounds,
            .worldBounds = DebugWorldBounds(m_desc.node, passMeshBounds),
        });
        auto opt = CreateRenderPass(device.handle(),
                                    VK_FORMAT_R8G8B8A8_UNORM,
                                    loadOp,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    m_desc.use_depth,
                                    depthLoadOp);
        if (! opt.has_value()) return;
        auto& pass = opt.value();

        descriptor_info.push_descriptor = true;
        GraphicsPipeline pipeline;
        pipeline.toDefault();
        if (m_desc.use_depth) {
            pipeline.depth.depthTestEnable       = VK_TRUE;
            pipeline.depth.depthWriteEnable      = VK_TRUE;
            pipeline.depth.depthCompareOp        = VK_COMPARE_OP_LESS_OR_EQUAL;
            pipeline.depth.depthBoundsTestEnable = VK_FALSE;
            pipeline.depth.stencilTestEnable     = VK_FALSE;
        }
        pipeline.addDescriptorSetInfo(spanone { descriptor_info })
            .setColorBlendStates(spanone { color_blend })
            .setTopology(m_desc.index_buf ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
                                          : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP)
            .addInputBindingDescription(bind_descriptions)
            .addInputAttributeDescription(attr_descriptions);
        for (auto& spv : spvs) pipeline.addStage(std::move(spv));

        if (! pipeline.create(device, pass, m_desc.pipeline)) return;
    }

    {
        std::array<VkImageView, 2> attachments {
            m_desc.vk_output.view,
            m_desc.vk_depth.view,
        };
        VkFramebufferCreateInfo info {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext           = nullptr,
            .renderPass      = *m_desc.pipeline.pass,
            .attachmentCount = m_desc.use_depth ? 2u : 1u,
            .pAttachments    = attachments.data(),
            .width           = m_desc.vk_output.extent.width,
            .height          = m_desc.vk_output.extent.height,
            .layers          = 1,
        };
        VVK_CHECK_VOID_RE(device.handle().CreateFramebuffer(info, m_desc.fb));
    }

    m_desc.ubo_bufs.resize(ref.blocks.size());
    m_desc.ubo_bindings.resize(ref.blocks.size());
    m_desc.ubo_names.resize(ref.blocks.size());
    for (usize blockIndex = 0; blockIndex < ref.blocks.size(); ++blockIndex) {
        const auto& block = ref.blocks[blockIndex];
        m_desc.ubo_bindings[blockIndex] = static_cast<i32>(block.binding);
        m_desc.ubo_names[blockIndex]    = block.name;
        if (! rr.dyn_buf->allocateSubRef(
                block.size,
                m_desc.ubo_bufs[blockIndex],
                device.limits().minUniformBufferOffsetAlignment)) {
            return;
        }
    }

    if (! ref.blocks.empty()) {
        std::function<void()> update_dyn_buf_op;
        if (m_desc.dyn_vertex) {
            auto& mesh        = *m_desc.node->Mesh();
            auto* dyn_buf     = rr.dyn_buf;
            auto& vertex_bufs = m_desc.vertex_bufs;
            auto& draw_count  = m_desc.draw_count;
            auto& index_buf   = m_desc.index_buf;
            update_dyn_buf_op = [&mesh, &vertex_bufs, &draw_count, &index_buf, dyn_buf]() {
                if (mesh.Dirty().exchange(false)) {
                    for (usize i = 0; i < mesh.VertexCount(); i++) {
                        const auto& vertex = mesh.GetVertexArray(i);
                        auto&       buf    = vertex_bufs[i];
                        if (! dyn_buf->writeToBuf(buf,
                                                  { (uint8_t*)vertex.Data(), vertex.DataSizeOf() }))
                            return;
                    }
                    if (mesh.IndexCount() > 0) {
                        auto& indice = mesh.GetIndexArray(0);
                        u32   count  = (u32)((indice.RenderDataCount() * 2) / 3);
                        draw_count   = count * 3;
                        auto& buf = index_buf;
                        if (! dyn_buf->writeToBuf(buf,
                                                  { (uint8_t*)indice.Data(), indice.DataSizeOf() }))
                            return;
                    }
                }
            };
        }

        auto  blocks  = ref.blocks;
        auto* buf     = rr.dyn_buf;
        auto* bufrefs = &m_desc.ubo_bufs;

        auto* node           = m_desc.node;
        auto* shader_updater = scene.shaderValueUpdater.get();
        auto& sprites        = m_desc.sprites_map;
        auto& vk_textures    = m_desc.vk_textures;

        auto diagUpdateLogged = std::make_shared<bool>(false);

        m_desc.update_op = [shader_updater,
                            blocks,
                            buf,
                            bufrefs,
                            node,
                            &sprites,
                            &vk_textures,
                            update_dyn_buf_op,
                            diagPass,
                            diagUpdateLogged]() {
            const bool logUniformUpdate = diagPass && !*diagUpdateLogged;
            auto update_unf_op = [&blocks, buf, bufrefs](std::string_view       name,
                                                         wallpaper::ShaderValue value) {
                for (usize blockIndex = 0; blockIndex < blocks.size(); ++blockIndex) {
                    UpdateUniform(buf, (*bufrefs)[blockIndex], blocks[blockIndex], name, value);
                }
            };
            auto update_unf_op_logged = [&](std::string_view name, wallpaper::ShaderValue value) {
                if (logUniformUpdate) {
                    LogInterestingUniformValue("update", std::string(name), value);
                }
                update_unf_op(name, std::move(value));
            };
            shader_updater->UpdateUniforms(node, sprites, update_unf_op_logged);
            // update image slot for sprites
            {
                for (auto& [i, sp] : sprites) {
                    if (i >= vk_textures.size()) continue;
                    vk_textures.at(i).active = sp.GetCurFrame().imageId;
                }
            }
            if (update_dyn_buf_op) update_dyn_buf_op();
            if (logUniformUpdate) {
                *diagUpdateLogged = true;
            }
        };

        auto exists_unf_op = [&blocks](std::string_view name) {
            return std::any_of(blocks.begin(),
                               blocks.end(),
                               [name](const ShaderReflected::Block& block) {
                                   return exists(block.member_map, name);
                               });
        };
        shader_updater->InitUniforms(node, exists_unf_op);

        for (usize blockIndex = 0; blockIndex < blocks.size(); ++blockIndex) {
            auto& bufref = (*bufrefs)[blockIndex];
            buf->fillBuf(bufref, 0, bufref.size, 0);
        }
        {
            auto&      default_values = mesh.Material()->customShader.shader->default_uniforms;
            auto&      const_values   = mesh.Material()->customShader.constValues;
            std::array values_array   = { &default_values, &const_values };
            for (auto& values : values_array) {
                for (auto& v : *values) {
                    if (diagPass) {
                        LogInterestingUniformValue(values == &default_values ? "default" : "const",
                                                   v.first,
                                                   v.second);
                    }
                    for (usize blockIndex = 0; blockIndex < blocks.size(); ++blockIndex) {
                        const auto& block = blocks[blockIndex];
                        if (exists(block.member_map, v.first)) {
                            UpdateUniform(buf, (*bufrefs)[blockIndex], block, v.first, v.second);
                        }
                    }
                }
            }
        }
        m_desc.update_op();
    }

    {
        auto& sc = scene.clearColor;
        if (IsSpecTex(m_desc.output) && !IsMainSceneColorTarget(m_desc.output)) {
            m_desc.clear_value = VkClearValue {
                .color = { 0.0f, 0.0f, 0.0f, 0.0f },
            };
        } else {
            m_desc.clear_value = VkClearValue {
                .color = { sc[0], sc[1], sc[2], 1.0f },
            };
        }
    }
    for (auto& tex : releaseTexs()) {
        device.tex_cache().MarkShareReady(tex);
    }
    setPrepared();
}

void CustomShaderPass::execute(const Device&, RenderingResources& rr) {
    if (m_desc.update_op) m_desc.update_op();

    const bool diagPass = IsArsenalModelDiagnosticPass(m_desc);

    auto&                   cmd    = rr.command;
    auto&                   outext = m_desc.vk_output.extent;
    VkImageSubresourceRange base_srang {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel   = 0,
        .levelCount     = VK_REMAINING_ARRAY_LAYERS,
        .baseArrayLayer = 0,
        .layerCount     = VK_REMAINING_MIP_LEVELS,
    };
    for (usize i = 0; i < m_desc.vk_textures.size(); i++) {
        auto& slot    = m_desc.vk_textures[i];
        int   binding = m_desc.vk_tex_binding[i];
        if (binding < 0) continue;
        if (slot.slots.empty()) continue;
        auto&                 img = slot.getActive();
        if (diagPass) {
            const auto& texName = i < m_desc.textures.size() ? m_desc.textures[i] : std::string();
            LOG_INFO("model pass execute texture: tex=%s slot=%zu binding=%d active=%td extent=%ux%u",
                     texName.c_str(),
                     i,
                     binding,
                     slot.active,
                     img.extent.width,
                     img.extent.height);
        }
        VkDescriptorImageInfo desc_img { img.sampler,
                                         img.view,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkWriteDescriptorSet  wset {
             .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
             .pNext           = nullptr,
             .dstSet          = {},
             .dstBinding      = (uint32_t)binding,
             .descriptorCount = 1,
             .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
             .pImageInfo      = &desc_img,
        };
        cmd.PushDescriptorSetKHR(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_desc.pipeline.layout, 0, wset);

        VkImageMemoryBarrier imb = customShaderPassTextureReadBarrier(img.handle, base_srang);

        cmd.PipelineBarrier(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            VK_DEPENDENCY_BY_REGION_BIT,
                            imb);
    }

    for (usize blockIndex = 0; blockIndex < m_desc.ubo_bufs.size(); ++blockIndex) {
        const auto& bufref  = m_desc.ubo_bufs[blockIndex];
        const i32   binding =
            blockIndex < m_desc.ubo_bindings.size() ? m_desc.ubo_bindings[blockIndex] : -1;
        if (! bufref || binding < 0) {
            continue;
        }
        if (diagPass) {
            const auto& name =
                blockIndex < m_desc.ubo_names.size() ? m_desc.ubo_names[blockIndex] : std::string();
            LOG_INFO("model pass execute ubo: name=%s binding=%d offset=%zu size=%zu",
                     name.c_str(),
                     binding,
                     static_cast<size_t>(bufref.offset),
                     static_cast<size_t>(bufref.size));
        }
        VkDescriptorBufferInfo desc_buf {
            rr.dyn_buf->gpuBuf(),
            bufref.offset,
            bufref.size,
        };
        VkWriteDescriptorSet wset {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext           = nullptr,
            .dstSet          = {},
            .dstBinding      = static_cast<uint32_t>(binding),
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo     = &desc_buf,
        };
        cmd.PushDescriptorSetKHR(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_desc.pipeline.layout, 0, wset);
    }

    std::array<VkClearValue, 2> clearValues {
        m_desc.clear_value,
        VkClearValue { .depthStencil = { 1.0f, 0 } },
    };
    VkRenderPassBeginInfo pass_begin_info {
        .sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .pNext       = nullptr,
        .renderPass  = *m_desc.pipeline.pass,
        .framebuffer = *m_desc.fb,
        .renderArea =
            VkRect2D {
                .offset = { 0, 0 },
                .extent = { outext.width, outext.height },
            },
        .clearValueCount = m_desc.use_depth ? 2u : 1u,
        .pClearValues    = clearValues.data(),
    };
    cmd.BeginRenderPass(pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

    cmd.BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_desc.pipeline.handle);
    VkViewport viewport {
        .x        = 0,
        .y        = (float)outext.height,
        .width    = (float)outext.width,
        .height   = -(float)outext.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor { { 0, 0 }, { outext.width, outext.height } };

    cmd.SetViewport(0, viewport);
    cmd.SetScissor(0, scissor);

    auto gpu_buf = m_desc.dyn_vertex ? rr.dyn_buf->gpuBuf() : rr.vertex_buf->gpuBuf();

    for (usize i = 0; i < m_desc.vertex_bufs.size(); i++) {
        auto& buf = m_desc.vertex_bufs[i];
        cmd.BindVertexBuffers((u32)i, 1, &gpu_buf, &buf.offset);
    }
    if (m_desc.index_buf) {
        if (diagPass) {
            LOG_INFO("model pass execute draw: indexed=1 drawCount=%u indexOffset=%zu",
                     m_desc.draw_count,
                     static_cast<size_t>(m_desc.index_buf.offset));
        }
        cmd.BindIndexBuffer(gpu_buf, m_desc.index_buf.offset, VK_INDEX_TYPE_UINT16);
        cmd.DrawIndexed(m_desc.draw_count, 1, 0, 0, 0);
    } else {
        if (diagPass) {
            LOG_INFO("model pass execute draw: indexed=0 drawCount=%u",
                     m_desc.draw_count);
        }
        cmd.Draw(m_desc.draw_count, 1, 0, 0);
    }

    cmd.EndRenderPass();
}

void CustomShaderPass::destory(const Device&, RenderingResources& rr) {
    m_desc.update_op = {};
    {
        auto& buf = m_desc.dyn_vertex ? rr.dyn_buf : rr.vertex_buf;
        for (auto& bufref : m_desc.vertex_bufs) {
            buf->unallocateSubRef(bufref);
        }
    }
    for (const auto& bufref : m_desc.ubo_bufs) {
        rr.dyn_buf->unallocateSubRef(bufref);
    }
}

void CustomShaderPass::setDescTex(u32 index, std::string_view tex_key) {
    assert(index < m_desc.textures.size());
    if (index >= m_desc.textures.size()) return;
    m_desc.textures[index] = tex_key;
}
