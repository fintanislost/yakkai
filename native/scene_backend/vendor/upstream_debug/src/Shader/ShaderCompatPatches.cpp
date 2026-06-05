#include "Shader/ShaderCompatPatches.hpp"

#include <regex>
#include <string_view>

namespace wallpaper
{
namespace
{

bool ComboEnabled(const Combos& combos, std::string_view name)
{
    const auto it = combos.find(std::string(name));
    return it != combos.end() && it->second != "0" && !it->second.empty();
}

std::string PreserveAlphaReplacement(std::string_view sampleCoordinate)
{
    return "vec4 yakkaiPreserveSourceAlphaColor = texSample2D(g_Texture0, " +
           std::string(sampleCoordinate) + ");\n"
           "\tvec4 yakkaiPreserveSourceAlphaSource = texSample2D(g_Texture0, v_TexCoord.xy);\n"
           "\tfloat yakkaiPreserveSourceAlphaBlend = clamp(yakkaiPreserveSourceAlphaColor.a / max(yakkaiPreserveSourceAlphaSource.a, 0.0001), 0.0, 1.0);\n"
           "\tyakkaiPreserveSourceAlphaColor.rgb = mix(yakkaiPreserveSourceAlphaSource.rgb, yakkaiPreserveSourceAlphaColor.rgb, yakkaiPreserveSourceAlphaBlend);\n"
           "\tyakkaiPreserveSourceAlphaColor.a = yakkaiPreserveSourceAlphaSource.a;\n"
           "\tgl_FragColor = yakkaiPreserveSourceAlphaColor;";
}

} // namespace

bool ShouldPreservePuppetSourceAlphaForShader(const std::string& shader)
{
    return shader == "effects/shake" ||
           shader.ends_with("/effects/shake");
}

std::string ApplySourceAlphaPreservePatch(const std::string& src,
                                          const Combos& combos,
                                          ShaderType type)
{
    if (type != ShaderType::FRAGMENT ||
        !ComboEnabled(combos, "YAKKAI_PRESERVE_SOURCE_ALPHA")) {
        return src;
    }
    if (src.find("v_TexCoord") == std::string::npos ||
        src.find("g_Texture0") == std::string::npos ||
        src.find("texCoord") == std::string::npos) {
        return src;
    }

    const std::regex re(R"(gl_FragColor\s*=\s*texSample2D\(\s*g_Texture0\s*,\s*texCoord\s*\)\s*;)");
    std::string patched = std::regex_replace(src, re, PreserveAlphaReplacement("texCoord"));

    const std::regex directDisplacedSample(
        R"(gl_FragColor\s*=\s*texSample2D\(\s*g_Texture0\s*,\s*(texCoordOffset\s*\+\s*v_TexCoord\.xy)\s*\)\s*;)");
    patched = std::regex_replace(
        patched,
        directDisplacedSample,
        PreserveAlphaReplacement("texCoordOffset + v_TexCoord.xy"));
    return patched;
}

} // namespace wallpaper
