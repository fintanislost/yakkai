#pragma once

namespace yakkai::harness
{

class CaptureStartGate
{
public:
    void markRootWindowReady()
    {
        m_rootWindowReady = true;
    }

    void markFirstFrameReady()
    {
        m_firstFrameReady = true;
    }

    bool consumeReadyToStart()
    {
        if (m_started || !m_rootWindowReady || !m_firstFrameReady) {
            return false;
        }

        m_started = true;
        return true;
    }

private:
    bool m_rootWindowReady = false;
    bool m_firstFrameReady = false;
    bool m_started = false;
};

}
