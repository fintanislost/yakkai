#pragma once
#include "Interface/IImageParser.h"
#include "Fs/VFS.h"

#include <cstdint>
#include <string>
#include <span>
#include <unordered_map>
#include <vector>

namespace wallpaper
{

class WPTexImageParser : public IImageParser {
public:
    WPTexImageParser(fs::VFS* vfs): m_vfs(vfs) {}
    virtual ~WPTexImageParser() = default;

    std::shared_ptr<Image> Parse(const std::string&) override;
    ImageHeader            ParseHeader(const std::string&) override;
    void                   RegisterGeneratedRgbaImage(const std::string& name,
                                                      int32_t            width,
                                                      int32_t            height,
                                                      std::span<const uint8_t> rgba);

private:
    struct GeneratedRgbaImage {
        ImageHeader          header;
        std::vector<uint8_t> rgba;
    };

    fs::VFS* m_vfs;
    std::unordered_map<std::string, GeneratedRgbaImage> m_generatedRgbaImages;

    std::shared_ptr<Image> ParseRasterImage(const std::string& name);
    ImageHeader            ParseRasterHeader(const std::string& name);
    std::shared_ptr<Image> ParseGeneratedRgbaImage(const std::string& name) const;
};
} // namespace wallpaper
