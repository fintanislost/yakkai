#include "ReflectionPass.hpp"

#include "PassCommon.hpp"
#include "Resource.hpp"
#include "Utils/Logging.h"
#include "Vulkan/Shader.hpp"

using namespace wallpaper::vulkan;

namespace
{
constexpr std::string_view vert_code = R"(#version 320 es
layout(location = 0) in vec3 Position;
layout(location = 1) in vec2 Texcoord;
layout(location = 0) out vec2 v_Texcoord;

void main()
{
    v_Texcoord = Texcoord;
    gl_Position = vec4(Position, 1.0);
}
)";

constexpr std::string_view frag_code = R"(#version 320 es
layout(location = 0) in vec2 v_Texcoord;
layout(location = 0) out vec4 out_FragColor;

layout(binding = 1) uniform sampler2D u_Texture;

void main()
{
    vec2 uv = vec2(v_Texcoord.x, 1.0 - v_Texcoord.y);
    vec3 reflected = texture(u_Texture, uv).rgb;
    out_FragColor = vec4(reflected, 1.0);
}
)";

struct VertexInput {
    std::array<float, 3> pos;
    std::array<float, 2> texcoord;
};

constexpr std::array kVertexInput = {
    VertexInput { { -1.0f, -1.0f, 0.0f }, { 0.0f, 1.0f } },
    VertexInput { { -1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },
    VertexInput { { 1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f } },
    VertexInput { { 1.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } },
};

std::optional<vvk::RenderPass> CreateRenderPass(const vvk::Device& device, VkFormat format) {
    VkAttachmentDescription attachment {
        .format         = format,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkAttachmentReference attachment_ref {
        .attachment = 0,
        .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass {
        .pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &attachment_ref,
    };

    VkRenderPassCreateInfo create_info {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments    = &attachment,
        .subpassCount    = 1,
        .pSubpasses      = &subpass,
    };

    vvk::RenderPass pass;
    const auto res = device.CreateRenderPass(create_info, pass);
    if (res == VK_SUCCESS) {
        return pass;
    }
    LOG_ERROR("reflection pass CreateRenderPass failed: %d", static_cast<int>(res));
    return std::nullopt;
}
} // namespace

ReflectionPass::ReflectionPass(const Desc& desc)
    : m_desc() {
    m_desc.src = desc.src;
    m_desc.dst = desc.dst;
}

ReflectionPass::~ReflectionPass() {}

void ReflectionPass::prepare(Scene& scene, const Device& device, RenderingResources& rr) {
    if (scene.renderTargets.count(m_desc.src) == 0 || scene.renderTargets.count(m_desc.dst) == 0) {
        LOG_ERROR("reflection pass missing render target: src=%s dst=%s",
                  m_desc.src.c_str(),
                  m_desc.dst.c_str());
        return;
    }

    auto& src_rt = scene.renderTargets.at(m_desc.src);
    auto& dst_rt = scene.renderTargets.at(m_desc.dst);

    if (auto opt = device.tex_cache().Query(m_desc.src, ToTexKey(src_rt), ! src_rt.allowReuse);
        opt.has_value()) {
        m_desc.vk_src = opt.value();
    } else {
        LOG_ERROR("reflection pass source query failed: %s", m_desc.src.c_str());
        return;
    }

    if (auto opt = device.tex_cache().Query(m_desc.dst, ToTexKey(dst_rt), ! dst_rt.allowReuse);
        opt.has_value()) {
        m_desc.vk_dst = opt.value();
    } else {
        LOG_ERROR("reflection pass destination query failed: %s", m_desc.dst.c_str());
        return;
    }

    std::vector<Uni_ShaderSpv> spvs;
    ShaderCompOpt              opt;
    opt.client_ver             = glslang::EShTargetVulkan_1_1;
    opt.relaxed_errors_glsl    = true;
    opt.relaxed_rules_vulkan   = true;
    opt.suppress_warnings_glsl = true;

    std::array<ShaderCompUnit, 2> units;
    units[0] = ShaderCompUnit { .stage = EShLangVertex, .src = std::string(vert_code) };
    units[1] = ShaderCompUnit { .stage = EShLangFragment, .src = std::string(frag_code) };
    CompileAndLinkShaderUnits(units, opt, spvs);

    VkVertexInputBindingDescription                bind_description {};
    std::vector<VkVertexInputAttributeDescription> attr_descriptions;
    bind_description.stride    = sizeof(VertexInput);
    bind_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bind_description.binding   = 0;

    VkVertexInputAttributeDescription attr_pos {
        .location = 0,
        .binding  = 0,
        .format   = VK_FORMAT_R32G32B32_SFLOAT,
        .offset   = offsetof(VertexInput, pos),
    };
    VkVertexInputAttributeDescription attr_texcoord {
        .location = 1,
        .binding  = 0,
        .format   = VK_FORMAT_R32G32_SFLOAT,
        .offset   = offsetof(VertexInput, texcoord),
    };
    attr_descriptions.push_back(attr_pos);
    attr_descriptions.push_back(attr_texcoord);

    rr.vertex_buf->allocateSubRef(sizeof(kVertexInput), m_desc.vertex_buf);
    rr.vertex_buf->writeToBuf(
        m_desc.vertex_buf,
        { reinterpret_cast<uint8_t*>(const_cast<VertexInput*>(kVertexInput.data())),
          m_desc.vertex_buf.size });

    DescriptorSetInfo descriptor_info;
    descriptor_info.push_descriptor = true;
    descriptor_info.bindings.resize(1);
    descriptor_info.bindings[0].binding         = 1;
    descriptor_info.bindings[0].descriptorCount = 1;
    descriptor_info.bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_info.bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    auto pass_opt = CreateRenderPass(device.handle(), ToVkType(ToTexKey(dst_rt).format));
    if (! pass_opt.has_value()) {
        return;
    }
    auto pass = std::move(pass_opt.value());

    GraphicsPipeline pipeline;
    pipeline.toDefault();
    pipeline.addDescriptorSetInfo(spanone { descriptor_info })
        .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP)
        .addInputBindingDescription(spanone { bind_description })
        .addInputAttributeDescription(attr_descriptions);
    for (auto& spv : spvs) {
        pipeline.addStage(std::move(spv));
    }

    if (! pipeline.create(device, pass, m_desc.pipeline)) {
        return;
    }

    m_desc.clear_value = VkClearValue { { 0.0f, 0.0f, 0.0f, 1.0f } };

    LOG_INFO("reflection pass prepare: src=%s dst=%s extent=%ux%u",
             m_desc.src.c_str(),
             m_desc.dst.c_str(),
             m_desc.vk_dst.extent.width,
             m_desc.vk_dst.extent.height);
    setPrepared();
}

void ReflectionPass::execute(const Device& device, RenderingResources& rr) {
    auto& cmd    = rr.command;
    auto& dstext = m_desc.vk_dst.extent;
    VkExtent2D dst_extent_2d { dstext.width, dstext.height };

    m_desc.fb = {};
    VkFramebufferCreateInfo info {
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass      = *m_desc.pipeline.pass,
        .attachmentCount = 1,
        .pAttachments    = &m_desc.vk_dst.view,
        .width           = dst_extent_2d.width,
        .height          = dst_extent_2d.height,
        .layers          = 1,
    };
    (void)device.handle().CreateFramebuffer(info, m_desc.fb);

    VkDescriptorImageInfo desc_img {
        .sampler     = m_desc.vk_src.sampler,
        .imageView   = m_desc.vk_src.view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet wset {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstBinding      = 1,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo      = &desc_img,
    };
    cmd.PushDescriptorSetKHR(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_desc.pipeline.layout, 0, wset);

    VkRenderPassBeginInfo pass_begin_info {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass      = *m_desc.pipeline.pass,
        .framebuffer     = *m_desc.fb,
        .renderArea      = VkRect2D { .offset = { 0, 0 }, .extent = dst_extent_2d },
        .clearValueCount = 1,
        .pClearValues    = &m_desc.clear_value,
    };
    cmd.BeginRenderPass(pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

    cmd.BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_desc.pipeline.handle);
    VkViewport viewport {
        .x        = 0.0f,
        .y        = static_cast<float>(dstext.height),
        .width    = static_cast<float>(dstext.width),
        .height   = -static_cast<float>(dstext.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor { { 0, 0 }, dst_extent_2d };
    cmd.SetViewport(0, viewport);
    cmd.SetScissor(0, scissor);

    cmd.BindVertexBuffers(
        0, 1, std::array { rr.vertex_buf->gpuBuf() }.data(), &m_desc.vertex_buf.offset);
    cmd.Draw(4, 1, 0, 0);
    cmd.EndRenderPass();
}

void ReflectionPass::destory(const Device&, RenderingResources& rr) {
    setPrepared(false);
    clearReleaseTexs();
    rr.vertex_buf->unallocateSubRef(m_desc.vertex_buf);
}
