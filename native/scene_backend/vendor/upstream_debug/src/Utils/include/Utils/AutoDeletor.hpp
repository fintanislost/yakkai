#pragma once

#include <utility>

namespace wallpaper::utils
{

template<typename Callback>
class ScopeExit
{
public:
    explicit ScopeExit(Callback callback)
        : m_callback(std::move(callback))
    {
    }

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

    ScopeExit(ScopeExit&& other) noexcept
        : m_callback(std::move(other.m_callback))
        , m_active(std::exchange(other.m_active, false))
    {
    }

    ~ScopeExit()
    {
        if (m_active) {
            m_callback();
        }
    }

    void release()
    {
        m_active = false;
    }

private:
    Callback m_callback;
    bool m_active = true;
};

template<typename Callback>
ScopeExit<Callback> makeScopeExit(Callback callback)
{
    return ScopeExit<Callback>(std::move(callback));
}

} // namespace wallpaper::utils

#define AUTO_DELETER(name, callback) auto name = ::wallpaper::utils::makeScopeExit(callback)
