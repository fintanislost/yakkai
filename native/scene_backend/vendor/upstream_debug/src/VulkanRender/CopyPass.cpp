#include "CopyPass.hpp"
#include "SpecTexs.hpp"
#include "Utils/Logging.h"
#include "Utils/AutoDeletor.hpp"
#include "Resource.hpp"
#include "PassCommon.hpp"

#include <algorithm>
#include <cassert>

using namespace wallpaper::vulkan;

CopyPassExtent wallpaper::vulkan::chooseCopyPassExtent(VkExtent3D src, VkExtent3D dst) {
    VkExtent3D extent {
        .width = std::min(src.width, dst.width),
        .height = std::min(src.height, dst.height),
        .depth = std::min(src.depth, dst.depth),
    };
    return {
        .extent = extent,
        .clamped = extent.width != src.width || extent.height != src.height ||
                   extent.depth != src.depth,
    };
}

CopyPass::CopyPass(const Desc& desc): m_desc(desc) {}

CopyPass::~CopyPass() {};

void CopyPass::prepare(Scene& scene, const Device& device, RenderingResources& rr) {
    if (scene.renderTargets.count(m_desc.src) == 0) {
        LOG_ERROR("%s not found", m_desc.src.c_str());
        return;
    }
    if (scene.renderTargets.count(m_desc.dst) == 0) {
        auto& rt                                   = scene.renderTargets.at(m_desc.src);
        scene.renderTargets[m_desc.dst]            = rt;
        scene.renderTargets[m_desc.dst].allowReuse = true;
    }

    std::array<std::string, 2>      textures    = { m_desc.src, m_desc.dst };
    std::array<ImageParameters*, 2> vk_textures = { &m_desc.vk_src, &m_desc.vk_dst };
    for (usize i = 0; i < textures.size(); i++) {
        auto& tex_name = textures[i];
        if (tex_name.empty()) continue;

        ImageParameters img;
        if (IsSpecTex(tex_name)) {
            auto& rt  = scene.renderTargets.at(tex_name);
            auto  opt = device.tex_cache().Query(tex_name, ToTexKey(rt), ! rt.allowReuse);
            if (opt.has_value())
                img = opt.value();
            else
                LOG_ERROR("query image from cache failed");
        } else {
            LOG_ERROR("can't copy image source");
            return;
        }
        *vk_textures[i] = img;
    }

    for (auto& tex : releaseTexs()) {
        device.tex_cache().MarkShareReady(tex);
    }

    setPrepared();
};
void CopyPass::execute(const Device& device, RenderingResources& rr) {
    auto& cmd = rr.command;
    auto& src = m_desc.vk_src;
    auto& dst = m_desc.vk_dst;
    const auto copyExtent = chooseCopyPassExtent(src.extent, dst.extent);

    if (! (src.handle && dst.handle)) {
        assert(src.handle && dst.handle);
        return;
    }
    if (copyExtent.extent.width == 0 || copyExtent.extent.height == 0 ||
        copyExtent.extent.depth == 0) {
        LOG_ERROR("copy skipped because source or destination extent is empty: %s -> %s",
                  m_desc.src.c_str(),
                  m_desc.dst.c_str());
        return;
    }
    if (copyExtent.clamped) {
        LOG_INFO("copy extent clamped: %s %ux%ux%u -> %s %ux%ux%u using %ux%ux%u",
                 m_desc.src.c_str(),
                 src.extent.width,
                 src.extent.height,
                 src.extent.depth,
                 m_desc.dst.c_str(),
                 dst.extent.width,
                 dst.extent.height,
                 dst.extent.depth,
                 copyExtent.extent.width,
                 copyExtent.extent.height,
                 copyExtent.extent.depth);
    }

    VkImageSubresourceRange srang {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,

    };
    VkImageCopy copy {
        .srcSubresource =
            VkImageSubresourceLayers {
                .aspectMask     = srang.aspectMask,
                .mipLevel       = 0,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
        .dstSubresource =
            VkImageSubresourceLayers {
                .aspectMask     = srang.aspectMask,
                .mipLevel       = 0,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
        .extent = copyExtent.extent,
    };
    {
        VkImageMemoryBarrier in_bar {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext            = nullptr,
            .srcAccessMask    = VK_ACCESS_MEMORY_READ_BIT,
            .dstAccessMask    = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .image            = src.handle,
            .subresourceRange = srang,
        };
        VkImageMemoryBarrier out_bar {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext            = nullptr,
            .srcAccessMask    = VK_ACCESS_MEMORY_READ_BIT,
            .dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image            = dst.handle,
            .subresourceRange = srang,
        };

        cmd.PipelineBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_DEPENDENCY_BY_REGION_BIT,
                            {},
                            {},
                            std::array { in_bar, out_bar });
    }
    cmd.CopyImage(src.handle,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  dst.handle,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  copy);
    {
        VkImageMemoryBarrier in_bar {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext            = nullptr,
            .srcAccessMask    = VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask    = VK_ACCESS_MEMORY_READ_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image            = src.handle,
            .subresourceRange = srang,
        };
        VkImageMemoryBarrier out_bar {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext            = nullptr,
            .srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask    = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image            = dst.handle,
            .subresourceRange = srang,
        };

        cmd.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            VK_DEPENDENCY_BY_REGION_BIT,
                            {},
                            {},
                            std::array { in_bar, out_bar });
    }

    if (dst.mipmap_level > 1) {
        device.tex_cache().RecGenerateMipmaps(cmd, dst);
    }
};
void CopyPass::destory(const Device&, RenderingResources&) {}
