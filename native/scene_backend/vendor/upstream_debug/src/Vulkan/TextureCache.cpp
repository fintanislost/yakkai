
#include "Shader.hpp"
#include "Swapchain.hpp"
#include "TextureCache.hpp"
#include "Device.hpp"
#include "Util.hpp"

#include "Image.hpp"
#include "VideoFrameDecoder.hpp"
#include "Core/MapSet.hpp"
#include "Core/ArrayHelper.hpp"
#include "Utils/AutoDeletor.hpp"
#include "Utils/Hash.h"
#include "include/Vulkan/Parameters.hpp"
#include "vvk/vulkan_wrapper.hpp"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>

using namespace wallpaper;
using namespace wallpaper::vulkan;

namespace wallpaper
{
namespace vulkan
{
namespace
{
VkFormat ToVkDepthFormat() { return VK_FORMAT_D32_SFLOAT; }
}

VkFormat ToVkType(TextureFormat tf) {
    switch (tf) {
    case TextureFormat::BC1: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case TextureFormat::BC2: return VK_FORMAT_BC2_UNORM_BLOCK;
    case TextureFormat::BC3: return VK_FORMAT_BC3_UNORM_BLOCK;
    case TextureFormat::BC7: return VK_FORMAT_BC7_UNORM_BLOCK;
    case TextureFormat::R8: return VK_FORMAT_R8_UNORM;
    case TextureFormat::RG8: return VK_FORMAT_R8G8_UNORM;
    case TextureFormat::RGB8: return VK_FORMAT_R8G8B8_UNORM;
    case TextureFormat::RGBA8: return VK_FORMAT_R8G8B8A8_UNORM;
    default: assert(false); return VK_FORMAT_R8G8B8A8_UNORM;
    }
}

VkSamplerAddressMode ToVkType(wallpaper::TextureWrap sam) {
    using namespace wallpaper;
    switch (sam) {
    case TextureWrap::CLAMP_TO_EDGE: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case TextureWrap::REPEAT:
    default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}
VkFilter ToVkType(wallpaper::TextureFilter sam) {
    using namespace wallpaper;
    switch (sam) {
    case TextureFilter::LINEAR: return VK_FILTER_LINEAR;
    case TextureFilter::NEAREST:
    default: return VK_FILTER_NEAREST;
    }
}
} // namespace vulkan
} // namespace wallpaper

namespace
{
VkSamplerCreateInfo GenSamplerInfo(TextureKey key) {
    auto& sam = key.sample;

    VkSamplerCreateInfo sampler_info { .sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                                       .pNext            = nullptr,
                                       .magFilter        = ToVkType(sam.magFilter),
                                       .minFilter        = (ToVkType(sam.minFilter)),
                                       .mipmapMode       = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                                       .addressModeU     = (ToVkType(sam.wrapS)),
                                       .addressModeV     = (ToVkType(sam.wrapS)),
                                       .addressModeW     = (ToVkType(sam.wrapT)),
                                       .anisotropyEnable = (false),
                                       .maxAnisotropy    = (1.0f),
                                       .compareEnable    = (false),
                                       .compareOp        = VK_COMPARE_OP_NEVER,
                                       .minLod           = (0.0f),
                                       .maxLod           = (1.0f),
                                       .borderColor      = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
                                       .unnormalizedCoordinates = (false) };
    return sampler_info;
}

bool CreateReadbackBuffer(VmaAllocator allocator, std::size_t size,
                          VmaBufferParameters& buffer) {
    VkBufferCreateInfo ci {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .size  = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    };
    buffer.req_size = ci.size;

    VmaAllocationCreateInfo vma_info = {};
    vma_info.usage                   = VMA_MEMORY_USAGE_CPU_ONLY;
    VVK_CHECK_BOOL_RE(vvk::CreateBuffer(allocator, ci, vma_info, buffer.handle));
    return true;
}

bool WriteTga(std::string_view path, std::span<const std::uint8_t> rgba, uint32_t width,
              uint32_t height) {
    std::filesystem::path outPath(path);
    if (outPath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(outPath.parent_path(), ec);
    }

    std::ofstream out(outPath, std::ios::binary);
    if (! out.is_open()) {
        LOG_ERROR("debug dump open failed: %s", outPath.string().c_str());
        return false;
    }

    std::array<std::uint8_t, 18> header {};
    header[2]  = 2; // uncompressed true-color
    header[12] = static_cast<std::uint8_t>(width & 0xffu);
    header[13] = static_cast<std::uint8_t>((width >> 8u) & 0xffu);
    header[14] = static_cast<std::uint8_t>(height & 0xffu);
    header[15] = static_cast<std::uint8_t>((height >> 8u) & 0xffu);
    header[16] = 32;
    header[17] = 0x28; // top-left origin, 8 alpha bits
    out.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));

    std::vector<std::uint8_t> bgra(rgba.size());
    for (usize i = 0; i + 3 < rgba.size(); i += 4) {
        bgra[i + 0] = rgba[i + 2];
        bgra[i + 1] = rgba[i + 1];
        bgra[i + 2] = rgba[i + 0];
        bgra[i + 3] = rgba[i + 3];
    }
    out.write(reinterpret_cast<const char*>(bgra.data()),
              static_cast<std::streamsize>(bgra.size()));
    return out.good();
}

VkResult TransImgLayout(const vvk::Queue& queue, vvk::CommandBuffer& cmd,
                        const ImageParameters& image, VkImageLayout layout,
                        VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT) {
    VkResult result;
    do {
        cmd.Reset();
        result = cmd.Begin(VkCommandBufferBeginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        });
        if (result != VK_SUCCESS) break;

        VkImageSubresourceRange subresourceRange {
            .aspectMask     = aspectMask,
            .baseMipLevel   = 0,
            .levelCount     = VK_REMAINING_MIP_LEVELS,
            .baseArrayLayer = 0,
            .layerCount     = VK_REMAINING_ARRAY_LAYERS,
        };
        {
            VkPipelineStageFlags dstStage  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            VkAccessFlags        dstAccess = VK_ACCESS_MEMORY_READ_BIT;
            if (aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT) {
                dstStage  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
                dstAccess =
                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            }
            VkImageMemoryBarrier out_bar {
                .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext            = nullptr,
                .srcAccessMask    = VK_ACCESS_MEMORY_WRITE_BIT,
                .dstAccessMask    = dstAccess,
                .oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout        = layout,
                .image            = image.handle,
                .subresourceRange = subresourceRange,
            };
            cmd.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                                dstStage,
                                VK_DEPENDENCY_BY_REGION_BIT,
                                out_bar);
        }
        result = cmd.End();
        if (result != VK_SUCCESS) break;

        VkSubmitInfo sub_info {
            .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext              = nullptr,
            .commandBufferCount = 1,
            .pCommandBuffers    = cmd.address(),
        };
        result = queue.Submit(sub_info);
    } while (false);
    return result;
}

std::optional<vvk::DeviceMemory> AllocateMemory(const vvk::Device& device, vvk::PhysicalDevice gpu,
                                                VkMemoryRequirements  reqs,
                                                VkMemoryPropertyFlags property,
                                                void*                 pNext = NULL) {
    VkPhysicalDeviceMemoryProperties pros = gpu.GetMemoryProperties().memoryProperties;
    for (uint32_t i = 0; i < pros.memoryTypeCount; ++i) {
        if ((reqs.memoryTypeBits & (1 << i)) && (pros.memoryTypes[i].propertyFlags & property)) {
            VkMemoryAllocateInfo memory_allocate_info { .sType =
                                                            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                                        .pNext           = pNext,
                                                        .allocationSize  = reqs.size,
                                                        .memoryTypeIndex = i };
            vvk::DeviceMemory    mem;
            VkResult             res = device.AllocateMemory(memory_allocate_info, mem);
            if (res == VK_SUCCESS) {
                return mem;
            } else {
                VVK_CHECK(res);
                return std::nullopt;
            }
        }
    }
    LOG_ERROR("vulkan allocate memory failed, no memory match requires");
    return std::nullopt;
}

std::optional<ExImageParameters> CreateExImage(uint32_t width, uint32_t height, VkFormat format,
                                               VkImageTiling       tiling,
                                               VkSamplerCreateInfo sampler_info,
                                               VkImageUsageFlags usage, const vvk::Device& device,
                                               const vvk::PhysicalDevice& gpu) {
    ExImageParameters image;
    do {
        VkExternalMemoryImageCreateInfo ex_info {
            .sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
            .pNext       = NULL,
            .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT
        };
        VkExportMemoryAllocateInfo ex_mem_info { .sType =
                                                     VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
                                                 .pNext = NULL,
                                                 .handleTypes =
                                                     VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT };
        VkImageCreateInfo          info {
                     .sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                     .pNext       = &ex_info,
                     .imageType   = VK_IMAGE_TYPE_2D,
                     .format      = format,
                     .extent      = VkExtent3D { .width = width, .height = height, .depth = 1 },
                     .mipLevels   = 1,
                     .arrayLayers = 1,
                     .samples     = VK_SAMPLE_COUNT_1_BIT,
                     .tiling      = tiling,
                     .usage       = usage,
                     .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                     .queueFamilyIndexCount = 0,
                     .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        image.extent = info.extent;

        VVK_CHECK_ACT(break, device.CreateImage(info, image.handle));

        image.mem_reqs = device.GetImageMemoryRequirements(*image.handle);

        if (auto opt = AllocateMemory(
                device, gpu, image.mem_reqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &ex_mem_info);
            opt.has_value()) {
            image.mem = std::move(opt.value());
        } else
            break;

        VVK_CHECK_ACT(break, image.handle.BindMemory(*image.mem, 0));
        {
            VkImageViewCreateInfo createinfo {
                .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .pNext    = nullptr,
                .image    = *image.handle,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format   = format,
                .subresourceRange =
                    VkImageSubresourceRange {
                        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel   = 0,
                        .levelCount     = 1,
                        .baseArrayLayer = 0,
                        .layerCount     = 1,
                    },
            };
            VVK_CHECK_ACT(break, device.CreateImageView(createinfo, image.view));
        }
        VVK_CHECK_ACT(break, device.CreateSampler(sampler_info, image.sampler));
        VVK_CHECK_ACT(break, image.mem.GetMemoryFdKHR(&image.fd));

        return image;

    } while (false);
    return std::nullopt;
}

inline std::optional<VmaImageParameters>
CreateImage(const Device& device, VkExtent3D extent, u32 miplevel, VkFormat format,
            VkSamplerCreateInfo sampler_info, VkImageUsageFlags usage,
            VkImageAspectFlags aspectMask,
            VmaMemoryUsage mem_usage = VMA_MEMORY_USAGE_GPU_ONLY) {
    VmaImageParameters image;
    do {
        VkImageCreateInfo info {
            .sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext                 = nullptr,
            .imageType             = VK_IMAGE_TYPE_2D,
            .format                = format,
            .extent                = extent,
            .mipLevels             = miplevel,
            .arrayLayers           = 1,
            .samples               = VK_SAMPLE_COUNT_1_BIT,
            .tiling                = VK_IMAGE_TILING_OPTIMAL,
            .usage                 = usage,
            .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        image.extent = info.extent;
        VmaAllocationCreateInfo vma_info {};
        vma_info.usage = mem_usage;
        VVK_CHECK_ACT(break,
                      vvk::CreateImage(device.vma_allocator(), info, vma_info, image.handle));

        image.mipmap_level = miplevel;
        {
            VkImageViewCreateInfo createinfo {
                .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .pNext    = nullptr,
                .image    = *image.handle,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format   = format,
                .subresourceRange =
                    VkImageSubresourceRange {
                        .aspectMask     = aspectMask,
                        .baseMipLevel   = 0,
                        .levelCount     = miplevel,
                        .baseArrayLayer = 0,
                        .layerCount     = 1,
                    },
            };
            VVK_CHECK_ACT(break, device.handle().CreateImageView(createinfo, image.view));
        }
        VVK_CHECK_ACT(break, device.handle().CreateSampler(sampler_info, image.sampler));
        return image;
    } while (false);
    /*
    if (result != vk::Result::eSuccess) {
        device.DestroyImageParameters(image);
    }
    */
    return std::nullopt;
}

inline VkResult CopyImageData(std::span<const BufferParameters> in_bufs,
                              std::span<const VkExtent3D> in_exts, const vvk::Queue& queue,
                              vvk::CommandBuffer& cmd, const ImageParameters& image) {
    VkResult result;
    do {
        cmd.Reset();
        result = cmd.Begin(VkCommandBufferBeginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        });
        if (result != VK_SUCCESS) break;

        VkImageSubresourceRange subresourceRange {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = (uint32_t)in_bufs.size(),
            .baseArrayLayer = 0,
            .layerCount     = 1,
        };
        {
            VkImageMemoryBarrier in_bar {
                .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext            = nullptr,
                .srcAccessMask    = VK_ACCESS_MEMORY_WRITE_BIT,
                .dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .image            = image.handle,
                .subresourceRange = subresourceRange,
            };
            cmd.PipelineBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_DEPENDENCY_BY_REGION_BIT,
                                in_bar);
        }
        VkBufferImageCopy copy {
            .imageSubresource =
                VkImageSubresourceLayers {
                    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseArrayLayer = 0,
                    .layerCount     = 1,
                },
        };
        for (usize i = 0; i < in_bufs.size(); i++) {
            copy.imageSubresource.mipLevel = (u32)i;
            copy.imageExtent               = in_exts[i];
            cmd.CopyBufferToImage(
                in_bufs[i].handle, image.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, copy);
        }
        {
            VkImageMemoryBarrier out_bar {
                .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext            = nullptr,
                .srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask    = VK_ACCESS_SHADER_READ_BIT,
                .oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .image            = image.handle,
                .subresourceRange = subresourceRange,
            };
            cmd.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                VK_DEPENDENCY_BY_REGION_BIT,
                                out_bar);
        }
        result = cmd.End();
        if (result != VK_SUCCESS) break;

        VkSubmitInfo sub_info {
            .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext              = nullptr,
            .commandBufferCount = 1,
            .pCommandBuffers    = cmd.address(),
        };
        result = queue.Submit(sub_info);
    } while (false);
    return result;
}
} // namespace

std::size_t TextureKey::HashValue(const TextureKey& k) {
    std::size_t seed { 0 };
    utils::hash_combine(seed, k.width);
    utils::hash_combine(seed, k.height);
    utils::hash_combine(seed, (int)k.usage);
    utils::hash_combine(seed, (int)k.format);
    utils::hash_combine(seed, (int)k.mipmap_level);

    utils::hash_combine(seed, (int)k.sample.wrapS);
    utils::hash_combine(seed, (int)k.sample.wrapT);
    utils::hash_combine(seed, (int)k.sample.magFilter);
    return seed;
}

std::optional<ExImageParameters> TextureCache::CreateExTex(uint32_t width, uint32_t height,
                                                           VkFormat format, VkImageTiling tiling) {
    VkSamplerCreateInfo sampler_info {
        .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext                   = nullptr,
        .magFilter               = VK_FILTER_NEAREST,
        .minFilter               = VK_FILTER_NEAREST,
        .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU            = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
        .addressModeV            = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
        .addressModeW            = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
        .anisotropyEnable        = false,
        .maxAnisotropy           = 1.0f,
        .compareEnable           = false,
        .compareOp               = VK_COMPARE_OP_NEVER,
        .minLod                  = 0.0f,
        .maxLod                  = 1.0f,
        .borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = false,
    };

    auto opt = CreateExImage(width,
                             height,
                             format,
                             tiling,
                             sampler_info,
                             VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                             m_device.device(),
                             m_device.gpu());
    if (opt.has_value()) {
        const auto& eximg = opt.value();

        if (! m_tex_cmd) allocateCmd();
        TransImgLayout(m_device.graphics_queue().handle, m_tex_cmd, eximg, VK_IMAGE_LAYOUT_GENERAL);
        VVK_CHECK(m_device.handle().WaitIdle());
    }
    return opt;
}

ImageSlotsRef TextureCache::CreateTex(Image& image) {
    if (exists(m_tex_map, image.key)) {
        return m_tex_map.at(image.key);
    }

    ImageSlots img_slots;

    if (! m_tex_cmd) allocateCmd();

    img_slots.slots.resize(image.slots.size());

    auto& sam = image.header.sample;

    for (usize i = 0; i < image.slots.size(); i++) {
        auto& image_paras   = img_slots.slots[i];
        auto& image_slot    = image.slots[i];
        auto  mipmap_levels = image_slot.mipmaps.size();

        // check data
        if (! image_slot) return {};
        VkSamplerCreateInfo sampler_info {
            .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .pNext                   = nullptr,
            .magFilter               = ToVkType(sam.magFilter),
            .minFilter               = (ToVkType(sam.minFilter)),
            .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU            = (ToVkType(sam.wrapS)),
            .addressModeV            = (ToVkType(sam.wrapS)),
            .addressModeW            = (ToVkType(sam.wrapT)),
            .anisotropyEnable        = (false),
            .maxAnisotropy           = (1.0f),
            .compareEnable           = (false),
            .compareOp               = VK_COMPARE_OP_NEVER,
            .minLod                  = (0.0f),
            .maxLod                  = (float)mipmap_levels,
            .borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            .unnormalizedCoordinates = (false),
        };
        VkFormat   format = ToVkType(image.header.format);
        VkExtent3D ext { (u32)image_slot.width, (u32)image_slot.height, 1 };

        if (auto opt = CreateImage(m_device,
                                   ext,
                                   (u32)mipmap_levels,
                                   format,
                                   sampler_info,
                                   VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                   VK_IMAGE_ASPECT_COLOR_BIT);
            opt.has_value()) {
            image_paras = std::move(opt.value());
        } else
            break;

        std::vector<VmaBufferParameters> stage_bufs;
        std::vector<VkExtent3D>          extents;

        for (usize j = 0; j < image_slot.mipmaps.size(); j++) {
            auto&               image_data = image_slot.mipmaps[j];
            VmaBufferParameters buf;
            (void)CreateStagingBuffer(m_device.vma_allocator(), (u32)image_data.size, buf);
            {
                void* v_data;
                VVK_CHECK(buf.handle.MapMemory(&v_data));
                memcpy(v_data, image_data.data.get(), (u32)image_data.size);
                buf.handle.UnMapMemory();
            }
            stage_bufs.emplace_back(std::move(buf));
            extents.push_back(VkExtent3D { (u32)image_data.width, (u32)image_data.height, 1 });
        }

        CopyImageData(transform<VmaBufferParameters>(stage_bufs,
                                                     [](BufferParameters e) {
                                                         return e;
                                                     }),
                      extents,
                      m_device.graphics_queue().handle,
                      m_tex_cmd,
                      image_paras);

        m_device.handle().WaitIdle();
    }
    m_tex_map[image.key] = std::move(img_slots);
    return m_tex_map[image.key];
}

void TextureCache::allocateCmd() {
    const auto& pool = m_device.cmd_pool();
    VVK_CHECK(pool.Allocate(1, VK_COMMAND_BUFFER_LEVEL_PRIMARY, m_tex_cmds));
    m_tex_cmd = vvk::CommandBuffer(m_tex_cmds[0], m_device.handle().Dispatch());
}

std::optional<VmaImageParameters> TextureCache::CreateTex(TextureKey tex_key) {
    VmaImageParameters image_paras;
    do {
        VkSamplerCreateInfo sam_info = GenSamplerInfo(tex_key);
        const bool          isDepth  = tex_key.usage == TexUsage::DEPTH;
        VkFormat            format   = isDepth ? ToVkDepthFormat() : ToVkType(tex_key.format);
        VkExtent3D          ext { (u32)tex_key.width, (u32)tex_key.height, 1 };
        VkImageUsageFlags usage = isDepth
                                      ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                                      : (VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                         VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                         VK_IMAGE_USAGE_SAMPLED_BIT |
                                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        VkImageAspectFlags aspectMask =
            isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

        if (auto opt =
                CreateImage(m_device,
                            ext,
                            tex_key.mipmap_level,
                            format,
                            sam_info,
                            usage,
                            aspectMask);
            opt.has_value()) {
            image_paras = std::move(opt.value());
        } else
            break;

        if (! m_tex_cmd) allocateCmd();
        TransImgLayout(m_device.graphics_queue().handle,
                       m_tex_cmd,
                       image_paras,
                       isDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                               : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       aspectMask);

        VVK_CHECK_ACT(break, m_device.handle().WaitIdle());
        return image_paras;
    } while (false);
    return std::nullopt;
}

TextureCache::TextureCache(const Device& device): m_device(device) {}

TextureCache::~TextureCache() {};

void TextureCache::Clear() {
    // Stop all video decode threads before clearing textures
    for (auto& entry : m_video_textures) {
        if (entry.decoder) entry.decoder->Stop();
    }
    m_video_textures.clear();

    m_tex_map.clear();
    m_query_texs.clear();
    m_query_map.clear();
}

std::optional<ImageParameters> TextureCache::Query(std::string_view key, TextureKey content_hash,
                                                   bool persist) {
    if (exists(m_query_map, key)) {
        auto& query = *(m_query_map.find(key)->second);

        query.share_ready = false;
        query.persist     = persist;

        return query.image;
    };

    TexHash tex_hash = TextureKey::HashValue(content_hash);
    for (auto& query : m_query_texs) {
        if (! (query->share_ready)) continue;
        if (query->content_hash != tex_hash) continue;

        query->share_ready = false;
        query->persist     = persist;
        query->query_keys.insert(std::string(key));

        m_query_map[std::string(key)] = &(*query);

        return query->image;
    }

    m_query_texs.emplace_back(std::make_unique<QueryTex>());
    auto& query                   = *m_query_texs.back();
    m_query_map[std::string(key)] = &query;

    query.index        = (idx)m_query_texs.size() - 1;
    query.content_hash = tex_hash;
    query.query_keys.insert(std::string(key));
    query.persist = persist;
    if (auto opt = CreateTex(content_hash); opt.has_value()) {
        query.image = std::move(opt.value());
        return query.image;
    }
    return std::nullopt;
}

void TextureCache::MarkShareReady(std::string_view key) {
    if (exists(m_query_map, key)) {
        auto& query = m_query_map.find(key)->second;
        if (query->persist) return;
        query->share_ready = true;
        m_query_map.erase(key.data());
    }
}

bool TextureCache::DumpTexture(std::string_view key, std::string_view path) {
    QueryTex* query { nullptr };
    if (exists(m_query_map, key)) {
        query = m_query_map.at(std::string(key));
    } else {
        for (auto& candidate : m_query_texs) {
            if (candidate == nullptr) continue;
            if (candidate->query_keys.count(std::string(key)) == 0) continue;
            query = candidate.get();
            break;
        }
    }

    if (query == nullptr) {
        LOG_ERROR("debug dump texture not found in cache: %s", std::string(key).c_str());
        return false;
    }

    const auto& image = query->image;
    if (! image.handle || image.extent.width == 0 || image.extent.height == 0) {
        LOG_ERROR("debug dump texture invalid: %s", std::string(key).c_str());
        return false;
    }

    const std::size_t pixelBytes = static_cast<std::size_t>(image.extent.width) *
                                   static_cast<std::size_t>(image.extent.height) * 4u;
    VmaBufferParameters readbackBuffer;
    if (! CreateReadbackBuffer(m_device.vma_allocator(), pixelBytes, readbackBuffer)) {
        LOG_ERROR("debug dump buffer allocation failed: %s", std::string(key).c_str());
        return false;
    }
    if (! m_tex_cmd) allocateCmd();

    VkImageSubresourceRange subresourceRange {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };
    VkBufferImageCopy copyRegion {
        .bufferOffset      = 0,
        .bufferRowLength   = 0,
        .bufferImageHeight = 0,
        .imageSubresource =
            VkImageSubresourceLayers {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel       = 0,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
        .imageOffset = { 0, 0, 0 },
        .imageExtent = { image.extent.width, image.extent.height, 1 },
    };

    VVK_CHECK_BOOL_RE(m_tex_cmd.Begin(VkCommandBufferBeginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    }));
    {
        VkImageMemoryBarrier barrier {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext            = nullptr,
            .srcAccessMask    = VK_ACCESS_SHADER_READ_BIT,
            .dstAccessMask    = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .image            = *image.handle,
            .subresourceRange = subresourceRange,
        };
        m_tex_cmd.PipelineBarrier(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  VK_DEPENDENCY_BY_REGION_BIT,
                                  barrier);
    }
    m_tex_cmd.CopyImageToBuffer(*image.handle,
                                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                *readbackBuffer.handle,
                                spanone<VkBufferImageCopy> { copyRegion });
    {
        VkImageMemoryBarrier barrier {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext            = nullptr,
            .srcAccessMask    = VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask    = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image            = *image.handle,
            .subresourceRange = subresourceRange,
        };
        m_tex_cmd.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                  VK_DEPENDENCY_BY_REGION_BIT,
                                  barrier);
    }
    VVK_CHECK_BOOL_RE(m_tex_cmd.End());

    VkSubmitInfo submitInfo {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext              = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers    = m_tex_cmd.address(),
    };
    VVK_CHECK_BOOL_RE(m_device.graphics_queue().handle.Submit(submitInfo));
    VVK_CHECK_BOOL_RE(m_device.handle().WaitIdle());

    std::vector<std::uint8_t> rgba(pixelBytes);
    void* mapped { nullptr };
    VVK_CHECK_BOOL_RE(readbackBuffer.handle.MapMemory(&mapped));
    std::memcpy(rgba.data(), mapped, rgba.size());
    readbackBuffer.handle.UnMapMemory();

    const bool ok = WriteTga(path, rgba, image.extent.width, image.extent.height);
    if (ok) {
        LOG_INFO("debug dump texture wrote: key=%s path=%s size=%ux%u",
                 std::string(key).c_str(),
                 std::string(path).c_str(),
                 image.extent.width,
                 image.extent.height);
    }
    return ok;
}

void TextureCache::RecGenerateMipmaps(vvk::CommandBuffer& cmd, const ImageParameters& image) const {
    VkImageMemoryBarrier barrier {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext               = nullptr,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = image.handle,
        .subresourceRange =
            VkImageSubresourceRange {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
    };
    /*
    cmd.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        VK_DEPENDENCY_BY_REGION_BIT,
                        out_bar);
        */

    i32 mipWidth  = (i32)image.extent.width;
    i32 mipHeight = (i32)image.extent.height;

    for (uint i = 1; i < image.mipmap_level; i++) {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout                     = i == 1 ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                       : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

        barrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        cmd.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_DEPENDENCY_BY_REGION_BIT,
                            barrier);

        barrier.subresourceRange.baseMipLevel = i;
        barrier.oldLayout                     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout                     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

        cmd.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_DEPENDENCY_BY_REGION_BIT,
                            barrier);

        VkImageBlit blit {
            .srcSubresource =
                VkImageSubresourceLayers {
                    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel       = i - 1,
                    .baseArrayLayer = 0,
                    .layerCount     = 1,
                },
            .srcOffsets = { VkOffset3D { 0, 0, 0 }, VkOffset3D { mipWidth, mipHeight, 1 } },
            .dstOffsets = { VkOffset3D { 0, 0, 0 },
                            VkOffset3D { mipWidth > 1 ? mipWidth / 2 : 1,
                                         mipHeight > 1 ? mipHeight / 2 : 1,
                                         1 } },
        };
        blit.dstSubresource =
            VkImageSubresourceLayers {
                .aspectMask     = blit.srcSubresource.aspectMask,
                .mipLevel       = blit.srcSubresource.mipLevel + 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },

        cmd.BlitImage(image.handle,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      image.handle,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      blit,
                      VK_FILTER_LINEAR);

        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout                     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout                     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask                 = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask                 = VK_ACCESS_SHADER_READ_BIT;

        cmd.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            VK_DEPENDENCY_BY_REGION_BIT,
                            barrier);

        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }

    barrier.subresourceRange.baseMipLevel = image.mipmap_level - 1;
    barrier.oldLayout                     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout                     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask                 = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask                 = VK_ACCESS_SHADER_READ_BIT;

    cmd.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        VK_DEPENDENCY_BY_REGION_BIT,
                        barrier);
}

void TextureCache::RegisterVideoTexture(const std::string& key,
                                        std::shared_ptr<VideoFrameDecoder> decoder) {
    // Check if already registered
    for (const auto& entry : m_video_textures) {
        if (entry.key == key) return;
    }

    // Find the existing GPU texture for this key
    if (! exists(m_tex_map, key)) {
        LOG_ERROR("video texture '%s' not found in texture cache", key.c_str());
        return;
    }
    auto& slots = m_tex_map.at(key);
    if (slots.slots.empty()) {
        LOG_ERROR("video texture '%s' has no GPU slots", key.c_str());
        return;
    }
    auto& image = slots.slots.front();

    // Verify dimensions match between GPU texture and decoder output
    if (static_cast<int>(image.extent.width) != decoder->Width() ||
        static_cast<int>(image.extent.height) != decoder->Height()) {
        LOG_ERROR("video texture '%s' dimension mismatch: GPU %ux%u vs decoder %dx%d",
                  key.c_str(), image.extent.width, image.extent.height,
                  decoder->Width(), decoder->Height());
        return;
    }

    // Allocate a persistent staging buffer for per-frame uploads
    size_t frame_size = static_cast<size_t>(image.extent.width) * image.extent.height * 4;
    VmaBufferParameters staging {};
    if (! CreateStagingBuffer(m_device.vma_allocator(), frame_size, staging)) {
        LOG_ERROR("failed to allocate video staging buffer for '%s': %zu bytes", key.c_str(), frame_size);
        return;
    }

    VideoTexEntry entry;
    entry.key = key;
    entry.decoder = std::move(decoder);
    entry.staging_buf = std::move(staging);
    entry.image = image;
    m_video_textures.push_back(std::move(entry));
    LOG_INFO("registered video texture: key=%s %ux%u staging=%zu bytes",
             key.c_str(), image.extent.width, image.extent.height, frame_size);
}

void TextureCache::UpdateAllVideoTextures(vvk::CommandBuffer& cmd) {
    for (auto& entry : m_video_textures) {
        auto frame = entry.decoder->TryGetFrame();
        if (! frame) continue;

        size_t frame_size = static_cast<size_t>(entry.image.extent.width) * entry.image.extent.height * 4;

        // Copy decoded frame to staging buffer
        void* mapped = nullptr;
        VVK_CHECK(entry.staging_buf.handle.MapMemory(&mapped));
        memcpy(mapped, frame.get(), frame_size);
        entry.staging_buf.handle.UnMapMemory();

        // Transition image to transfer destination
        VkImageSubresourceRange subresRange {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        };
        {
            VkImageMemoryBarrier barrier {
                .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext            = nullptr,
                .srcAccessMask    = VK_ACCESS_SHADER_READ_BIT,
                .dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .image            = entry.image.handle,
                .subresourceRange = subresRange,
            };
            cmd.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_DEPENDENCY_BY_REGION_BIT,
                                barrier);
        }

        // Copy staging buffer to image
        VkBufferImageCopy copy {
            .imageSubresource = VkImageSubresourceLayers {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel       = 0,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
            .imageExtent = { entry.image.extent.width, entry.image.extent.height, 1 },
        };
        cmd.CopyBufferToImage(*entry.staging_buf.handle,
                              entry.image.handle,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              copy);

        // Transition back to shader read
        {
            VkImageMemoryBarrier barrier {
                .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext            = nullptr,
                .srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask    = VK_ACCESS_SHADER_READ_BIT,
                .oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .image            = entry.image.handle,
                .subresourceRange = subresRange,
            };
            cmd.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                VK_DEPENDENCY_BY_REGION_BIT,
                                barrier);
        }
    }
}
