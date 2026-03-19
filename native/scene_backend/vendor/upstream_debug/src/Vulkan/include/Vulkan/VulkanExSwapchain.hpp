#pragma once

#include "Swapchain/ExSwapchain.hpp"
#include "Device.hpp"
#include <cstdio>

namespace wallpaper
{
namespace vulkan
{

struct VulkanExHandle : NoCopy {
    ExHandle          handle;
    ExImageParameters image;
    vvk::Semaphore    ready_semaphore;
    vvk::Semaphore    release_semaphore;

    VulkanExHandle()  = default;
    ~VulkanExHandle() = default;
    VulkanExHandle(VulkanExHandle&& o) noexcept
        : handle(o.handle),
          image(std::move(o.image)),
          ready_semaphore(std::move(o.ready_semaphore)),
          release_semaphore(std::move(o.release_semaphore)) {}
    VulkanExHandle& operator=(VulkanExHandle&& o) noexcept {
        handle            = o.handle;
        image             = std::move(o.image);
        ready_semaphore   = std::move(o.ready_semaphore);
        release_semaphore = std::move(o.release_semaphore);
        return *this;
    }
};

class VulkanExSwapchain : public ExSwapchain {
    using atomic_ = std::atomic<ExHandle*>;

public:
    VulkanExSwapchain(std::array<VulkanExHandle, 3> handles, VkExtent2D ext)
        : m_handles(std::move(handles)), m_extent(ext) {
        int index = 0;
        for (auto& h : m_handles) {
            auto& handle  = h.handle;
            handle.setId(index++);
            handle.width  = (i32)h.image.extent.width;
            handle.height = (i32)h.image.extent.height;
            handle.fd     = h.image.fd;
            handle.size   = h.image.mem_reqs.size;
        }
        m_presented  = &m_handles[0].handle;
        m_ready      = &m_handles[1].handle;
        m_inprogress = &m_handles[2].handle;
    }
    virtual ~VulkanExSwapchain() = default;

    uint width() const override { return m_extent.width; }
    uint height() const override { return m_extent.height; }

    const auto& handles() const { return m_handles; }

    ExImageParameters& GetInprogressImage() {
        return m_handles.at((usize)(*inprogress()).id()).image;
    }
    vvk::Semaphore& GetInprogressReadySemaphore() {
        return m_handles.at((usize)(*inprogress()).id()).ready_semaphore;
    }
    vvk::Semaphore& GetInprogressReleaseSemaphore() {
        return m_handles.at((usize)(*inprogress()).id()).release_semaphore;
    }

    constexpr VkFormat format() const { return VK_FORMAT_R8G8B8A8_UNORM; };

protected:
    atomic_& presented() override { return m_presented; };
    atomic_& ready() override { return m_ready; };
    atomic_& inprogress() override { return m_inprogress; };

private:
    std::array<VulkanExHandle, 3> m_handles;
    atomic_                       m_presented { nullptr };
    atomic_                       m_ready { nullptr };
    atomic_                       m_inprogress { nullptr };
    VkExtent2D                    m_extent;
};

inline bool CreateExportableSemaphore(const Device& device, vvk::Semaphore& semaphore, int* fd) {
    VkExportSemaphoreCreateInfo export_info {
        .sType       = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
        .pNext       = nullptr,
        .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT,
    };
    VkSemaphoreCreateInfo create_info {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &export_info,
    };

    if (device.handle().CreateSemaphore(create_info, semaphore) != VK_SUCCESS) return false;
    return semaphore.GetFdKHR(fd) == VK_SUCCESS;
}

inline std::unique_ptr<VulkanExSwapchain> CreateExSwapchain(const Device& device, uint w, uint h,
                                                            VkImageTiling tiling) {
    std::array<VulkanExHandle, 3> handles;
    for (auto& handle : handles) {
        if (auto rv = device.tex_cache().CreateExTex(w, h, VK_FORMAT_R8G8B8A8_UNORM, tiling);
            rv.has_value())
            handle.image = std::move(rv.value());
        else
            return nullptr;

        if (! CreateExportableSemaphore(device, handle.ready_semaphore, &handle.handle.ready_fd))
            return nullptr;
        if (! CreateExportableSemaphore(device,
                                        handle.release_semaphore,
                                        &handle.handle.release_fd))
            return nullptr;
    }
    return std::make_unique<VulkanExSwapchain>(std::move(handles), VkExtent2D { w, h });
}

} // namespace vulkan
} // namespace wallpaper
