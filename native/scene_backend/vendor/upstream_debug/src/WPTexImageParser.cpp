#include "WPTexImageParser.hpp"

#include "Type.hpp"
#include "WPCommon.hpp"
#include <cstdint>
#include <lz4.h>

#include "SpriteAnimation.hpp"
#include "Utils/Algorism.h"
#include "Fs/VFS.h"
#include "Utils/BitFlags.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "VideoFrameDecoder.hpp"

#include <array>
#include <cstring>
#include <iostream>

using namespace wallpaper;

enum class WPTexFlagEnum : uint32_t
{
    // true for no bilinear
    noInterpolation = 0,
    // true for no repeat
    clampUVs = 1,
    sprite   = 2,

    compo1 = 20,
    compo2 = 21,
    compo3 = 22
};
using WPTexFlags = BitFlags<WPTexFlagEnum>;

namespace
{
bool ShouldLogSleepingAronaTexture(std::string_view name) {
    return name == "ARONA_CROP_SHEET" || name == "ARONA_CROP_SHEET_channelmap";
}

const char* TextureFormatName(TextureFormat format) {
    switch (format) {
    case TextureFormat::BC1: return "BC1";
    case TextureFormat::BC2: return "BC2";
    case TextureFormat::BC3: return "BC3";
    case TextureFormat::RGB8: return "RGB8";
    case TextureFormat::RGBA8: return "RGBA8";
    case TextureFormat::RG8: return "RG8";
    case TextureFormat::R8: return "R8";
    default: return "unknown";
    }
}

const char* ImageTypeName(ImageType type) {
    switch (type) {
    case ImageType::UNKNOWN: return "UNKNOWN";
    case ImageType::BMP: return "BMP";
    case ImageType::ICO: return "ICO";
    case ImageType::JPEG: return "JPEG";
    case ImageType::JNG: return "JNG";
    case ImageType::KOALA: return "KOALA";
    case ImageType::LBM: return "LBM";
    case ImageType::MNG: return "MNG";
    case ImageType::PBM: return "PBM";
    case ImageType::PBMRAW: return "PBMRAW";
    case ImageType::PCD: return "PCD";
    case ImageType::PCX: return "PCX";
    case ImageType::PGM: return "PGM";
    case ImageType::PGMRAW: return "PGMRAW";
    case ImageType::PNG: return "PNG";
    case ImageType::PPM: return "PPM";
    case ImageType::PPMRAW: return "PPMRAW";
    case ImageType::RAS: return "RAS";
    case ImageType::TARGA: return "TARGA";
    case ImageType::TIFF: return "TIFF";
    case ImageType::WBMP: return "WBMP";
    case ImageType::PSD: return "PSD";
    case ImageType::CUT: return "CUT";
    case ImageType::XBM: return "XBM";
    case ImageType::XPM: return "XPM";
    case ImageType::DDS: return "DDS";
    case ImageType::GIF: return "GIF";
    case ImageType::HDR: return "HDR";
    case ImageType::FAXG3: return "FAXG3";
    case ImageType::SGI: return "SGI";
    case ImageType::EXR: return "EXR";
    case ImageType::J2K: return "J2K";
    case ImageType::JP2: return "JP2";
    case ImageType::PFM: return "PFM";
    case ImageType::PICT: return "PICT";
    case ImageType::RAW: return "RAW";
    default: return "unknown";
    }
}

std::string FormatUint8Stats(const char* label,
                             const uint8_t* data,
                             size_t count,
                             size_t stride,
                             size_t offset) {
    if (data == nullptr || count == 0 || stride == 0 || offset >= stride) {
        return std::string(label) + "=n/a";
    }

    uint8_t minValue = 255;
    uint8_t maxValue = 0;
    double  sum = 0.0;
    size_t  nonZeroCount = 0;
    size_t  fullCount = 0;
    for (size_t i = 0; i < count; ++i) {
        const uint8_t value = data[i * stride + offset];
        minValue = std::min(minValue, value);
        maxValue = std::max(maxValue, value);
        sum += value;
        if (value != 0) {
            nonZeroCount++;
        }
        if (value == 255) {
            fullCount++;
        }
    }

    std::ostringstream out;
    out << label
        << "={min=" << static_cast<int>(minValue)
        << " max=" << static_cast<int>(maxValue)
        << " mean=" << static_cast<int>(std::lround(sum / static_cast<double>(count)))
        << " nonZero=" << nonZeroCount
        << "/" << count
        << " full=" << fullCount
        << "/" << count
        << "}";
    return out.str();
}

void LogSleepingAronaTextureStats(const Image& img) {
    if (! ShouldLogSleepingAronaTexture(img.key) || img.slots.empty() || img.slots[0].mipmaps.empty()) {
        return;
    }

    const auto& mip = img.slots[0].mipmaps[0];
    LOG_INFO("sleeping arona texture parsed: name=%s format=%s type=%s tex=%dx%d map=%dx%d slot=%dx%d mip=%dx%d bytes=%td",
             img.key.c_str(),
             TextureFormatName(img.header.format),
             ImageTypeName(img.header.type),
             img.header.width,
             img.header.height,
             img.header.mapWidth,
             img.header.mapHeight,
             img.slots[0].width,
             img.slots[0].height,
             mip.width,
             mip.height,
             mip.size);

    const auto* bytes = mip.data.get();
    if (bytes == nullptr || mip.width <= 0 || mip.height <= 0) {
        return;
    }

    const size_t pixelCount = static_cast<size_t>(mip.width) * static_cast<size_t>(mip.height);
    std::vector<std::string> stats;
    switch (img.header.format) {
    case TextureFormat::RGBA8:
        if (mip.size >= static_cast<isize>(pixelCount * 4)) {
            stats.push_back(FormatUint8Stats("r", bytes, pixelCount, 4, 0));
            stats.push_back(FormatUint8Stats("g", bytes, pixelCount, 4, 1));
            stats.push_back(FormatUint8Stats("b", bytes, pixelCount, 4, 2));
            stats.push_back(FormatUint8Stats("a", bytes, pixelCount, 4, 3));
        }
        break;
    case TextureFormat::RG8:
        if (mip.size >= static_cast<isize>(pixelCount * 2)) {
            stats.push_back(FormatUint8Stats("r", bytes, pixelCount, 2, 0));
            stats.push_back(FormatUint8Stats("g", bytes, pixelCount, 2, 1));
        }
        break;
    case TextureFormat::R8:
        if (mip.size >= static_cast<isize>(pixelCount)) {
            stats.push_back(FormatUint8Stats("r", bytes, pixelCount, 1, 0));
        }
        break;
    default: break;
    }

    if (! stats.empty()) {
        std::ostringstream out;
        for (size_t i = 0; i < stats.size(); ++i) {
            if (i > 0) {
                out << " ";
            }
            out << stats[i];
        }
        LOG_INFO("sleeping arona texture channels: name=%s %s",
                 img.key.c_str(),
                 out.str().c_str());
    }
}

bool HasRasterExtension(std::string_view name) {
    const auto dot = name.find_last_of('.');
    if (dot == std::string_view::npos) {
        return false;
    }

    std::string ext(name.substr(dot + 1));
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ext == "png" || ext == "tga" || ext == "jpg" || ext == "jpeg" || ext == "bmp";
}

char* Lz4Decompress(const char* src, int size, int decompressed_size) {
    char* dst       = new char[(usize)decompressed_size];
    int   load_size = LZ4_decompress_safe(src, dst, size, decompressed_size);
    if (load_size < decompressed_size) {
        LOG_ERROR("lz4 decompress failed");
        delete[] dst;
        return nullptr;
    }
    return dst;
}

TextureFormat ToTexFormate(int type) {
    /*
        type
        RGBA8888 = 0,
        DXT5 = 4,
        DXT3 = 6,
        DXT1 = 7,
        RG88 = 8,
        R8 = 9,
    */
    switch (type) {
    case 0: return TextureFormat::RGBA8;
    case 4: return TextureFormat::BC3;
    case 6: return TextureFormat::BC2;
    case 5: return TextureFormat::BC7;
    case 7: return TextureFormat::BC1;
    case 8: return TextureFormat::RG8;
    case 9: return TextureFormat::R8;
    default:
        LOG_ERROR("ERROR::ToTexFormate Unkown image type: %d", type);
        return TextureFormat::RGBA8;
    }
}
void LoadHeader(fs::IBinaryStream& file, ImageHeader& header) {
    header.extraHeader["texv"].val = ReadTexVesion(file);
    header.extraHeader["texi"].val = ReadTexVesion(file);

    header.format = ToTexFormate(file.ReadInt32());
    WPTexFlags flags(file.ReadUint32());
    {
        header.isSprite     = flags[WPTexFlagEnum::sprite];
        header.sample.wrapS = header.sample.wrapT =
            flags[WPTexFlagEnum::clampUVs] ? TextureWrap::CLAMP_TO_EDGE : TextureWrap::REPEAT;
        header.sample.minFilter = header.sample.magFilter =
            flags[WPTexFlagEnum::noInterpolation] ? TextureFilter::NEAREST : TextureFilter::LINEAR;
        header.extraHeader["compo1"].val = flags[WPTexFlagEnum::compo1];
        header.extraHeader["compo2"].val = flags[WPTexFlagEnum::compo2];
        header.extraHeader["compo3"].val = flags[WPTexFlagEnum::compo3];
    }

    /*
        picture:
        width, height --> pow of 2 (tex size)
        mapw, maph    --> pic size
        mips
        mipw,miph     --> pow of 2

        sprites:
        width, height --> piece of sprite sheet
        mapw, maph    --> same
        1 mip
        mipw,mimp     --> tex size
    */

    header.width  = file.ReadInt32();
    header.height = file.ReadInt32();
    // in sprite this mean one pic
    header.mapWidth  = file.ReadInt32();
    header.mapHeight = file.ReadInt32();

    file.ReadInt32(); // unknown

    header.extraHeader["texb"].val = ReadTexVesion(file);

    header.count = file.ReadInt32();

    if (header.extraHeader["texb"].val == 3) header.type = static_cast<ImageType>(file.ReadInt32());
}

void SetHeaderPow2(ImageHeader& header, i32 mip_0_w, i32 mip_0_h) {
    header.mipmap_pow2   = algorism::IsPowOfTwo((u32)mip_0_w) || algorism::IsPowOfTwo((u32)mip_0_h);
    header.mipmap_larger = mip_0_w * mip_0_h > header.mapWidth * header.mapHeight;
}

} // namespace

std::shared_ptr<Image> WPTexImageParser::ParseRasterImage(const std::string& name) {
    auto pfile = m_vfs->Open("/assets/materials/" + name);
    if (! pfile) return nullptr;

    auto& file = *pfile;
    const isize fileSize = file.Size();
    if (fileSize <= 0) return nullptr;

    std::vector<uint8_t> encoded((usize)fileSize);
    file.Read(encoded.data(), encoded.size());

    int width = 0;
    int height = 0;
    int channels = 0;
    auto* decoded = stbi_load_from_memory(encoded.data(),
                                          static_cast<int>(encoded.size()),
                                          &width,
                                          &height,
                                          &channels,
                                          4);
    if (decoded == nullptr || width <= 0 || height <= 0) {
        if (decoded != nullptr) {
            stbi_image_free(decoded);
        }
        LOG_ERROR("parse raster image failed: %s", name.c_str());
        return nullptr;
    }

    auto img_ptr = std::make_shared<Image>();
    auto& img = *img_ptr;
    img.key = name;
    img.header.width = width;
    img.header.height = height;
    img.header.mapWidth = width;
    img.header.mapHeight = height;
    img.header.count = 1;
    img.header.type = ImageType::UNKNOWN;
    img.header.format = TextureFormat::RGBA8;
    img.header.sample.magFilter = TextureFilter::LINEAR;
    img.header.sample.minFilter = TextureFilter::LINEAR;
    img.header.sample.wrapS = TextureWrap::REPEAT;
    img.header.sample.wrapT = TextureWrap::REPEAT;

    const auto dot = name.find_last_of('.');
    if (dot != std::string::npos) {
        const std::string texBase = name.substr(0, dot);
        auto texFile = m_vfs->Open("/assets/materials/" + texBase + ".tex");
        if (texFile) {
            LoadHeader(*texFile, img.header);
            img.header.width = width;
            img.header.height = height;
            img.header.mapWidth = width;
            img.header.mapHeight = height;
            img.header.count = 1;
            img.header.type = ImageType::UNKNOWN;
            img.header.format = TextureFormat::RGBA8;
        }
    }

    img.slots.resize(1);
    auto& slot = img.slots[0];
    slot.width = width;
    slot.height = height;
    slot.mipmaps.resize(1);
    auto& mip = slot.mipmaps[0];
    mip.width = width;
    mip.height = height;
    mip.size = static_cast<isize>(width) * static_cast<isize>(height) * 4;
    mip.data = ImageDataPtr(reinterpret_cast<uint8_t*>(decoded), [](uint8_t* data) {
        stbi_image_free(data);
    });

    LogSleepingAronaTextureStats(img);
    return img_ptr;
}

ImageHeader WPTexImageParser::ParseRasterHeader(const std::string& name) {
    ImageHeader header;
    auto pfile = m_vfs->Open("/assets/materials/" + name);
    if (! pfile) return header;

    auto& file = *pfile;
    const isize fileSize = file.Size();
    if (fileSize <= 0) return header;

    std::vector<uint8_t> encoded((usize)fileSize);
    file.Read(encoded.data(), encoded.size());

    int width = 0;
    int height = 0;
    int channels = 0;
    if (! stbi_info_from_memory(encoded.data(),
                                static_cast<int>(encoded.size()),
                                &width,
                                &height,
                                &channels) ||
        width <= 0 || height <= 0) {
        LOG_ERROR("parse raster image header failed: %s", name.c_str());
        return header;
    }

    header.width = width;
    header.height = height;
    header.mapWidth = width;
    header.mapHeight = height;
    header.count = 1;
    header.type = ImageType::UNKNOWN;
    header.format = TextureFormat::RGBA8;
    header.sample.magFilter = TextureFilter::LINEAR;
    header.sample.minFilter = TextureFilter::LINEAR;
    header.sample.wrapS = TextureWrap::REPEAT;
    header.sample.wrapT = TextureWrap::REPEAT;

    const auto dot = name.find_last_of('.');
    if (dot != std::string::npos) {
        const std::string texBase = name.substr(0, dot);
        auto texFile = m_vfs->Open("/assets/materials/" + texBase + ".tex");
        if (texFile) {
            LoadHeader(*texFile, header);
            header.width = width;
            header.height = height;
            header.mapWidth = width;
            header.mapHeight = height;
            header.count = 1;
            header.type = ImageType::UNKNOWN;
            header.format = TextureFormat::RGBA8;
        }
    }

    return header;
}

std::shared_ptr<Image> WPTexImageParser::Parse(const std::string& name) {
    if (HasRasterExtension(name)) {
        return ParseRasterImage(name);
    }

    std::string            path    = "/assets/materials/" + name + ".tex";
    std::shared_ptr<Image> img_ptr = std::make_shared<Image>();
    auto&                  img     = *img_ptr;
    img.key                        = name;
    // std::ifstream file = fs::GetFileFstream(vfs, path);
    auto pfile = m_vfs->Open(path);
    if (! pfile) return nullptr;
    auto& file     = *pfile;
    auto  startpos = file.Tell();
    LoadHeader(file, img.header);

    // image
    i32 _image_count = img.header.count;
    if (_image_count < 0) return nullptr;
    usize image_count = (usize)_image_count;

    img.slots.resize(image_count);
    // TEXB v4: flat structure — {unknown(4), unknown(4), format(4), width(4), height(4),
    //           lz4_flag(4), decomp_size(4), comp_size(4), payload...}
    // TEXB v1-v3: per-slot mipmap loop — {mipmap_count, [width, height, [lz4, decomp], size, data]...}
    const bool isTexbV4 = img.header.extraHeader["texb"].val >= 4;

    for (usize i_image = 0; i_image < image_count; i_image++) {
        auto& img_slot = img.slots[i_image];
        auto& mipmaps  = img_slot.mipmaps;

        bool    LZ4_compressed    = false;
        int32_t decompressed_size = 0;
        i32     src_size          = 0;

        if (isTexbV4) {
            // TEXB v4 flat format
            file.ReadInt32(); // unknown (0xFFFFFFFF)
            file.ReadInt32(); // unknown (0)
            i32 v4_format = file.ReadInt32();
            i32 v4_width  = file.ReadInt32();
            i32 v4_height = file.ReadInt32();
            LZ4_compressed    = file.ReadInt32() == 1;
            decompressed_size = file.ReadInt32();
            src_size          = file.ReadInt32();

            mipmaps.resize(1);
            auto& mipmap  = mipmaps[0];
            mipmap.width  = v4_width;
            mipmap.height = v4_height;
            img_slot.width  = v4_width;
            img_slot.height = v4_height;
            SetHeaderPow2(img.header, v4_width, v4_height);

            // TEXB v4 format IDs differ from TEXI. When the data size doesn't
            // match the TEXI format, trust the v4 format and data size.
            // Known v4 format IDs: 1=RGBA8, 5=BC7
            {
                i32 dataBytes = LZ4_compressed ? decompressed_size : src_size;
                i32 expectedRgba = v4_width * v4_height * 4;
                if (img.header.format == TextureFormat::RGBA8 && dataBytes > 0 && dataBytes < expectedRgba) {
                    // Data is smaller than RGBA8 — must be compressed
                    if (v4_format == 5) {
                        img.header.format = TextureFormat::BC7;
                    }
                }
            }

            if (src_size <= 0 || v4_width <= 0 || v4_height <= 0)
                return nullptr;
        } else {
            // TEXB v1-v3 mipmap loop
            usize mipmap_count = (usize)std::max<i32>(file.ReadInt32(), 0);
            mipmaps.resize(mipmap_count);
        }

        usize mip_start = isTexbV4 ? 0 : 0;
        usize mip_end   = isTexbV4 ? 1 : mipmaps.size();

        for (usize i_mipmap = mip_start; i_mipmap < mip_end; i_mipmap++) {
            auto& mipmap = mipmaps.at(i_mipmap);

            if (! isTexbV4) {
                mipmap.width  = file.ReadInt32();
                mipmap.height = file.ReadInt32();
                if (i_mipmap == 0) {
                    img_slot.width  = mipmap.width;
                    img_slot.height = mipmap.height;
                    SetHeaderPow2(img.header, mipmap.width, mipmap.height);
                }

                LZ4_compressed    = false;
                decompressed_size = 0;
                if (img.header.extraHeader["texb"].val > 1) {
                    LZ4_compressed    = file.ReadInt32() == 1;
                    decompressed_size = file.ReadInt32();
                }

                src_size = file.ReadInt32();
                if (src_size <= 0 || mipmap.width <= 0 || mipmap.height <= 0 || decompressed_size < 0)
                    return nullptr;
            }

            char* result;
            result = new char[(usize)src_size];
            file.Read(result, (usize)src_size);

            // is LZ4 compress
            if (LZ4_compressed) {
                char* decompressed_char = Lz4Decompress(result, src_size, decompressed_size);
                src_size                = decompressed_size;
                if (decompressed_char != nullptr) {
                    delete[] result;
                    result = decompressed_char;
                } else {
                    LOG_ERROR("lz4 decompress failed");
                    delete[] result;
                    return nullptr;
                }
            }
            // Detect image containers (PNG/JPEG) by magic bytes, regardless of TEXB version.
            // TEXB v4 with lz4=0/decomp=0 can embed PNG/JPEG directly.
            const bool isPng = src_size >= 8 &&
                (unsigned char)result[0] == 0x89 && result[1] == 'P' && result[2] == 'N' && result[3] == 'G';
            const bool isJpeg = src_size >= 3 &&
                (unsigned char)result[0] == 0xFF && (unsigned char)result[1] == 0xD8 && (unsigned char)result[2] == 0xFF;
            const bool isImageContainer =
                (img.header.extraHeader["texb"].val == 3 && img.header.type != ImageType::UNKNOWN) ||
                isPng || isJpeg;

            if (isImageContainer) {
                int32_t w, h, n;
                auto*   data =
                    stbi_load_from_memory((const unsigned char*)result, src_size, &w, &h, &n, 4);
                if (data == nullptr) {
                    // Detect container format from magic bytes for diagnostics
                    const bool isMp4 = src_size >= 12 &&
                        result[4] == 'f' && result[5] == 't' && result[6] == 'y' && result[7] == 'p';
                    const bool isWebP = src_size >= 12 &&
                        result[0] == 'R' && result[1] == 'I' && result[2] == 'F' && result[3] == 'F' &&
                        result[8] == 'W' && result[9] == 'E' && result[10] == 'B' && result[11] == 'P';
                    LOG_ERROR("unsupported texture container for '%s': size=%d header=%dx%d format=%s",
                              img.key.c_str(), src_size, img.header.width, img.header.height,
                              isMp4 ? "MP4 video (needs video decoder)" :
                              isWebP ? "WebP (needs libwebp)" : "unknown");
                }
                mipmap.data = ImageDataPtr((uint8_t*)data, [](uint8_t* data) {
                    stbi_image_free((unsigned char*)data);
                });
                if (data != nullptr) {
                    src_size = w * h * 4;
                    img.header.format = TextureFormat::RGBA8;
                } else {
                    src_size = 0;
                }
            } else {
                // Check for video container (MP4/WebM/MKV) by magic bytes.
                // Video textures can appear in any TEXB version, including v4.
                bool videoHandled = false;
                const i32 expectedRawSize = mipmap.width * mipmap.height * 4;
                const bool sizeMismatch = src_size != expectedRawSize && src_size > expectedRawSize;
                const bool isMp4Magic = src_size >= 12 &&
                    result[4] == 'f' && result[5] == 't' && result[6] == 'y' && result[7] == 'p';
                const bool isWebMmagic = src_size >= 4 &&
                    (unsigned char)result[0] == 0x1A && result[1] == 0x45 &&
                    (unsigned char)result[2] == 0xDF && (unsigned char)result[3] == 0xA3;
                const bool hasVideoMagic = isMp4Magic || isWebMmagic;

#ifdef YAKKAI_HAS_FFMPEG
                if ((sizeMismatch || hasVideoMagic) && ! img.video_decoder) {
                    auto decoder = std::make_shared<VideoFrameDecoder>(
                        (const uint8_t*)result, src_size, mipmap.width, mipmap.height);
                    if (decoder->IsValid()) {
                        auto firstFrame = decoder->DecodeFirstFrame();
                        if (firstFrame) {
                            mipmap.width = decoder->Width();
                            mipmap.height = decoder->Height();
                            src_size = decoder->Width() * decoder->Height() * 4;
                            mipmap.data = ImageDataPtr(new uint8_t[(usize)src_size], [](uint8_t* p) { delete[] p; });
                            std::copy(firstFrame.get(), firstFrame.get() + src_size, mipmap.data.get());
                            img.header.format = TextureFormat::RGBA8;
                            // Large videos (>= 1920 wide) are likely the main
                            // wallpaper content — enable continuous playback.
                            // Small videos are overlays — use static first frame
                            // to avoid CPU overhead.
                            const bool isMainVideo = decoder->Width() >= 1920;
                            if (isMainVideo) {
                                img.video_decoder = decoder;
                                decoder->Start();
                                LOG_INFO("video texture decoded (playback): name=%s %dx%d", img.key.c_str(), mipmap.width, mipmap.height);
                            } else {
                                LOG_INFO("video texture decoded (static first frame): name=%s %dx%d", img.key.c_str(), mipmap.width, mipmap.height);
                            }
                            videoHandled = true;
                        } else {
                            LOG_ERROR("video texture '%s': FFmpeg opened but first frame decode failed", img.key.c_str());
                        }
                    }
                }
#else
                if (sizeMismatch) {
                    LOG_ERROR("texture '%s': data size %d doesn't match %dx%d RGBA (%d bytes), FFmpeg not available",
                              img.key.c_str(), src_size, mipmap.width, mipmap.height, expectedRawSize);
                }
#endif

                if (! videoHandled) {
                    mipmap.data = ImageDataPtr(new uint8_t[(usize)src_size], [](uint8_t* data) {
                        delete[] data;
                    });
                    std::copy(result, result + src_size, mipmap.data.get());
                }
            }
            mipmap.size = src_size * (i32)sizeof(uint8_t);
            delete[] result;
        }
    }
    LogSleepingAronaTextureStats(img);
    return img_ptr;
}

ImageHeader WPTexImageParser::ParseHeader(const std::string& name) {
    if (HasRasterExtension(name)) {
        return ParseRasterHeader(name);
    }

    ImageHeader header;
    std::string path  = "/assets/materials/" + name + ".tex";
    auto        pfile = m_vfs->Open(path);
    if (! pfile) return header;
    auto& file = *pfile;

    LoadHeader(file, header);
    if (header.count < 0) return header;

    usize image_count = (usize)header.count;

    // load sprite info
    if (header.isSprite) {
        // bypass image data, store width and height
        std::vector<std::vector<float>> imageDatas(image_count);
        for (usize i_image = 0; i_image < image_count; i_image++) {
            int mipmap_count = file.ReadInt32();
            for (int32_t i_mipmap = 0; i_mipmap < mipmap_count; i_mipmap++) {
                int32_t width  = file.ReadInt32();
                int32_t height = file.ReadInt32();
                if (i_mipmap == 0) {
                    imageDatas.at(i_image) = { (float)width, (float)height };
                    header.mipmap_pow2     = algorism::IsPowOfTwo((u32)(width * height));
                }
                if (header.extraHeader["texb"].val > 1) {
                    int32_t LZ4_compressed    = file.ReadInt32();
                    int32_t decompressed_size = file.ReadInt32();
                    (void)LZ4_compressed;
                    (void)decompressed_size;
                }
                long src_size = file.ReadInt32();
                file.SeekCur(src_size);
            }
        }
        // sprite pos
        int32_t texs       = ReadTexVesion(file);
        int32_t framecount = file.ReadInt32();
        if (texs > 3) {
            LOG_ERROR("Unkown texs version");
        }
        if (texs == 3) {
            i32 width  = file.ReadInt32();
            i32 height = file.ReadInt32();
            (void)width;
            (void)height;
        }

        for (int32_t i = 0; i < framecount; i++) {
            SpriteFrame sf;
            sf.imageId = file.ReadInt32();
            if (sf.imageId < 0) {
                LOG_ERROR("get neg imageid");
            }
            float spriteWidth  = imageDatas.at((usize)sf.imageId)[0];
            float spriteHeight = imageDatas.at((usize)sf.imageId)[1];

            sf.frametime = file.ReadFloat();
            if (texs == 1) {
                sf.x        = (float)file.ReadInt32() / spriteWidth;
                sf.y        = (float)file.ReadInt32() / spriteHeight;
                sf.xAxis[0] = (float)file.ReadInt32();
                sf.xAxis[1] = (float)file.ReadInt32();
                sf.yAxis[0] = (float)file.ReadInt32();
                sf.yAxis[1] = (float)file.ReadInt32();
            } else {
                sf.x        = file.ReadFloat() / spriteWidth;
                sf.y        = file.ReadFloat() / spriteHeight;
                sf.xAxis[0] = file.ReadFloat();
                sf.xAxis[1] = file.ReadFloat();
                sf.yAxis[0] = file.ReadFloat();
                sf.yAxis[1] = file.ReadFloat();
            }
            sf.width  = (float)std::sqrt(std::pow(sf.xAxis[0], 2) + std::pow(sf.xAxis[1], 2));
            sf.height = (float)std::sqrt(std::pow(sf.yAxis[0], 2) + std::pow(sf.yAxis[1], 2));
            sf.xAxis[0] /= spriteWidth;
            sf.xAxis[1] /= spriteWidth;
            sf.yAxis[0] /= spriteHeight;
            sf.yAxis[1] /= spriteHeight;
            sf.rate = sf.height / sf.width;
            header.spriteAnim.AppendFrame(sf);
        }
    } else {
        i32 mipmap_count = file.ReadInt32();
        (void)mipmap_count;
        i32 width  = file.ReadInt32();
        i32 height = file.ReadInt32();
        SetHeaderPow2(header, width, height);
    }
    return header;
}
