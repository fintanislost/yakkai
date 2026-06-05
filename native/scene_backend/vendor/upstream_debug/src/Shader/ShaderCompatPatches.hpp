#pragma once

#include "Shader/ShaderTypes.hpp"

#include <string>

namespace wallpaper
{

bool ShouldPreservePuppetSourceAlphaForShader(const std::string& shader);

std::string ApplySourceAlphaPreservePatch(const std::string& src,
                                          const Combos& combos,
                                          ShaderType type);

} // namespace wallpaper
