#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace wallpaper {

// Decodes video frames from an in-memory MP4 buffer using FFmpeg.
// Runs a background decode thread and double-buffers RGBA8 frames.
class VideoFrameDecoder {
public:
    using DataPtr = std::shared_ptr<const uint8_t[]>;

    VideoFrameDecoder(const uint8_t* data, size_t size, int expected_width, int expected_height);
    ~VideoFrameDecoder();

    VideoFrameDecoder(const VideoFrameDecoder&) = delete;
    VideoFrameDecoder& operator=(const VideoFrameDecoder&) = delete;

    // Returns true if the decoder opened the video successfully.
    bool IsValid() const { return m_valid; }

    int Width() const { return m_width; }
    int Height() const { return m_height; }

    // Returns the latest decoded frame (RGBA8, width*height*4 bytes).
    // Returns nullptr if no frame is available yet.
    // The returned pointer is valid until the next call to TryGetFrame().
    DataPtr TryGetFrame();

    // Decode the first frame synchronously (for initial texture upload).
    // Returns nullptr on failure.
    DataPtr DecodeFirstFrame();

    // Start the background decode thread (call after scene is ready).
    void Start();

    // Stop the background decode thread.
    void Stop();

private:
    void DecodeLoop();
    bool OpenVideo();
    DataPtr DecodeNextFrame();
    void SeekToStart();

    // The raw MP4 data (owned copy)
    std::vector<uint8_t> m_data;

    // FFmpeg contexts (opaque pointers to avoid header leak)
    struct FFmpegState;
    std::unique_ptr<FFmpegState> m_ff;

    int  m_width { 0 };
    int  m_height { 0 };
    bool m_valid { false };

    // Double buffer: decode thread writes to m_back, render reads m_front
    DataPtr            m_front;
    DataPtr            m_back;
    std::mutex         m_frameMutex;
    std::atomic<bool>  m_hasNewFrame { false };

    // Decode thread
    std::thread       m_thread;
    std::atomic<bool> m_running { false };

    // Frame timing
    double m_frameDuration { 1.0 / 30.0 };
};

} // namespace wallpaper
