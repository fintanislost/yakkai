#pragma once
#include "Interface/IImageParser.h"
#include "Fs/VFS.h"

namespace wallpaper
{

class WPTexImageParser : public IImageParser {
public:
    WPTexImageParser(fs::VFS* vfs): m_vfs(vfs) {}
    virtual ~WPTexImageParser() = default;

    std::shared_ptr<Image> Parse(const std::string&) override;
    ImageHeader            ParseHeader(const std::string&) override;

private:
    fs::VFS* m_vfs;

    std::shared_ptr<Image> ParseRasterImage(const std::string& name);
    ImageHeader            ParseRasterHeader(const std::string& name);
};
} // namespace wallpaper
