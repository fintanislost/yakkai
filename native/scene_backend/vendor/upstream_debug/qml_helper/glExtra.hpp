#pragma once
#include <memory>
#include <array>
#include <cstdint>

#include <span>
#include "ExSwapchain.hpp"

class GlExtra {
public:
    GlExtra();
    ~GlExtra();
    bool init(void* get_proc_address(const char*));
    uint genExTexture(wallpaper::ExHandle&);
    bool importExSemaphores(wallpaper::ExHandle&, uint& ready_semaphore, uint& release_semaphore);
    bool waitSemaphoreTexture(uint semaphore, uint texture);
    bool signalSemaphoreTexture(uint semaphore, uint texture);
    void deleteTexture(uint);
    void deleteSemaphore(uint);
    void finish();
    bool supportsExternalSemaphoreInterop() const;

    std::span<const std::uint8_t> uuid() const;
    wallpaper::TexTiling     tiling() const;

private:
    class impl;
    std::unique_ptr<impl> pImpl;

    bool inited { false };

    wallpaper::TexTiling m_tiling { wallpaper::TexTiling::OPTIMAL };
};
