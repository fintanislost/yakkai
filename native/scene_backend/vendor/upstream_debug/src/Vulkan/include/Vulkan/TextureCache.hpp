#pragma once

#include "Parameters.hpp"
#include "Type.hpp"
#include "Core/NoCopyMove.hpp"
#include "Core/MapSet.hpp"
#include "vvk/vulkan_wrapper.hpp"

#include <memory>

namespace wallpaper
{

class Image;
class VideoFrameDecoder;

namespace vulkan
{

VkFormat             ToVkType(TextureFormat);
VkSamplerAddressMode ToVkType(TextureWrap);
VkFilter             ToVkType(TextureFilter);

enum class TexUsage
{
    COLOR,
    DEPTH
};

using TexHash = std::size_t;

struct TextureKey {
    i32           width;
    i32           height;
    TexUsage      usage;
    TextureFormat format;
    TextureSample sample;
    uint          mipmap_level { 1 };

    static TexHash HashValue(const TextureKey&);
};

class TextureCache : NoCopy, NoMove {
public:
    TextureCache(const Device&);
    ~TextureCache();

    void Clear();

    std::optional<ExImageParameters> CreateExTex(uint32_t witdh, uint32_t height, VkFormat,
                                                 VkImageTiling);
    ImageSlotsRef                    CreateTex(Image&);

    std::optional<ImageParameters> Query(std::string_view key, TextureKey content_hash,
                                         bool persist = false);

    void MarkShareReady(std::string_view key);
    bool DumpTexture(std::string_view key, std::string_view path);

    void RecGenerateMipmaps(vvk::CommandBuffer& cmd, const ImageParameters& image) const;

    // Video texture support — per-frame pixel updates from VideoFrameDecoder
    void RegisterVideoTexture(const std::string& key, std::shared_ptr<VideoFrameDecoder> decoder);
    void UpdateAllVideoTextures(vvk::CommandBuffer& cmd);

private:
    std::optional<VmaImageParameters> CreateTex(TextureKey);
    void                              allocateCmd();
    vvk::CommandBuffers               m_tex_cmds;
    vvk::CommandBuffer                m_tex_cmd;

    const Device&                m_device;
    Map<std::string, ImageSlots> m_tex_map;

    struct QueryTex {
        idx                index { 0 };
        bool               share_ready { false };
        bool               persist { false };
        TexHash            content_hash;
        VmaImageParameters image;
        Set<std::string>   query_keys;
    };
    std::vector<std::unique_ptr<QueryTex>> m_query_texs;
    Map<std::string, QueryTex*>            m_query_map;

    struct VideoTexEntry {
        std::string                        key;
        std::shared_ptr<VideoFrameDecoder> decoder;
        VmaBufferParameters                staging_buf {};
        ImageParameters                    image {};
    };
    std::vector<VideoTexEntry> m_video_textures;
};

} // namespace vulkan
} // namespace wallpaper
