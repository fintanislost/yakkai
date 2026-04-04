#include "VideoFrameDecoder.hpp"
#include "Utils/Logging.h"

#ifdef YAKKAI_HAS_FFMPEG

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <chrono>
#include <cstring>

namespace wallpaper {

struct AvioMemCtx {
    const uint8_t* data;
    size_t size;
    size_t pos;
};

struct VideoFrameDecoder::FFmpegState {
    AVFormatContext* fmt_ctx { nullptr };
    AVCodecContext*  codec_ctx { nullptr };
    SwsContext*      sws_ctx { nullptr };
    AVFrame*         frame { nullptr };
    AVFrame*         rgba_frame { nullptr };
    AVPacket*        pkt { nullptr };
    uint8_t*         avio_buffer { nullptr };
    AVIOContext*     avio_ctx { nullptr };
    int              video_stream_idx { -1 };
    AvioMemCtx       mem_ctx {};

    ~FFmpegState() {
        if (rgba_frame) av_frame_free(&rgba_frame);
        if (frame) av_frame_free(&frame);
        if (pkt) av_packet_free(&pkt);
        if (sws_ctx) sws_freeContext(sws_ctx);
        if (codec_ctx) avcodec_free_context(&codec_ctx);
        if (fmt_ctx) {
            avformat_close_input(&fmt_ctx);
        } else if (avio_ctx) {
            avio_context_free(&avio_ctx);
        }
    }
};

static int avio_read_callback(void* opaque, uint8_t* buf, int buf_size) {
    auto* ctx = static_cast<AvioMemCtx*>(opaque);
    if (ctx->pos >= ctx->size) return AVERROR_EOF;
    size_t remaining = ctx->size - ctx->pos;
    size_t to_read = std::min(remaining, static_cast<size_t>(buf_size));
    memcpy(buf, ctx->data + ctx->pos, to_read);
    ctx->pos += to_read;
    return static_cast<int>(to_read);
}

static int64_t avio_seek_callback(void* opaque, int64_t offset, int whence) {
    auto* ctx = static_cast<AvioMemCtx*>(opaque);
    if (whence == AVSEEK_SIZE) return static_cast<int64_t>(ctx->size);
    int64_t new_pos;
    switch (whence) {
    case SEEK_SET: new_pos = offset; break;
    case SEEK_CUR: new_pos = static_cast<int64_t>(ctx->pos) + offset; break;
    case SEEK_END: new_pos = static_cast<int64_t>(ctx->size) + offset; break;
    default: return AVERROR(EINVAL);
    }
    if (new_pos < 0 || new_pos > static_cast<int64_t>(ctx->size)) return AVERROR(EINVAL);
    ctx->pos = static_cast<size_t>(new_pos);
    return new_pos;
}

VideoFrameDecoder::VideoFrameDecoder(const uint8_t* data, size_t size, int expected_width, int expected_height)
    : m_data(data, data + size)
    , m_ff(std::make_unique<FFmpegState>())
    , m_width(expected_width)
    , m_height(expected_height) {
    m_valid = OpenVideo();
    if (m_valid) {
        LOG_INFO("video decoder opened: %dx%d duration=%.1fs frameDuration=%.4fs",
                 m_width, m_height,
                 m_ff->fmt_ctx->duration > 0 ? m_ff->fmt_ctx->duration / (double)AV_TIME_BASE : 0.0,
                 m_frameDuration);
    }
}

VideoFrameDecoder::~VideoFrameDecoder() {
    Stop();
}

bool VideoFrameDecoder::OpenVideo() {
    constexpr size_t avio_buf_size = 32768;

    m_ff->mem_ctx = { m_data.data(), m_data.size(), 0 };

    m_ff->avio_buffer = static_cast<uint8_t*>(av_malloc(avio_buf_size));
    if (! m_ff->avio_buffer) return false;

    m_ff->avio_ctx = avio_alloc_context(
        m_ff->avio_buffer, avio_buf_size, 0, &m_ff->mem_ctx,
        avio_read_callback, nullptr, avio_seek_callback);
    if (! m_ff->avio_ctx) return false;

    m_ff->fmt_ctx = avformat_alloc_context();
    if (! m_ff->fmt_ctx) return false;
    m_ff->fmt_ctx->pb = m_ff->avio_ctx;

    if (avformat_open_input(&m_ff->fmt_ctx, nullptr, nullptr, nullptr) < 0) {
        LOG_ERROR("video decoder: avformat_open_input failed");
        return false;
    }
    if (avformat_find_stream_info(m_ff->fmt_ctx, nullptr) < 0) {
        LOG_ERROR("video decoder: avformat_find_stream_info failed");
        return false;
    }

    // Find video stream
    for (unsigned i = 0; i < m_ff->fmt_ctx->nb_streams; i++) {
        if (m_ff->fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_ff->video_stream_idx = static_cast<int>(i);
            break;
        }
    }
    if (m_ff->video_stream_idx < 0) {
        LOG_ERROR("video decoder: no video stream found");
        return false;
    }

    auto* codecpar = m_ff->fmt_ctx->streams[m_ff->video_stream_idx]->codecpar;
    auto* codec = avcodec_find_decoder(codecpar->codec_id);
    if (! codec) {
        LOG_ERROR("video decoder: unsupported codec %d", static_cast<int>(codecpar->codec_id));
        return false;
    }

    m_ff->codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(m_ff->codec_ctx, codecpar);
    if (avcodec_open2(m_ff->codec_ctx, codec, nullptr) < 0) {
        LOG_ERROR("video decoder: avcodec_open2 failed");
        return false;
    }

    m_width = m_ff->codec_ctx->width;
    m_height = m_ff->codec_ctx->height;

    // Set up color conversion to RGBA
    m_ff->sws_ctx = sws_getContext(
        m_width, m_height, m_ff->codec_ctx->pix_fmt,
        m_width, m_height, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (! m_ff->sws_ctx) {
        LOG_ERROR("video decoder: sws_getContext failed");
        return false;
    }

    m_ff->frame = av_frame_alloc();
    m_ff->rgba_frame = av_frame_alloc();
    m_ff->pkt = av_packet_alloc();

    m_ff->rgba_frame->format = AV_PIX_FMT_RGBA;
    m_ff->rgba_frame->width = m_width;
    m_ff->rgba_frame->height = m_height;
    av_frame_get_buffer(m_ff->rgba_frame, 32);

    // Compute frame duration
    auto* stream = m_ff->fmt_ctx->streams[m_ff->video_stream_idx];
    if (stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0) {
        m_frameDuration = static_cast<double>(stream->avg_frame_rate.den) /
                          static_cast<double>(stream->avg_frame_rate.num);
    }

    return true;
}

VideoFrameDecoder::DataPtr VideoFrameDecoder::DecodeNextFrame() {
    while (av_read_frame(m_ff->fmt_ctx, m_ff->pkt) >= 0) {
        if (m_ff->pkt->stream_index != m_ff->video_stream_idx) {
            av_packet_unref(m_ff->pkt);
            continue;
        }

        int ret = avcodec_send_packet(m_ff->codec_ctx, m_ff->pkt);
        av_packet_unref(m_ff->pkt);
        if (ret < 0) continue;

        ret = avcodec_receive_frame(m_ff->codec_ctx, m_ff->frame);
        if (ret == AVERROR(EAGAIN)) continue;
        if (ret < 0) return nullptr;

        // Convert to RGBA
        sws_scale(m_ff->sws_ctx,
                  m_ff->frame->data, m_ff->frame->linesize,
                  0, m_height,
                  m_ff->rgba_frame->data, m_ff->rgba_frame->linesize);

        // Copy to owned buffer
        size_t frame_size = static_cast<size_t>(m_width) * m_height * 4;
        auto* buf = new uint8_t[frame_size];
        for (int y = 0; y < m_height; y++) {
            memcpy(buf + y * m_width * 4,
                   m_ff->rgba_frame->data[0] + y * m_ff->rgba_frame->linesize[0],
                   m_width * 4);
        }
        return DataPtr(buf, [](const uint8_t* p) { delete[] p; });
    }
    return nullptr;
}

void VideoFrameDecoder::SeekToStart() {
    if (av_seek_frame(m_ff->fmt_ctx, m_ff->video_stream_idx, 0, AVSEEK_FLAG_BACKWARD) < 0) {
        LOG_INFO("video decoder: seek to start failed, resetting read position");
    }
    avcodec_flush_buffers(m_ff->codec_ctx);
    m_ff->mem_ctx.pos = 0;
}

VideoFrameDecoder::DataPtr VideoFrameDecoder::DecodeFirstFrame() {
    if (! m_valid) return nullptr;
    SeekToStart();
    return DecodeNextFrame();
}

VideoFrameDecoder::DataPtr VideoFrameDecoder::TryGetFrame() {
    std::lock_guard lock(m_frameMutex);
    if (m_hasNewFrame.exchange(false)) {
        m_front = m_back;
    }
    return m_front;
}

void VideoFrameDecoder::Start() {
    if (m_running || ! m_valid) return;
    m_running = true;
    m_thread = std::thread(&VideoFrameDecoder::DecodeLoop, this);
}

void VideoFrameDecoder::Stop() {
    m_running = false;
    if (m_thread.joinable()) m_thread.join();
}

void VideoFrameDecoder::DecodeLoop() {
    SeekToStart();
    auto next_frame_time = std::chrono::steady_clock::now();
    int  consecutive_failures = 0;

    while (m_running) {
        auto now = std::chrono::steady_clock::now();
        if (now < next_frame_time) {
            std::this_thread::sleep_until(next_frame_time);
            if (! m_running) break;
        }
        next_frame_time += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(m_frameDuration));

        auto frame = DecodeNextFrame();
        if (! frame) {
            SeekToStart();
            frame = DecodeNextFrame();
        }
        if (! frame) {
            if (++consecutive_failures >= 3) {
                LOG_ERROR("video decoder: giving up after %d consecutive decode failures", consecutive_failures);
                break;
            }
            continue;
        }
        consecutive_failures = 0;

        {
            std::lock_guard lock(m_frameMutex);
            m_back = std::move(frame);
        }
        m_hasNewFrame = true;
    }
}

} // namespace wallpaper

#else // !YAKKAI_HAS_FFMPEG

namespace wallpaper {

struct AvioMemCtx {
    const uint8_t* data;
    size_t size;
    size_t pos;
};

struct VideoFrameDecoder::FFmpegState {};

VideoFrameDecoder::VideoFrameDecoder(const uint8_t*, size_t, int, int)
    : m_ff(std::make_unique<FFmpegState>()) {}
VideoFrameDecoder::~VideoFrameDecoder() { Stop(); }
bool VideoFrameDecoder::OpenVideo() { return false; }
VideoFrameDecoder::DataPtr VideoFrameDecoder::DecodeNextFrame() { return nullptr; }
VideoFrameDecoder::DataPtr VideoFrameDecoder::DecodeFirstFrame() { return nullptr; }
VideoFrameDecoder::DataPtr VideoFrameDecoder::TryGetFrame() { return nullptr; }
void VideoFrameDecoder::SeekToStart() {}
void VideoFrameDecoder::Start() {}
void VideoFrameDecoder::Stop() { m_running = false; if (m_thread.joinable()) m_thread.join(); }
void VideoFrameDecoder::DecodeLoop() {}

} // namespace wallpaper

#endif // YAKKAI_HAS_FFMPEG
