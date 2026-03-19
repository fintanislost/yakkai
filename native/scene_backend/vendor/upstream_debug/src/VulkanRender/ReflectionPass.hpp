#pragma once
#include "VulkanPass.hpp"

#include "Scene/Scene.h"
#include "Vulkan/Device.hpp"
#include "Vulkan/GraphicsPipeline.hpp"
#include "Vulkan/StagingBuffer.hpp"

namespace wallpaper
{
namespace vulkan
{

class ReflectionPass : public VulkanPass {
public:
    struct Desc {
        std::string src;
        std::string dst;

        ImageParameters vk_src;
        ImageParameters vk_dst;
        VkClearValue    clear_value;

        StagingBufferRef   vertex_buf;
        vvk::Framebuffer   fb;
        PipelineParameters pipeline;
    };

    ReflectionPass(const Desc&);
    virtual ~ReflectionPass();

    void prepare(Scene&, const Device&, RenderingResources&) override;
    void execute(const Device&, RenderingResources&) override;
    void destory(const Device&, RenderingResources&) override;

private:
    Desc m_desc;
};

} // namespace vulkan
} // namespace wallpaper
