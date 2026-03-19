#pragma once
#include "TripleSwapchain.hpp"
#include <atomic>
#include <cstdint>
#include <memory>

namespace wallpaper
{

enum class TexTiling
{
    OPTIMAL,
    LINEAR
};

struct ExHandleSyncState {
    std::atomic<bool> release_ready { false };
};

struct ExHandle {
    int         fd { -1 };
    int         ready_fd { -1 };
    int         release_fd { -1 };
    int32_t     width { 0 };
    int32_t     height { 0 };
    std::size_t size { 0 };
    std::shared_ptr<ExHandleSyncState> sync_state;
    // format rgba8

    ExHandle(): sync_state(std::make_shared<ExHandleSyncState>()) {}
    ExHandle(int id): sync_state(std::make_shared<ExHandleSyncState>()), m_id(id) {};

    int32_t id() const { return m_id; }
    void    setId(int32_t id) { m_id = id; }
    bool    takeReleaseReady() const {
        return sync_state && sync_state->release_ready.exchange(false);
    }
    void markReleaseReady() const {
        if (sync_state) sync_state->release_ready.store(true);
    }

private:
    int32_t m_id { 0 };
};

// class ExSwapchain : public TripleSwapchain<ExHandle> {};
using ExSwapchain = TripleSwapchain<ExHandle>;
} // namespace wallpaper
