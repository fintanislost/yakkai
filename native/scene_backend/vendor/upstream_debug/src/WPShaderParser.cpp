#include "WPShaderParser.hpp"

#include "Fs/IBinaryStream.h"
#include "Utils/Logging.h"
#include "WPJson.hpp"

#include "wpscene/WPUniform.h"
#include "Fs/VFS.h"
#include "Utils/Sha.hpp"
#include "Utils/String.h"
#include "WPCommon.hpp"

#include "Vulkan/ShaderComp.hpp"

#include <regex>
#include <fstream>
#include <stack>
#include <charconv>
#include <string>

static constexpr std::string_view SHADER_PLACEHOLD { "__SHADER_PLACEHOLD__" };

#define SHADER_DIR    "spvs01"
#define SHADER_SUFFIX "spvs"

using namespace wallpaper;

namespace
{

static constexpr const char* pre_shader_code = R"(#version 150
#extension GL_EXT_spec_constant_composites : enable
#define GLSL 1
#define HLSL 1
#define highp

#define CAST2(x) (vec2(x))
#define CAST3(x) (vec3(x))
#define CAST4(x) (vec4(x))
#define CAST3X3(x) (mat3(x))

#define texSample2D texture
#define texSample2DLod textureLod
#define mul(x, y) ((y) * (x))
#define frac fract
#define atan2 atan
#define fmod(x, y) (x-y*trunc(x/y))
#define ddx dFdx
#define ddy(x) dFdy(-(x))
#define saturate(x) (clamp(x, 0.0, 1.0))

#define float1 float
#define float2 vec2
#define float3 vec3
#define float4 vec4
#define lerp mix

__SHADER_PLACEHOLD__

)";

static constexpr const char* pre_shader_code_vert = R"(
#define attribute in
#define varying out

)";
static constexpr const char* pre_shader_code_frag = R"(
#define varying in
#define gl_FragColor glOutColor
out vec4 glOutColor;

)";

inline std::string LoadGlslInclude(fs::VFS& vfs, const std::string& input) {
    std::string::size_type pos = 0;
    std::string            output;
    std::string::size_type linePos = std::string::npos;

    while (linePos = input.find("#include", pos), linePos != std::string::npos) {
        auto lineEnd  = input.find_first_of('\n', linePos);
        auto lineSize = lineEnd - linePos;
        auto lineStr  = input.substr(linePos, lineSize);
        output.append(input.substr(pos, linePos - pos));

        auto inP         = lineStr.find_first_of('\"') + 1;
        auto inE         = lineStr.find_last_of('\"');
        auto includeName = lineStr.substr(inP, inE - inP);
        auto includeSrc  = fs::GetFileContent(vfs, "/assets/shaders/" + includeName);
        output.append("\n//-----include " + includeName + "\n");
        output.append(LoadGlslInclude(vfs, includeSrc));
        output.append("\n//-----include end\n");

        pos = lineEnd;
    }
    output.append(input.substr(pos));
    return output;
}

inline void ParseWPShader(const std::string& src, WPShaderInfo* pWPShaderInfo,
                          const std::vector<WPShaderTexInfo>& texinfos) {
    auto& combos       = pWPShaderInfo->combos;
    auto& wpAliasDict  = pWPShaderInfo->alias;
    auto& shadervalues = pWPShaderInfo->svs;
    auto& defTexs      = pWPShaderInfo->defTexs;
    idx   texcount     = std::ssize(texinfos);

    // pos start of line
    std::string::size_type pos = 0, lineEnd = std::string::npos;
    while ((lineEnd = src.find_first_of(('\n'), pos)), true) {
        const auto clineEnd = lineEnd;
        const auto line     = src.substr(pos, lineEnd - pos);

        /*
        if(line.find("attribute ") != std::string::npos || line.find("in ") != std::string::npos) {
            update_pos = true;
        }
        */
        if (line.find("// [COMBO]") != std::string::npos) {
            nlohmann::json combo_json;
            std::string comboStr = line.substr(line.find_first_of('{'));
            // Fix malformed JSON: unquoted keys in "options" like {Color":0}  → {"Color":0}
            for (size_t p = 0; p < comboStr.size(); p++) {
                if (comboStr[p] == '{' && p + 1 < comboStr.size() &&
                    comboStr[p + 1] != '"' && comboStr[p + 1] != '}' &&
                    std::isalpha((unsigned char)comboStr[p + 1])) {
                    comboStr.insert(p + 1, "\"");
                }
            }
            if (PARSE_JSON(comboStr, combo_json)) {
                if (combo_json.contains("combo")) {
                    std::string name;
                    int32_t     value = 0;
                    GET_JSON_NAME_VALUE(combo_json, "combo", name);
                    GET_JSON_NAME_VALUE(combo_json, "default", value);
                    combos[name] = std::to_string(value);
                }
            }
        } else if (line.find("uniform ") != std::string::npos) {
            if (line.find("// {") != std::string::npos) {
                nlohmann::json sv_json;
                if (PARSE_JSON(line.substr(line.find_first_of('{')), sv_json)) {
                    std::vector<std::string> defines =
                        utils::SpliteString(line.substr(0, line.find_first_of(';')), ' ');

                    std::string material;
                    GET_JSON_NAME_VALUE_NOWARN(sv_json, "material", material);
                    if (! material.empty()) wpAliasDict[material] = defines.back();

                    ShaderValue sv;
                    std::string name  = defines.back();
                    bool        istex = name.compare(0, 9, "g_Texture") == 0;
                    if (istex) {
                        wpscene::WPUniformTex wput;
                        wput.FromJson(sv_json);
                        i32 index { 0 };
                        STRTONUM(name.substr(9), index);
                        if (! wput.default_.empty()) defTexs.push_back({ index, wput.default_ });
                        if (! wput.combo.empty()) {
                            if (index >= texcount)
                                combos[wput.combo] = "0";
                            else
                                combos[wput.combo] = "1";
                        }
                        if (index < texcount && texinfos[(usize)index].enabled) {
                            auto& compos = texinfos[(usize)index].composEnabled;

                            usize num = std::min(std::size(compos), std::size(wput.components));
                            for (usize i = 0; i < num; i++) {
                                if (compos[i]) combos[wput.components[i].combo] = "1";
                            }
                        }

                    } else {
                        if (sv_json.contains("default")) {
                            auto        value = sv_json.at("default");
                            ShaderValue sv;
                            name = defines.back();
                            if (value.is_string()) {
                                std::vector<float> v;
                                GET_JSON_VALUE(value, v);
                                sv = std::span<const float>(v);
                            } else if (value.is_number()) {
                                sv.setSize(1);
                                GET_JSON_VALUE(value, sv[0]);
                            }
                            shadervalues[name] = sv;
                        }
                        if (sv_json.contains("combo")) {
                            std::string name;
                            GET_JSON_NAME_VALUE(sv_json, "combo", name);
                            combos[name] = "1";
                        }
                    }
                    if (defines.back()[0] != 'g') {
                        LOG_INFO("PreShaderSrc User shadervalue not supported");
                    }
                }
            }
        }

        // end
        if (line.find("void main()") != std::string::npos || clineEnd == std::string::npos) {
            break;
        }
        pos = lineEnd + 1;
    }
}

inline usize FindIncludeInsertPos(const std::string& src, usize startPos) {
    /* rule:
    after attribute/varying/uniform/struct
    befor any func
    not in {}
    not in #if #endif
    */
    (void)startPos;

    auto NposToZero = [](usize p) {
        return p == std::string::npos ? 0 : p;
    };
    auto search = [](const std::string& p, usize pos, const auto& re) {
        auto        startpos = p.begin() + (isize)pos;
        std::smatch match;
        if (startpos < p.end() && std::regex_search(startpos, p.end(), match, re)) {
            return pos + (usize)match.position();
        }
        return std::string::npos;
    };
    auto searchLast = [](const std::string& p, const auto& re) {
        auto        startpos = p.begin();
        std::smatch match;
        while (startpos < p.end() && std::regex_search(startpos, p.end(), match, re)) {
            startpos++;
            startpos += match.position();
        }
        return startpos >= p.end() ? std::string::npos : usize(startpos - p.begin());
    };
    auto nextLinePos = [](const std::string& p, usize pos) {
        return p.find_first_of('\n', pos) + 1;
    };

    usize mainPos  = src.find("void main(");
    bool  two_main = src.find("void main(", mainPos + 2) != std::string::npos;
    if (two_main) return 0;

    usize pos;
    {
        const std::regex reAfters(R"(\n(attribute|varying|uniform|struct) )");
        usize            afterPos = searchLast(src, reAfters);
        if (afterPos != std::string::npos) {
            afterPos = nextLinePos(src, afterPos + 1);
        }
        pos = std::min({ NposToZero(afterPos), mainPos });
    }
    {
        std::stack<usize> ifStack;
        usize             nowPos { 0 };
        const std::regex  reIfs(R"((#if|#endif))");
        while (true) {
            auto p = search(src, nowPos + 1, reIfs);
            if (p > mainPos || p == std::string::npos) break;
            if (src.substr(p, 3) == "#if") {
                ifStack.push(p);
            } else {
                if (ifStack.empty()) break;
                usize ifp = ifStack.top();
                ifStack.pop();
                usize endp = p;
                if (pos > ifp && pos <= endp) {
                    pos = nextLinePos(src, endp + 1);
                }
            }
            nowPos = p;
        }
        pos = std::min({ pos, mainPos });
    }

    return NposToZero(pos);
}

inline EShLanguage ToGLSL(ShaderType type) {
    switch (type) {
    case ShaderType::VERTEX: return EShLangVertex;
    case ShaderType::FRAGMENT: return EShLangFragment;
    default: return EShLangVertex;
    }
}

inline std::string Preprocessor(const std::string& in_src, ShaderType type, const Combos& combos,
                                WPPreprocessorInfo& process_info) {
    std::string res;

    std::string src = wallpaper::WPShaderParser::PreShaderHeader(in_src, combos, type);

    // Resolve #require directives — WE injects these at runtime.
    // We provide our own implementations.
    {
        static const std::string lightingV1Impl = R"(
// --- PerformLighting_V1: point light PBR (no shadow mapping) ---
uniform vec3 g_LightsPosition[4];
uniform vec4 g_LightsColorRadius[4];

vec3 PerformLighting_V1(vec3 worldPos, vec3 albedo, vec3 normal,
                        vec3 viewDir, vec3 specularTint, vec3 f0,
                        float roughness, float metallic)
{
    vec3 result = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        vec3 L = g_LightsPosition[i].xyz - worldPos;
        float lightRadius = g_LightsColorRadius[i].w;
        vec3 lightColor = g_LightsColorRadius[i].rgb;
        if (lightRadius <= 0.0 || dot(lightColor, lightColor) <= 0.0) continue;
        result += ComputePBRLightShadow(normal, L, viewDir, albedo, lightColor,
                                        lightRadius, 2.0, specularTint, f0,
                                        roughness, metallic, 1.0);
    }
    return result;
}
// --- end PerformLighting_V1 ---
)";

        std::regex re_require("(^|\r?\n)#require (.+)(\r?\n)");
        std::smatch match;
        std::string tmp = src;
        std::string out;
        while (std::regex_search(tmp, match, re_require)) {
            out += match.prefix().str() + match[1].str();
            std::string name = match[2].str();
            // Trim whitespace
            while (! name.empty() && (name.back() == ' ' || name.back() == '\r'))
                name.pop_back();
            if (name == "LightingV1") {
                out += lightingV1Impl;
            } else {
                out += "//#require " + name;  // Comment out unknown requires
            }
            out += match[3].str();
            tmp = match.suffix().str();
        }
        out += tmp;
        src = out;
    }

    // Fix unbalanced #if/#endif — some workshop shaders have extra #endif
    // that WE's preprocessor ignores but glslang rejects.
    {
        int depth = 0;
        std::string::size_type pos = 0;
        while (pos < src.size()) {
            auto lineEnd = src.find('\n', pos);
            if (lineEnd == std::string::npos) lineEnd = src.size();
            auto line = src.substr(pos, lineEnd - pos);
            // Trim leading whitespace for directive detection
            auto trimmed = line.find_first_not_of(" \t");
            if (trimmed != std::string::npos) {
                auto directive = line.substr(trimmed);
                if (directive.compare(0, 3, "#if") == 0 &&
                    (directive.size() <= 3 || directive[3] == ' ' || directive[3] == 'd' || directive[3] == 'n')) {
                    depth++;
                } else if (directive.compare(0, 6, "#endif") == 0) {
                    if (depth > 0) {
                        depth--;
                    } else {
                        // Unmatched #endif — comment it out
                        src.replace(pos + trimmed, 6, "//" "endif");
                        LOG_INFO("commented out unmatched #endif at source offset %zu", pos);
                    }
                }
            }
            pos = lineEnd + 1;
        }
    }

    // Strip custom inverse() polyfills — GLSL 150+ has it built-in.
    // WE shaders include polyfills for older GLSL that conflict with the
    // built-in when precision qualifiers are stripped by #define highp.
    {
        std::string::size_type pos = 0;
        while ((pos = src.find("inverse(mat", pos)) != std::string::npos) {
            // Check if this is a function DEFINITION (not a call)
            // Look backwards for a type: "mat3 inverse(mat3" or "mat4 inverse(mat4"
            auto lineStart = src.rfind('\n', pos);
            if (lineStart == std::string::npos) lineStart = 0; else lineStart++;
            auto line = src.substr(lineStart, pos - lineStart);
            // Trim whitespace
            auto trimmed = line.find_first_not_of(" \t\r\n");
            if (trimmed != std::string::npos) {
                auto prefix = line.substr(trimmed);
                if (prefix.find("mat") == 0) {
                    // This is a function definition. Comment out until matching }
                    int braces = 0;
                    auto funcStart = lineStart;
                    auto p = src.find('{', pos);
                    if (p != std::string::npos) {
                        braces = 1;
                        auto bodyStart = p + 1;
                        while (bodyStart < src.size() && braces > 0) {
                            if (src[bodyStart] == '{') braces++;
                            else if (src[bodyStart] == '}') braces--;
                            bodyStart++;
                        }
                        // Replace the entire function with a comment
                        std::string replacement = "// STRIPPED inverse() polyfill (GLSL 150 built-in)\n";
                        src.replace(funcStart, bodyStart - funcStart, replacement);
                        pos = funcStart + replacement.size();
                        continue;
                    }
                }
            }
            pos++;
        }
    }

    glslang::TShader::ForbidIncluder includer;
    glslang::TShader                 shader(ToGLSL(type));
    const EShMessages emsg { (EShMessages)(EShMsgDefault | EShMsgSpvRules | EShMsgRelaxedErrors |
                                           EShMsgSuppressWarnings | EShMsgVulkanRules) };

    auto* data = src.c_str();
    shader.setStrings(&data, 1);
    shader.preprocess(&vulkan::DefaultTBuiltInResource,
                      110,
                      EProfile::ECoreProfile,
                      false,
                      false,
                      emsg,
                      &res,
                      includer);

    // Fix mutable varying: WE shaders (OpenGL) write to varying inputs in
    // fragment shaders, but GLSL 150 core `in` variables are read-only.
    // Create local mutable copies for any `in` variable that is assigned.
    if (type == ShaderType::FRAGMENT) {
        std::regex re_in_decl(R"((\s+)(in)\s+([\w]+)\s+([\w]+)\s*;)");
        std::smatch m;
        std::string patched = res;
        std::string mainInit;
        auto searchStart = patched.cbegin();
        while (std::regex_search(searchStart, patched.cend(), m, re_in_decl)) {
            std::string varName = m[4].str();
            std::string varType = m[3].str();
            // Check if this variable is assigned anywhere in the source
            // Use simple string search instead of regex to avoid catastrophic backtracking
            bool isAssigned = false;
            try {
                isAssigned = res.find(varName + " =") != std::string::npos ||
                             res.find(varName + "=") != std::string::npos ||
                             res.find(varName + ".") != std::string::npos ||
                             res.find(varName + "[") != std::string::npos;
            } catch (...) {}
            if (isAssigned) {
                // Rename the in declaration and add a local copy
                std::string prefixed = "_wp_in_" + varName;
                patched = std::regex_replace(patched,
                    std::regex("\\bin\\s+" + varType + "\\s+" + varName + "\\s*;"),
                    "in " + varType + " " + prefixed + ";");
                mainInit += "  " + varType + " " + varName + " = " + prefixed + ";\n";
                LOG_INFO("patched mutable varying: %s %s -> local copy from %s",
                         varType.c_str(), varName.c_str(), prefixed.c_str());
            }
            searchStart = m.suffix().first;
        }
        if (! mainInit.empty()) {
            auto mainPos = patched.find("void main()");
            if (mainPos != std::string::npos) {
                auto bracePos = patched.find('{', mainPos);
                if (bracePos != std::string::npos) {
                    patched.insert(bracePos + 1, "\n" + mainInit);
                }
            }
            res = patched;
        }
    }

    std::regex re_io(R"([^\n]+\s(in|out)\s[\s\w]+\s(\w+)\s*;)", std::regex::ECMAScript);
    for (auto it = std::sregex_iterator(res.begin(), res.end(), re_io);
         it != std::sregex_iterator();
         it++) {
        std::smatch mc = *it;
        if (mc[1] == "in") {
            process_info.input[mc[2]] = mc[0].str();
        } else {
            process_info.output[mc[2]] = mc[0].str();
        }
    }

    std::regex re_tex(R"(uniform\s+sampler2D\s+g_Texture(\d+))", std::regex::ECMAScript);
    for (auto it = std::sregex_iterator(res.begin(), res.end(), re_tex);
         it != std::sregex_iterator();
         it++) {
        std::smatch mc  = *it;
        auto        str = mc[1].str();
        uint        slot;
        auto [ptr, ec] { std::from_chars(str.c_str(), str.c_str() + str.size(), slot) };
        if (ec != std::errc()) continue;
        process_info.active_tex_slots.insert(slot);
    }
    return res;
}

inline std::string Finalprocessor(const WPShaderUnit& unit, const WPPreprocessorInfo* pre,
                                  const WPPreprocessorInfo* next) {
    std::string insert_str {};
    auto&       cur = unit.preprocess_info;
    if (pre != nullptr) {
        for (auto& [k, v] : pre->output) {
            if (! exists(cur.input, k)) {
                auto n = std::regex_replace(v, std::regex(R"(\s*out\s)"), " in ");
                insert_str += n + '\n';
            }
        }
    }
    if (next != nullptr) {
        for (auto& [k, v] : next->input) {
            if (! exists(cur.output, k)) {
                auto n = std::regex_replace(v, std::regex(R"(\s*in\s)"), " out ");
                insert_str += n + '\n';
            }
        }
    }
    std::regex re_hold(SHADER_PLACEHOLD.data());

    // LOG_INFO("insert: %s", insert_str.c_str());
    // return std::regex_replace(
    //    std::regex_replace(cur.result, re_hold, insert_str), std::regex(R"(\s+\n)"), "\n");
    return std::regex_replace(unit.src, re_hold, insert_str);
}

inline std::string GenSha1(std::span<const WPShaderUnit> units) {
    std::string shas;
    for (auto& unit : units) {
        shas += utils::genSha1(unit.src);
    }
    return utils::genSha1(shas);
}
inline std::string GetCachePath(std::string_view scene_id, std::string_view filename) {
    return std::string("/cache/") + std::string(scene_id) + "/" SHADER_DIR "/" +
           std::string(filename) + "." SHADER_SUFFIX;
}

inline bool LoadShaderFromFile(std::vector<ShaderCode>& codes, fs::IBinaryStream& file) {
    codes.clear();
    i32 ver = ReadSPVVesion(file);

    usize count = file.ReadUint32();
    assert(count <= 16 && count >= 0);
    if (count > 16) return false;

    codes.resize(count);
    for (usize i = 0; i < count; i++) {
        auto& c = codes[i];

        u32 size = file.ReadUint32();
        assert(size % 4 == 0);
        if (size % 4 != 0) return false;

        c.resize(size / 4);
        file.Read((char*)c.data(), size);
    }
    return true;
}

inline void SaveShaderToFile(std::span<const ShaderCode> codes, fs::IBinaryStreamW& file) {
    char nop[256] { '\0' };

    WriteSPVVesion(file, 1);
    file.WriteUint32((u32)codes.size());
    for (const auto& c : codes) {
        u32 size = (u32)c.size() * 4;
        file.WriteUint32(size);
        file.Write((const char*)c.data(), size);
    }
    file.Write(nop, sizeof(nop));
}

} // namespace

std::string WPShaderParser::PreShaderSrc(fs::VFS& vfs, const std::string& src,
                                         WPShaderInfo*                       pWPShaderInfo,
                                         const std::vector<WPShaderTexInfo>& texinfos) {
    std::string            newsrc(src);
    std::string::size_type pos = 0;
    std::string            include;
    while (pos = src.find("#include", pos), pos != std::string::npos) {
        auto begin = pos;
        pos        = src.find_first_of('\n', pos);
        newsrc.replace(begin, pos - begin, pos - begin, ' ');
        include.append(src.substr(begin, pos - begin) + "\n");
    }
    include = LoadGlslInclude(vfs, include);

    ParseWPShader(include, pWPShaderInfo, texinfos);
    ParseWPShader(newsrc, pWPShaderInfo, texinfos);

    newsrc.insert(FindIncludeInsertPos(newsrc, 0), include);
    return newsrc;
}

std::string WPShaderParser::PreShaderHeader(const std::string& src, const Combos& combos,
                                            ShaderType type) {
    std::string pre(pre_shader_code);
    if (type == ShaderType::VERTEX) pre += pre_shader_code_vert;
    if (type == ShaderType::FRAGMENT) pre += pre_shader_code_frag;
    std::string header(pre);
    for (const auto& c : combos) {
        std::string cup(c.first);
        std::transform(c.first.begin(), c.first.end(), cup.begin(), ::toupper);
        if (c.second.empty()) {
            LOG_ERROR("combo '%s' can't be empty", cup.c_str());
            continue;
        }
        header.append("#define " + cup + " " + c.second + "\n");
    }
    return header + src;
}

void WPShaderParser::InitGlslang() { glslang::InitializeProcess(); }
void WPShaderParser::FinalGlslang() { glslang::FinalizeProcess(); }

bool WPShaderParser::CompileToSpv(std::string_view scene_id, std::span<WPShaderUnit> units,
                                  std::vector<ShaderCode>& codes, fs::VFS& vfs,
                                  WPShaderInfo* shader_info, std::span<const WPShaderTexInfo> texs) {
    (void)texs;

    std::for_each(units.begin(), units.end(), [shader_info](auto& unit) {
        unit.src = Preprocessor(unit.src, unit.stage, shader_info->combos, unit.preprocess_info);
    });

    // Fix varying type mismatches between vertex (out) and fragment (in).
    // WE shaders commonly use vec4 v_TexCoord in vertex but vec2 in fragment.
    // Promote the fragment input to match the vertex output type.
    if (units.size() == 2) {
        auto& vertInfo = units[0].preprocess_info;
        auto& fragInfo = units[1].preprocess_info;
        for (auto& [name, fragDecl] : fragInfo.input) {
            if (vertInfo.output.count(name)) {
                const auto& vertDecl = vertInfo.output.at(name);
                // Extract types: match "in/out <type> <name>"
                auto extractType = [](const std::string& decl) -> std::string {
                    std::regex re(R"(\b(vec[234]|float|mat[234]|int)\s+\w+\s*;)");
                    std::smatch m;
                    if (std::regex_search(decl, m, re)) return m[1].str();
                    return {};
                };
                std::string vertType = extractType(vertDecl);
                std::string fragType = extractType(fragDecl);
                if (! vertType.empty() && ! fragType.empty() && vertType != fragType) {
                    LOG_INFO("fixing varying type mismatch: %s vert=%s frag=%s → demoting vert to %s",
                             name.c_str(), vertType.c_str(), fragType.c_str(), fragType.c_str());
                    // Demote the VERTEX output type to match the fragment input.
                    // This avoids renaming the varying (which would break the
                    // Finalprocessor's cross-stage matching).
                    std::string oldVertDecl = "out " + vertType + " " + name + ";";
                    std::string newVertDecl = "out " + fragType + " " + name + ";";
                    auto pos = units[0].src.find(oldVertDecl);
                    if (pos != std::string::npos) {
                        units[0].src.replace(pos, oldVertDecl.size(), newVertDecl);
                        // Also update vertex preprocess info
                        vertInfo.output[name] = std::regex_replace(vertInfo.output[name],
                            std::regex("\\b" + vertType + "\\b"), fragType);
                        // Add a .xy/.xyz swizzle at the assignment site in the
                        // vertex shader: "v_TexCoord = expr" → "v_TexCoord = (expr).xy"
                        std::regex reAssign("(" + name + R"(\s*=\s*)([^;]+)(;))");
                        units[0].src = std::regex_replace(units[0].src, reAssign,
                            "$1" + fragType + "($2)" + "$3");
                    }
                }
            }
        }
    }

    auto compile = [](std::span<WPShaderUnit> units, std::vector<ShaderCode>& codes) {
        std::vector<vulkan::ShaderCompUnit> vunits(units.size());
        for (usize i = 0; i < units.size(); i++) {
            auto&               unit     = units[i];
            auto&               vunit    = vunits[i];
            WPPreprocessorInfo* pre_info = i >= 1 ? &units[i - 1].preprocess_info : nullptr;
            WPPreprocessorInfo* post_info =
                i + 1 < units.size() ? &units[i + 1].preprocess_info : nullptr;

            unit.src = Finalprocessor(unit, pre_info, post_info);

            vunit.src   = unit.src;
            vunit.stage = ToGLSL(unit.stage);

        }

        vulkan::ShaderCompOpt opt;
        opt.client_ver             = glslang::EShTargetVulkan_1_1;
        opt.auto_map_bindings      = true;
        opt.auto_map_locations     = true;
        opt.relaxed_errors_glsl    = true;
        opt.relaxed_rules_vulkan   = true;
        opt.suppress_warnings_glsl = true;

        std::vector<vulkan::Uni_ShaderSpv> spvs(units.size());

        if (! vulkan::CompileAndLinkShaderUnits(vunits, opt, spvs)) {
            return false;
        }

        codes.clear();
        for (auto& spv : spvs) {
            codes.emplace_back(std::move(spv->spirv));
        }
        return true;
    };

    bool has_cache_dir = vfs.IsMounted("cache");

    if (has_cache_dir) {
        std::string sha1            = GenSha1(units);
        std::string cache_file_path = GetCachePath(scene_id, sha1);

        if (vfs.Contains(cache_file_path)) {
            auto cache_file = vfs.Open(cache_file_path);
            if (! cache_file || ! ::LoadShaderFromFile(codes, *cache_file)) {
                LOG_ERROR("load shader from \'%s\' failed", cache_file_path.c_str());
                return false;
            }
            // cache hit — no compilation needed
        } else {
            auto t0 = std::chrono::steady_clock::now();
            if (! compile(units, codes)) return false;
            auto t1 = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
            LOG_INFO("shader compiled in %lldms, caching: %s", (long long)ms, sha1.c_str());
            if (auto cache_file = vfs.OpenW(cache_file_path); cache_file) {
                ::SaveShaderToFile(codes, *cache_file);
            }
        }
        return true;

    } else {
        return compile(units, codes);
    }
}
