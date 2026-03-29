#include "WPMdlParser.hpp"
#include "Fs/VFS.h"
#include "Fs/IBinaryStream.h"
#include "Fs/MemBinaryStream.h"
#include "WPCommon.hpp"
#include "Utils/Logging.h"
#include "Scene/SceneMesh.h"
#include "SpecTexs.hpp"
#include "wpscene/WPMaterial.h"
#include "WPShaderParser.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace wallpaper;

namespace
{
std::array<float, 2> DecodePackedUnorm16x2(uint32_t packed) {
    const auto decode = [](uint16_t value) {
        return static_cast<float>(value) / 65535.0f;
    };

    return {
        decode(static_cast<uint16_t>(packed & 0xffffu)),
        decode(static_cast<uint16_t>((packed >> 16) & 0xffffu)),
    };
}

WPPuppet::PlayMode ToPlayMode(std::string_view m) {
    if (m == "loop" || m.empty()) return WPPuppet::PlayMode::Loop;
    if (m == "mirror") return WPPuppet::PlayMode::Mirror;
    if (m == "single") return WPPuppet::PlayMode::Single;

    LOG_ERROR("unknown puppet animation play mode \"%s\"", m.data());
    return WPPuppet::PlayMode::Loop;
}
} // namespace

// bytes * size
constexpr uint32_t singile_vertex  = 4 * (3 + 4 + 4 + 2);
constexpr uint32_t singile_indices = 2 * 3;
constexpr uint32_t std_format_vertex_size_herald_value = 0x01800009;

// number of bytes in an MDAT attachment after the attachment name
constexpr uint32_t mdat_attachment_data_byte_length = 64;

// alternative consts for alternative mdl format
constexpr uint32_t alt_singile_vertex = 4 * (3 + 4 + 4 + 2 + 7);
constexpr uint32_t alt_format_vertex_size_herald_value = 0x0180000F;

// static model format observed in Wallpaper Engine MDLV0014 assets such as
// Arsenal's pistols.mdl:
// position.xyz, normal.xyz, tangent.xyzw, uv0.xy, uv1.xy
constexpr uint32_t static_singile_vertex = 4 * (3 + 3 + 4 + 2 + 2);

constexpr uint32_t singile_bone_frame = 4 * 9;

namespace
{
enum class MdlVertexFormat
{
    StandardSkinned,
    AlternativeSkinned,
    Static56,
};

uint32_t VertexStride(MdlVertexFormat format) {
    switch (format) {
        case MdlVertexFormat::StandardSkinned:
            return singile_vertex;
        case MdlVertexFormat::AlternativeSkinned:
            return alt_singile_vertex;
        case MdlVertexFormat::Static56:
            return static_singile_vertex;
    }
    return singile_vertex;
}

bool ReadStatic56VertexBlock(fs::IBinaryStream& f, uint32_t vertex_size,
                             std::vector<WPMdl::Vertex>& vertexs) {
    if (vertex_size % static_singile_vertex != 0) {
        LOG_ERROR("unsupport mdl static vertex size %d", vertex_size);
        return false;
    }

    const uint32_t vertex_num = vertex_size / static_singile_vertex;
    vertexs.resize(vertex_num);
    for (auto& vert : vertexs) {
        for (auto& v : vert.position) v = f.ReadFloat();
        for (auto& v : vert.normal) v = f.ReadFloat();
        for (auto& v : vert.tangent) v = f.ReadFloat();
        for (auto& v : vert.texcoord) v = f.ReadFloat(); // uv0.xy
        for (auto& v : vert.texcoord2) v = f.ReadFloat(); // uv1.xy

        vert.blend_indices = { 0, 0, 0, 0 };
        vert.weight        = { 1.0f, 0.0f, 0.0f, 0.0f };
    }
    return true;
}

bool ReadIndicesBlock(fs::IBinaryStream& f, std::vector<std::array<uint16_t, 3>>& indices) {
    const uint32_t indices_size = f.ReadUint32();
    if (indices_size % singile_indices != 0) {
        LOG_ERROR("unsupport mdl indices size %d", indices_size);
        return false;
    }

    const uint32_t indices_num = indices_size / singile_indices;
    indices.resize(indices_num);
    for (auto& id : indices) {
        for (auto& v : id) v = f.ReadUint16();
    }
    return true;
}

idx FindSectionOffset(fs::IBinaryStream&  f,
                      std::string_view    prefix,
                      idx                 start,
                      int32_t&            version) {
    version = 0;

    if (f.Size() < 9) {
        return -1;
    }

    const idx last = f.Size() - 9;
    if (start < 0) {
        start = 0;
    }

    for (idx pos = start; pos <= last; ++pos) {
        if (! f.SeekSet(pos)) {
            break;
        }

        const int32_t parsedVersion = ReadVersion(prefix, f);
        if (parsedVersion != 0) {
            version = parsedVersion;
            return pos;
        }
    }

    return -1;
}

bool SeekSection(fs::IBinaryStream& f, std::string_view prefix, int32_t& version) {
    const idx start = f.Tell();
    const idx pos   = FindSectionOffset(f, prefix, start, version);
    if (pos < 0) {
        f.SeekSet(start);
        return false;
    }
    if (pos != start) {
        LOG_INFO("mdl resynced %.*s section from %td to %td",
                 static_cast<int>(prefix.size()),
                 prefix.data(),
                 start,
                 pos);
    }
    f.SeekSet(pos + 9);
    return true;
}

bool SeekNextModelSection(fs::IBinaryStream& f,
                          idx                start,
                          std::string&       type,
                          int32_t&           version) {
    int32_t mdatVersion = 0;
    int32_t mdlaVersion = 0;
    const idx mdatPos = FindSectionOffset(f, "MDAT", start, mdatVersion);
    const idx mdlaPos = FindSectionOffset(f, "MDLA", start, mdlaVersion);

    if (mdatPos < 0 && mdlaPos < 0) {
        return false;
    }

    bool useMdat = false;
    idx  pos     = -1;
    if (mdatPos >= 0 && (mdlaPos < 0 || mdatPos < mdlaPos)) {
        useMdat = true;
        pos     = mdatPos;
        version = mdatVersion;
        type    = "MDAT";
    } else {
        pos     = mdlaPos;
        version = mdlaVersion;
        type    = "MDLA";
    }

    if (pos != start) {
        LOG_INFO("mdl resynced %s section from %td to %td", type.c_str(), start, pos);
    }
    f.SeekSet(pos + 9);
    return true;
}

bool ReadSectionAsciiString(fs::IBinaryStream& f,
                            idx                section_end,
                            std::string&       out,
                            size_t             max_len = 128) {
    out.clear();
    while (f.Tell() < section_end) {
        char c = '\0';
        if (f.Read(&c, 1) != 1) {
            return false;
        }
        if (c == '\0') {
            return true;
        }
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc > 0x7e) {
            return false;
        }
        out.push_back(c);
        if (out.size() > max_len) {
            return false;
        }
    }
    return false;
}

bool IsLikelyPuppetAnimationHeader(fs::IBinaryStream& f,
                                   idx                pos,
                                   idx                section_end,
                                   uint32_t           bone_count) {
    const idx saved = f.Tell();
    auto restore = [&]() {
        f.SeekSet(saved);
    };

    constexpr idx min_header_size = 4 + 4 + 2 + 2 + 4 + 4 + 4 + 4;
    if (pos < 0 || pos + min_header_size > section_end || ! f.SeekSet(pos)) {
        restore();
        return false;
    }

    const int32_t anim_id = f.ReadInt32();
    if (anim_id <= 0 || anim_id > 1000000) {
        restore();
        return false;
    }

    const int32_t header_unk = f.ReadInt32();
    if (header_unk < 0 || header_unk > 1024) {
        restore();
        return false;
    }

    std::string name;
    if (! ReadSectionAsciiString(f, section_end, name) || name.empty()) {
        restore();
        return false;
    }

    std::string mode;
    if (! ReadSectionAsciiString(f, section_end, mode, 16)
        || (mode != "loop" && mode != "mirror" && mode != "single" && ! mode.empty())) {
        restore();
        return false;
    }

    const float fps = f.ReadFloat();
    if (! std::isfinite(fps) || fps <= 0.0f || fps > 240.0f) {
        restore();
        return false;
    }

    const int32_t length = f.ReadInt32();
    if (length <= 0 || length > 100000) {
        restore();
        return false;
    }

    const int32_t extra_unk = f.ReadInt32();
    if (extra_unk < 0 || extra_unk > 1024) {
        restore();
        return false;
    }

    const uint32_t block_count = f.ReadUint32();
    const uint32_t max_reasonable_blocks = std::max<uint32_t>(bone_count * 4, 64);
    if (block_count == 0 || block_count > max_reasonable_blocks) {
        restore();
        return false;
    }

    for (uint32_t i = 0; i < block_count; ++i) {
        if (f.Tell() + 8 > section_end) {
            restore();
            return false;
        }

        const int32_t bone_id = f.ReadInt32();
        if (bone_id < 0 || static_cast<uint32_t>(bone_id) > max_reasonable_blocks) {
            restore();
            return false;
        }

        const uint32_t byte_size = f.ReadUint32();
        if (byte_size == 0 || byte_size % singile_bone_frame != 0) {
            restore();
            return false;
        }

        const uint32_t frame_count = byte_size / singile_bone_frame;
        if (frame_count < static_cast<uint32_t>(length) || frame_count > static_cast<uint32_t>(length + 2)) {
            restore();
            return false;
        }

        if (f.Tell() + static_cast<idx>(byte_size) > section_end || ! f.SeekCur(byte_size)) {
            restore();
            return false;
        }
    }

    restore();
    return true;
}

bool SeekNextPuppetAnimationHeader(fs::IBinaryStream& f,
                                   idx                start,
                                   idx                section_end,
                                   uint32_t           bone_count) {
    if (section_end <= start) {
        return false;
    }

    for (idx pos = start; pos < section_end; ++pos) {
        if (! IsLikelyPuppetAnimationHeader(f, pos, section_end, bone_count)) {
            continue;
        }

        if (pos != start) {
            LOG_INFO("mdl resynced MDLA animation header from %td to %td", start, pos);
        }
        return f.SeekSet(pos);
    }
    return false;
}
} // namespace

bool WPMdlParser::Parse(std::string_view path, fs::VFS& vfs, WPMdl& mdl) {
    auto str_path = std::string(path);
    auto pfile    = vfs.Open("/assets/" + str_path);
    if (! pfile) {
        LOG_ERROR("mdl file not found: %s", str_path.c_str());
        return false;
    }
    auto memfile  = fs::MemBinaryStream(*pfile);
    auto& f = memfile;

    mdl.mdlv = ReadMDLVesion(f);

    int32_t mdl_flag = f.ReadInt32();
    if (mdl_flag == 9) {
        LOG_INFO("puppet '%s' is not complete, ignore", str_path.c_str());
        return false;
    };
    f.ReadInt32(); // unk, 1
    f.ReadInt32(); // unk, 1

    mdl.mat_json_file = f.ReadStr();
    // 0    
    f.ReadInt32();

    bool alt_mdl_format = false;
    uint32_t curr = f.ReadUint32();

    // if the uint at the normal vertex size position is 0, then this file
    // uses the alternative MDL format, therefore the actual vertex size is
    // located after the herald value, and we'll need to account for other differences later on.
    if(curr == 0){
        alt_mdl_format = true;
        while (curr != alt_format_vertex_size_herald_value){
            curr = f.ReadUint32();
        }
        curr = f.ReadUint32();
    }
    else if(curr == std_format_vertex_size_herald_value){
        curr = f.ReadUint32();
    }

    uint32_t        vertex_size = curr;
    MdlVertexFormat vertex_format =
      alt_mdl_format ? MdlVertexFormat::AlternativeSkinned : MdlVertexFormat::StandardSkinned;
    uint32_t vertex_stride = VertexStride(vertex_format);

    if (vertex_size % vertex_stride != 0) {
        if (! alt_mdl_format && vertex_size % static_singile_vertex == 0) {
            vertex_format = MdlVertexFormat::Static56;
            vertex_stride = static_singile_vertex;
            LOG_INFO("mdl using static 56-byte vertex layout: %s", str_path.c_str());
        }
    }

    if (vertex_size % vertex_stride != 0) {
        LOG_ERROR("unsupport mdl vertex size %d", vertex_size);
        return false;
    }

    if (vertex_format == MdlVertexFormat::Static56) {
        WPMdl::Submesh firstSubmesh;
        firstSubmesh.mat_json_file = mdl.mat_json_file;
        if (! ReadStatic56VertexBlock(f, vertex_size, firstSubmesh.vertexs)) {
            return false;
        }
        if (! ReadIndicesBlock(f, firstSubmesh.indices)) {
            return false;
        }

        mdl.submeshes.push_back(firstSubmesh);
        while (f.Tell() < f.Size()) {
            const std::string submeshMaterial = f.ReadStr();
            if (submeshMaterial.empty() || submeshMaterial.rfind("materials/", 0) != 0) {
                break;
            }

            f.ReadInt32(); // usually 0
            const uint32_t submeshVertexSize = f.ReadUint32();

            WPMdl::Submesh submesh;
            submesh.mat_json_file = submeshMaterial;
            if (! ReadStatic56VertexBlock(f, submeshVertexSize, submesh.vertexs)) {
                return false;
            }
            if (! ReadIndicesBlock(f, submesh.indices)) {
                return false;
            }
            mdl.submeshes.push_back(std::move(submesh));
        }

        mdl.vertexs      = mdl.submeshes.front().vertexs;
        mdl.indices      = mdl.submeshes.front().indices;
        mdl.mat_json_file = mdl.submeshes.front().mat_json_file;
        mdl.puppet = std::make_shared<WPPuppet>();
        mdl.puppet->prepared();

        size_t totalVertices = 0;
        size_t totalIndices  = 0;
        for (const auto& submesh : mdl.submeshes) {
            totalVertices += submesh.vertexs.size();
            totalIndices += submesh.indices.size();
        }

        LOG_INFO("read static mdl fallback: mdlv: %d, submeshes: %zu, vertexes: %zu, indices: %zu, first material: %s",
                 mdl.mdlv,
                 mdl.submeshes.size(),
                 totalVertices,
                 totalIndices,
                 mdl.mat_json_file.c_str());
        return true;
    }

    // if using the alternative MDL format, vertexes contain 7 extra 32-bit chunks between
    // position and blend indices.
    uint32_t vertex_num = vertex_size / vertex_stride;
    mdl.vertexs.resize(vertex_num);
    for (auto& vert : mdl.vertexs) {
        for (auto& v : vert.position) v = f.ReadFloat();
        if (vertex_format == MdlVertexFormat::AlternativeSkinned) {
            f.ReadFloat();
            f.ReadFloat();
            f.ReadFloat();
            f.ReadFloat();
            vert.channelmap_texcoord = DecodePackedUnorm16x2(f.ReadUint32());
            f.ReadUint32();
            f.ReadUint32();
        }
        for (auto& v : vert.blend_indices) v = f.ReadUint32();
        for (auto& v : vert.weight) v = f.ReadFloat();
        for (auto& v : vert.texcoord) v = f.ReadFloat();
    }

    if (! ReadIndicesBlock(f, mdl.indices)) {
        return false;
    }

    if (! SeekSection(f, "MDLS", mdl.mdls)) {
        LOG_ERROR("mdl missing MDLS section: %s", str_path.c_str());
        return false;
    }

    size_t bones_file_end = f.ReadUint32();
    (void)bones_file_end;

    uint16_t bones_num = f.ReadUint16();
    // 1 byte
    f.ReadUint16(); // unk

    mdl.puppet  = std::make_shared<WPPuppet>();
    auto& bones = mdl.puppet->bones;
    auto& anims = mdl.puppet->anims;

    bones.resize(bones_num);
    for (uint i = 0; i < bones_num; i++) {
        auto&       bone = bones[i];
        std::string name = f.ReadStr();
        f.ReadInt32(); // unk

        bone.parent = f.ReadUint32();
        if (bone.parent >= i && ! bone.noParent()) {
            LOG_ERROR("mdl wrong bone parent index %d for bone %u in %s",
                      bone.parent,
                      i,
                      str_path.c_str());
            bone.parent = 0xFFFFFFFFu;
        }

        uint32_t size = f.ReadUint32();
        if (size != 64) {
            LOG_ERROR("mdl unsupport bones size: %d", size);
            return false;
        }
        for (auto row : bone.transform.matrix().colwise()) {
            for (auto& x : row) x = f.ReadFloat();
        }

        std::string bone_simulation_json = f.ReadStr();
        /*
        auto trans = bone.transform.translation();
        LOG_INFO("trans: %f %f %f", trans[0], trans[1], trans[2]);
        */
    }

    if (mdl.mdls > 1) {
        int16_t unk = f.ReadInt16();
        if (unk != 0) {
            LOG_INFO("puppet: one unk is not 0, may be wrong");
        }

        uint8_t has_trans = f.ReadUint8();
        if (has_trans) {
            for (uint i = 0; i < bones_num; i++)
                for (uint j = 0; j < 16; j++) f.ReadFloat(); // mat
        }
        uint32_t size_unk = f.ReadUint32();
        for (uint i = 0; i < size_unk; i++)
            for (int j = 0; j < 3; j++) f.ReadUint32();

        f.ReadUint32(); // unk

        uint8_t has_offset_trans = f.ReadUint8();
        if (has_offset_trans) {
            for (uint i = 0; i < bones_num; i++) {
                for (uint j = 0; j < 3; j++) f.ReadFloat();  // like pos
                for (uint j = 0; j < 16; j++) f.ReadFloat(); // mat
            }
        }

        uint8_t has_index = f.ReadUint8();
        if (has_index) {
            for (uint i = 0; i < bones_num; i++) {
                f.ReadUint32();
            }
        }
    }

    // there can be zero padding or unknown blocks between MDLS and MDLA. Walk
    // forward to the next real section marker instead of assuming contiguous layout.
    std::string mdType;
    int32_t     mdVersion = 0;
    idx         sectionSearchStart = f.Tell();
    while (SeekNextModelSection(f, sectionSearchStart, mdType, mdVersion)) {
        if (mdType == "MDAT") {
            f.ReadUint32(); // skip 4 bytes
            uint32_t num_attachments = f.ReadUint16(); // number of attachments in the MDAT section

            for (uint32_t i = 0; i < num_attachments; i++) {
                f.ReadUint16(); // skip 2 bytes
                std::string attachment_name = f.ReadStr(); // attachment name
                (void)attachment_name;
                for (uint32_t j = 0; j < mdat_attachment_data_byte_length; j++) {
                    f.ReadUint8();
                }
            }
            sectionSearchStart = f.Tell();
            continue;
        }

        if (mdType == "MDLA") {
            mdl.mdla = mdVersion;
        }
        break;
    }

    LOG_INFO("mdl parse: mdType=%s mdVersion=%d pos=%td fileSize=%td bones=%u",
             mdType.c_str(), mdVersion, f.Tell(), f.Size(), bones_num);

    if (mdType == "MDLA" && mdl.mdla > 0) {
        if (mdl.mdla != 0) {
            uint end_size = f.ReadUint32();
            idx mdla_section_end = f.Size();
            if (end_size > 0 && end_size < static_cast<uint>(f.Size()) && end_size > static_cast<uint>(f.Tell())) {
                mdla_section_end = static_cast<idx>(end_size + 1);
            }

            uint anim_num = f.ReadUint32();
            LOG_INFO("mdl MDLA: version=%d end_size=%u section_end=%td anim_num=%u pos=%td",
                     mdl.mdla, end_size, mdla_section_end, anim_num, f.Tell());
            anims.resize(anim_num);
            for (auto& anim : anims) {
                if (! SeekNextPuppetAnimationHeader(f, f.Tell(), mdla_section_end, bones_num)) {
                    LOG_ERROR("failed to locate puppet animation header starting from %td (section_end=%td bones=%u)",
                              f.Tell(), mdla_section_end, bones_num);
                    return false;
                }

                anim.id = f.ReadInt32();
                f.ReadInt32();
                anim.name   = f.ReadStr();
                if(anim.name.empty()){
                    anim.name = f.ReadStr();
                }
                anim.mode   = ToPlayMode(f.ReadStr());
                anim.fps    = f.ReadFloat();
                anim.length = f.ReadInt32();
                f.ReadInt32();

                uint32_t b_num = f.ReadUint32();
                anim.bframes_array.resize(b_num);
                for (auto& bframes : anim.bframes_array) {
                    f.ReadInt32();
                    uint32_t byte_size = f.ReadUint32();
                    uint32_t num       = byte_size / singile_bone_frame;
                    if (byte_size % singile_bone_frame != 0) {
                        LOG_ERROR("wrong bone frame size %d", byte_size);
                        return false;
                    }
                    bframes.frames.resize(num);
                    for (auto& frame : bframes.frames) {
                        for (auto& v : frame.position) v = f.ReadFloat();
                        for (auto& v : frame.angle) v = f.ReadFloat();
                        for (auto& v : frame.scale) v = f.ReadFloat();
                    }
                }
            }
        }
    }
    
    mdl.puppet->prepared();

    LOG_INFO("read puppet: mdlv: %d, nmdls: %d, mdla: %d, bones: %d, anims: %d",
             mdl.mdlv,
             mdl.mdls,
             mdl.mdla,
             mdl.puppet->bones.size(),
             mdl.puppet->anims.size());
    return true;
}

void WPMdlParser::GenPuppetMesh(SceneMesh& mesh,
                                const WPMdl::Submesh& mdl,
                                const Eigen::Matrix3f& basis) {
    SceneVertexArray vertex({ { WE_IN_POSITION.data(), VertexType::FLOAT3 },
                              { WE_IN_BLENDINDICES.data(), VertexType::UINT4 },
                              { WE_IN_BLENDWEIGHTS.data(), VertexType::FLOAT4 },
                              { WE_IN_TEXCOORD.data(), VertexType::FLOAT2 } },
                            mdl.vertexs.size());

    Eigen::Matrix3f basisLinear = basis;
    if (! basisLinear.allFinite()) {
        basisLinear = Eigen::Matrix3f::Identity();
    }

    std::array<float, 16> one_vert;
    auto                  to_one = [](const WPMdl::Vertex& in, decltype(one_vert)& out) {
        uint offset = 0;
        memcpy(out.data() + 4 * (offset++), in.position.data(), sizeof(in.position));
        memcpy(out.data() + 4 * (offset++), in.blend_indices.data(), sizeof(in.blend_indices));
        memcpy(out.data() + 4 * (offset++), in.weight.data(), sizeof(in.weight));
        memcpy(out.data() + 4 * (offset++), in.texcoord.data(), sizeof(in.texcoord));
    };
    for (uint i = 0; i < mdl.vertexs.size(); i++) {
        auto  v = mdl.vertexs[i];
        const Eigen::Vector3f position = basisLinear * Eigen::Vector3f(v.position.data());
        std::copy_n(position.data(), 3, v.position.begin());
        to_one(v, one_vert);
        vertex.SetVertexs(i, one_vert);
    }
    std::vector<uint32_t> indices;
    size_t                u16_count = mdl.indices.size() * 3;
    indices.resize(u16_count / 2 + 1);
    memcpy(indices.data(), mdl.indices.data(), u16_count * sizeof(uint16_t));

    mesh.AddVertexArray(std::move(vertex));
    mesh.AddIndexArray(SceneIndexArray(indices));
}

void WPMdlParser::GenPuppetMesh(SceneMesh& mesh, const WPMdl& mdl) {
    GenPuppetMesh(mesh, WPMdl::Submesh { mdl.mat_json_file, mdl.vertexs, mdl.indices }, Eigen::Matrix3f::Identity());
}

bool WPMdlParser::GenPuppetMesh(SceneMesh&                mesh,
                                const WPMdl&              mdl,
                                std::span<const uint32_t> activePrimaryBlendSlots,
                                bool                      includeFullyActiveTriangles) {
    auto buildFilteredSubmesh = [&](WPMdl::Submesh& outSubmesh) {
        if (activePrimaryBlendSlots.empty()) {
            return false;
        }

        std::array<bool, 64> activeSlotMask {};
        for (const uint32_t slot : activePrimaryBlendSlots) {
            activeSlotMask[std::min<size_t>(slot, activeSlotMask.size() - 1)] = true;
        }

        std::vector<uint32_t> remappedVertexIndices(mdl.vertexs.size(), std::numeric_limits<uint32_t>::max());
        std::vector<WPMdl::Vertex> filteredVertices;
        std::vector<std::array<uint16_t, 3>> filteredIndices;

        auto hasMeaningfulActiveWeight = [&activeSlotMask](const WPMdl::Vertex& vertex) {
            constexpr float kMinActiveBlendWeight = 0.5f;
            float           activeWeight = 0.0f;
            for (size_t i = 0; i < vertex.blend_indices.size() && i < vertex.weight.size(); ++i) {
                const size_t slot = std::min<size_t>(vertex.blend_indices[i], activeSlotMask.size() - 1);
                if (! activeSlotMask[slot]) {
                    continue;
                }
                activeWeight += std::max(0.0f, vertex.weight[i]);
            }
            return activeWeight >= kMinActiveBlendWeight;
        };

        for (const auto& tri : mdl.indices) {
            size_t activeVertexCount = 0;
            for (const uint16_t vertexIndex : tri) {
                if (vertexIndex < mdl.vertexs.size() &&
                    hasMeaningfulActiveWeight(mdl.vertexs[vertexIndex])) {
                    activeVertexCount++;
                }
            }
            const bool isOverlayTriangle = activeVertexCount >= 2;
            const bool keepTriangle = includeFullyActiveTriangles ? isOverlayTriangle : ! isOverlayTriangle;
            if (! keepTriangle) {
                continue;
            }

            std::array<uint16_t, 3> remappedTri {};
            bool                    triValid = true;
            for (size_t corner = 0; corner < tri.size(); ++corner) {
                const uint16_t sourceIndex = tri[corner];
                if (sourceIndex >= mdl.vertexs.size()) {
                    triValid = false;
                    break;
                }

                uint32_t& remappedIndex = remappedVertexIndices[sourceIndex];
                if (remappedIndex == std::numeric_limits<uint32_t>::max()) {
                    remappedIndex = static_cast<uint32_t>(filteredVertices.size());
                    filteredVertices.push_back(mdl.vertexs[sourceIndex]);
                }
                if (remappedIndex > std::numeric_limits<uint16_t>::max()) {
                    triValid = false;
                    break;
                }
                remappedTri[corner] = static_cast<uint16_t>(remappedIndex);
            }

            if (triValid) {
                filteredIndices.push_back(remappedTri);
            }
        }

        if (filteredVertices.empty() || filteredIndices.empty()) {
            return false;
        }

        outSubmesh =
            WPMdl::Submesh { mdl.mat_json_file, std::move(filteredVertices), std::move(filteredIndices) };
        return true;
    };

    WPMdl::Submesh filteredSubmesh;
    if (! buildFilteredSubmesh(filteredSubmesh)) {
        GenPuppetMesh(mesh, mdl);
        return activePrimaryBlendSlots.empty();
    }

    GenPuppetMesh(mesh, filteredSubmesh, Eigen::Matrix3f::Identity());
    return true;
}

void WPMdlParser::GenPuppetImageSpaceMesh(SceneMesh&                  mesh,
                                          const WPMdl&                mdl,
                                          const std::array<float, 2>& imageSize) {
    SceneVertexArray vertex({ { WE_IN_POSITION.data(), VertexType::FLOAT3 },
                              { WE_IN_BLENDINDICES.data(), VertexType::UINT4 },
                              { WE_IN_BLENDWEIGHTS.data(), VertexType::FLOAT4 },
                              { WE_IN_TEXCOORD.data(), VertexType::FLOAT2 } },
                            mdl.vertexs.size());

    const float width = std::max(imageSize[0], 1.0f);
    const float height = std::max(imageSize[1], 1.0f);

    std::array<float, 16> one_vert {};
    for (uint i = 0; i < mdl.vertexs.size(); i++) {
        auto v = mdl.vertexs[i];

        const std::array<float, 2> image_texcoord {
            (v.position[0] + width * 0.5f) / width,
            1.0f - ((v.position[1] + height * 0.5f) / height),
        };

        std::fill(one_vert.begin(), one_vert.end(), 0.0f);
        memcpy(one_vert.data() + 4 * 0, v.position.data(), sizeof(v.position));
        memcpy(one_vert.data() + 4 * 1, v.blend_indices.data(), sizeof(v.blend_indices));
        memcpy(one_vert.data() + 4 * 2, v.weight.data(), sizeof(v.weight));
        memcpy(one_vert.data() + 4 * 3, image_texcoord.data(), sizeof(image_texcoord));
        vertex.SetVertexs(i, one_vert);
    }

    std::vector<uint32_t> indices;
    size_t                u16_count = mdl.indices.size() * 3;
    indices.resize(u16_count / 2 + 1);
    memcpy(indices.data(), mdl.indices.data(), u16_count * sizeof(uint16_t));

    mesh.AddVertexArray(std::move(vertex));
    mesh.AddIndexArray(SceneIndexArray(indices));
}

bool WPMdlParser::GenPuppetImageSpaceMesh(SceneMesh&                  mesh,
                                          const WPMdl&                mdl,
                                          const std::array<float, 2>& imageSize,
                                          std::span<const uint32_t>   activePrimaryBlendSlots,
                                          bool                        includeFullyActiveTriangles) {
    auto buildFilteredSubmesh = [&](WPMdl::Submesh& outSubmesh) {
        if (activePrimaryBlendSlots.empty()) {
            return false;
        }

        std::array<bool, 64> activeSlotMask {};
        for (const uint32_t slot : activePrimaryBlendSlots) {
            activeSlotMask[std::min<size_t>(slot, activeSlotMask.size() - 1)] = true;
        }

        std::vector<uint32_t> remappedVertexIndices(mdl.vertexs.size(), std::numeric_limits<uint32_t>::max());
        std::vector<WPMdl::Vertex> filteredVertices;
        std::vector<std::array<uint16_t, 3>> filteredIndices;

        auto hasMeaningfulActiveWeight = [&activeSlotMask](const WPMdl::Vertex& vertex) {
            constexpr float kMinActiveBlendWeight = 0.5f;
            float           activeWeight = 0.0f;
            for (size_t i = 0; i < vertex.blend_indices.size() && i < vertex.weight.size(); ++i) {
                const size_t slot = std::min<size_t>(vertex.blend_indices[i], activeSlotMask.size() - 1);
                if (! activeSlotMask[slot]) {
                    continue;
                }
                activeWeight += std::max(0.0f, vertex.weight[i]);
            }
            return activeWeight >= kMinActiveBlendWeight;
        };

        for (const auto& tri : mdl.indices) {
            size_t activeVertexCount = 0;
            for (const uint16_t vertexIndex : tri) {
                if (vertexIndex < mdl.vertexs.size() &&
                    hasMeaningfulActiveWeight(mdl.vertexs[vertexIndex])) {
                    activeVertexCount++;
                }
            }
            const bool isOverlayTriangle = activeVertexCount >= 2;
            const bool keepTriangle = includeFullyActiveTriangles ? isOverlayTriangle : ! isOverlayTriangle;
            if (! keepTriangle) {
                continue;
            }

            std::array<uint16_t, 3> remappedTri {};
            bool                    triValid = true;
            for (size_t corner = 0; corner < tri.size(); ++corner) {
                const uint16_t sourceIndex = tri[corner];
                if (sourceIndex >= mdl.vertexs.size()) {
                    triValid = false;
                    break;
                }

                uint32_t& remappedIndex = remappedVertexIndices[sourceIndex];
                if (remappedIndex == std::numeric_limits<uint32_t>::max()) {
                    remappedIndex = static_cast<uint32_t>(filteredVertices.size());
                    filteredVertices.push_back(mdl.vertexs[sourceIndex]);
                }
                if (remappedIndex > std::numeric_limits<uint16_t>::max()) {
                    triValid = false;
                    break;
                }
                remappedTri[corner] = static_cast<uint16_t>(remappedIndex);
            }

            if (triValid) {
                filteredIndices.push_back(remappedTri);
            }
        }

        if (filteredVertices.empty() || filteredIndices.empty()) {
            return false;
        }

        outSubmesh =
            WPMdl::Submesh { mdl.mat_json_file, std::move(filteredVertices), std::move(filteredIndices) };
        return true;
    };

    WPMdl::Submesh filteredSubmesh;
    if (! buildFilteredSubmesh(filteredSubmesh)) {
        GenPuppetImageSpaceMesh(mesh, mdl, imageSize);
        return activePrimaryBlendSlots.empty();
    }

    WPMdl filteredMdl = mdl;
    filteredMdl.vertexs = std::move(filteredSubmesh.vertexs);
    filteredMdl.indices = std::move(filteredSubmesh.indices);
    GenPuppetImageSpaceMesh(mesh, filteredMdl, imageSize);
    return true;
}

void WPMdlParser::GenPuppetChannelMapMesh(SceneMesh& mesh, const WPMdl& mdl) {
    GenPuppetChannelMapMesh(mesh, mdl, {});
}

void WPMdlParser::GenPuppetChannelMapBaseUvMesh(SceneMesh&                  mesh,
                                                const WPMdl&                mdl,
                                                const std::array<float, 2>& imageSize) {
    SceneVertexArray vertex({ { WE_IN_POSITION.data(), VertexType::FLOAT3 },
                              { WE_IN_BLENDINDICES.data(), VertexType::UINT4 },
                              { WE_IN_BLENDWEIGHTS.data(), VertexType::FLOAT4 },
                              { WE_IN_TEXCOORDVEC4.data(), VertexType::FLOAT4 } },
                            mdl.vertexs.size());

    const float width = std::max(imageSize[0], 1.0f);
    const float height = std::max(imageSize[1], 1.0f);

    std::array<float, 16> one_vert {};
    for (uint i = 0; i < mdl.vertexs.size(); i++) {
        const auto& in = mdl.vertexs[i];
        const std::array<float, 3> uv_space_position {
            (in.texcoord[0] - 0.5f) * width,
            (0.5f - in.texcoord[1]) * height,
            0.0f,
        };
        const std::array<float, 4> texcoord_vec4 {
            in.channelmap_texcoord[0],
            in.channelmap_texcoord[1],
            in.texcoord[0],
            in.texcoord[1],
        };

        std::fill(one_vert.begin(), one_vert.end(), 0.0f);
        memcpy(one_vert.data() + 4 * 0, uv_space_position.data(), sizeof(uv_space_position));
        memcpy(one_vert.data() + 4 * 1, in.blend_indices.data(), sizeof(in.blend_indices));
        memcpy(one_vert.data() + 4 * 2, in.weight.data(), sizeof(in.weight));
        memcpy(one_vert.data() + 4 * 3, texcoord_vec4.data(), sizeof(texcoord_vec4));
        vertex.SetVertexs(i, one_vert);
    }

    std::vector<uint32_t> indices;
    size_t                u16_count = mdl.indices.size() * 3;
    indices.resize(u16_count / 2 + 1);
    memcpy(indices.data(), mdl.indices.data(), u16_count * sizeof(uint16_t));

    mesh.AddVertexArray(std::move(vertex));
    mesh.AddIndexArray(SceneIndexArray(indices));
}

void WPMdlParser::GenPuppetChannelMapMesh(SceneMesh&                       mesh,
                                          const WPMdl&                     mdl,
                                          std::span<const Eigen::Affine3f> bone_affines) {
    SceneVertexArray vertex({ { WE_IN_POSITION.data(), VertexType::FLOAT3 },
                              { WE_IN_BLENDINDICES.data(), VertexType::UINT4 },
                              { WE_IN_BLENDWEIGHTS.data(), VertexType::FLOAT4 },
                              { WE_IN_TEXCOORDVEC4.data(), VertexType::FLOAT4 } },
                            mdl.vertexs.size());

    std::array<float, 16> one_vert {};
    for (uint i = 0; i < mdl.vertexs.size(); i++) {
        auto        in = mdl.vertexs[i];
        const std::array<float, 4> texcoord_vec4 {
            in.channelmap_texcoord[0],
            in.channelmap_texcoord[1],
            in.texcoord[0],
            in.texcoord[1],
        };

        if (! bone_affines.empty()) {
            Eigen::Vector3f skinned = Eigen::Vector3f::Zero();
            float           totalWeight = 0.0f;
            for (usize j = 0; j < in.weight.size(); ++j) {
                const float weight = in.weight[j];
                const usize boneIndex = static_cast<usize>(in.blend_indices[j]);
                if (weight <= 1.0e-6f || boneIndex >= bone_affines.size()) {
                    continue;
                }
                skinned += (bone_affines[boneIndex] * Eigen::Vector3f(in.position.data())) * weight;
                totalWeight += weight;
            }
            if (totalWeight > 1.0e-6f) {
                skinned /= totalWeight;
                std::copy_n(skinned.data(), 3, in.position.begin());
            }
        }

        std::fill(one_vert.begin(), one_vert.end(), 0.0f);
        memcpy(one_vert.data() + 4 * 0, in.position.data(), sizeof(in.position));
        memcpy(one_vert.data() + 4 * 1, in.blend_indices.data(), sizeof(in.blend_indices));
        memcpy(one_vert.data() + 4 * 2, in.weight.data(), sizeof(in.weight));
        memcpy(one_vert.data() + 4 * 3, texcoord_vec4.data(), sizeof(texcoord_vec4));
        vertex.SetVertexs(i, one_vert);
    }

    std::vector<uint32_t> indices;
    size_t                u16_count = mdl.indices.size() * 3;
    indices.resize(u16_count / 2 + 1);
    memcpy(indices.data(), mdl.indices.data(), u16_count * sizeof(uint16_t));

    mesh.AddVertexArray(std::move(vertex));
    mesh.AddIndexArray(SceneIndexArray(indices));
}

void WPMdlParser::GenStaticMesh(SceneMesh& mesh,
                                const WPMdl::Submesh& mdl,
                                bool                  useNormalMap,
                                bool                  useLightmap,
                                const Eigen::Matrix3f& basis) {
    const bool includeTangent = useNormalMap || useLightmap;
    std::vector<SceneVertexArray::SceneVertexAttribute> attrs {
        { WE_IN_POSITION.data(), VertexType::FLOAT3 },
        { WE_IN_NORMAL.data(), VertexType::FLOAT3 },
    };
    if (includeTangent) {
        attrs.push_back({ WE_IN_TANGENT4.data(), VertexType::FLOAT4 });
    }
    if (useLightmap) {
        attrs.push_back({ WE_IN_TEXCOORDVEC4.data(), VertexType::FLOAT4 });
    } else {
        attrs.push_back({ WE_IN_TEXCOORD.data(), VertexType::FLOAT2 });
    }

    SceneVertexArray vertex(attrs, mdl.vertexs.size());

    Eigen::Matrix3f basisLinear = basis;
    if (! basisLinear.allFinite()) {
        basisLinear = Eigen::Matrix3f::Identity();
    }

    for (uint i = 0; i < mdl.vertexs.size(); i++) {
        const auto& in = mdl.vertexs[i];
        std::vector<float> packed(attrs.size() * 4, 0.0f);
        usize              offset = 0;

        const Eigen::Vector3f position = basisLinear * Eigen::Vector3f(in.position.data());

        Eigen::Vector3f normal = basisLinear * Eigen::Vector3f(in.normal.data());
        if (normal.squaredNorm() > 1.0e-12f) {
            normal.normalize();
        } else {
            normal = Eigen::Vector3f::UnitZ();
        }

        memcpy(packed.data() + 4 * (offset++), position.data(), sizeof(float) * 3);
        memcpy(packed.data() + 4 * (offset++), normal.data(), sizeof(float) * 3);
        if (includeTangent) {
            Eigen::Vector3f tangent = basisLinear * Eigen::Vector3f(in.tangent[0], in.tangent[1], in.tangent[2]);
            if (tangent.squaredNorm() > 1.0e-12f) {
                tangent.normalize();
            } else {
                tangent = Eigen::Vector3f::UnitX();
            }

            const std::array<float, 4> tangent4 { tangent.x(), tangent.y(), tangent.z(), in.tangent[3] };
            memcpy(packed.data() + 4 * (offset++), tangent4.data(), sizeof(tangent4));
        }
        if (useLightmap) {
            const std::array<float, 4> texcoords { in.texcoord[0], in.texcoord[1], in.texcoord2[0], in.texcoord2[1] };
            memcpy(packed.data() + 4 * (offset++), texcoords.data(), sizeof(texcoords));
        } else {
            memcpy(packed.data() + 4 * (offset++), in.texcoord.data(), sizeof(in.texcoord));
        }
        vertex.SetVertexs(i, packed);
    }

    std::vector<uint32_t> indices;
    size_t                u16_count = mdl.indices.size() * 3;
    indices.resize(u16_count / 2 + 1);
    memcpy(indices.data(), mdl.indices.data(), u16_count * sizeof(uint16_t));

    mesh.AddVertexArray(std::move(vertex));
    mesh.AddIndexArray(SceneIndexArray(indices));
}

void WPMdlParser::AddPuppetShaderInfo(WPShaderInfo& info, const WPMdl& mdl) {
    info.combos["SKINNING"]  = "1";
    info.combos["BONECOUNT"] = std::to_string(mdl.puppet->bones.size());
}

void WPMdlParser::AddPuppetMatInfo(wpscene::WPMaterial& mat, const WPMdl& mdl) {
    mat.combos["SKINNING"]  = 1;
    mat.combos["BONECOUNT"] = (i32)mdl.puppet->bones.size();
    mat.use_puppet          = true;
}
