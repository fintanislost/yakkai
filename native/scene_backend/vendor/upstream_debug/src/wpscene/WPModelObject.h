#pragma once

#include "WPJson.hpp"

#include <array>
#include <nlohmann/json.hpp>
#include <string>

namespace wallpaper
{
namespace fs
{
class VFS;
}

namespace wpscene
{

class WPModelObject {
public:
    bool                 FromJson(const nlohmann::json&, fs::VFS&);
    int32_t              id { 0 };
    std::string          name;
    std::array<float, 3> origin { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> scale { 1.0f, 1.0f, 1.0f };
    std::array<float, 3> angles { 0.0f, 0.0f, 0.0f };
    bool                 visible { true };
    std::string          model;
};

} // namespace wpscene
} // namespace wallpaper
