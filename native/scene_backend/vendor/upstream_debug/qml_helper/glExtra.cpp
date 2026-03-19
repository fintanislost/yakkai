#include "glExtra.hpp"
#include <glad/glad.h>
#include <stdio.h>
#include <unistd.h>
#include <atomic>
#include <vector>
#include "Utils/Logging.h"

using namespace wallpaper;

#define CHECK_GL_ERROR_IF_DEBUG() CheckGlError(__SHORT_FILE__, __FUNCTION__, __LINE__);

namespace
{
bool ShouldLogHighFrequency(std::atomic<uint64_t>& counter,
                            uint64_t               initial_burst = 6,
                            uint64_t               interval = 180) {
    const uint64_t count = counter.fetch_add(1, std::memory_order_relaxed) + 1;
    return count <= initial_burst || (count % interval) == 0;
}

std::atomic<uint64_t> s_wait_ready_begin_log_counter { 0 };
std::atomic<uint64_t> s_wait_ready_done_log_counter { 0 };
std::atomic<uint64_t> s_signal_release_begin_log_counter { 0 };
std::atomic<uint64_t> s_signal_release_done_log_counter { 0 };

inline char const* const GLErrorToStr(GLenum const err) noexcept {
#define Enum_GLError(glerr) \
    case glerr: return #glerr;

    switch (err) {
        // opengl 2
        Enum_GLError(GL_NO_ERROR);
        Enum_GLError(GL_INVALID_ENUM);
        Enum_GLError(GL_INVALID_VALUE);
        Enum_GLError(GL_INVALID_OPERATION);
        Enum_GLError(GL_OUT_OF_MEMORY);
        // opengl 3 errors (1)
        Enum_GLError(GL_INVALID_FRAMEBUFFER_OPERATION);
    default: return "Unknown GLError";
    }
#undef Enum_GLError
}

inline void CheckGlError(const char* file, const char* func, int line) {
    int err = glGetError();
    if (err != 0) {
        WallpaperLog(LOGLEVEL_ERROR, file, line, "%s(%d) at %s", GLErrorToStr(err), err, func);
    }
}

inline bool DrainGlErrors(const char* file, const char* func, int line, const char* stage) {
    bool had_error = false;
    for (GLenum err = glGetError(); err != GL_NO_ERROR; err = glGetError()) {
        had_error = true;
        WallpaperLog(LOGLEVEL_ERROR,
                     file,
                     line,
                     "%s(%d) at %s stage=%s",
                     GLErrorToStr(err),
                     err,
                     func,
                     stage);
    }
    return had_error;
}
} // namespace

class GlExtra::impl {
public:
    bool                                       test;
    std::array<std::uint8_t, GL_UUID_SIZE_EXT> uuid;
};

GlExtra::GlExtra(): pImpl(std::make_unique<impl>()) {}
GlExtra::~GlExtra() {}

static std::array<std::uint8_t, GL_UUID_SIZE_EXT> getUUID() {
    std::array<std::uint8_t, GL_UUID_SIZE_EXT> result {};
    int32_t num_device = 0;
    glGetIntegerv(GL_NUM_DEVICE_UUIDS_EXT, &num_device);
    DrainGlErrors(__SHORT_FILE__, __FUNCTION__, __LINE__, "query-device-uuid-count");

    if (num_device <= 0) {
        LOG_ERROR("gl: no device UUIDs reported by the current GL context");
        return result;
    }

    GLubyte uuid[GL_UUID_SIZE_EXT] = { 0 };
    glGetUnsignedBytei_vEXT(GL_DEVICE_UUID_EXT, 0, uuid);
    if (DrainGlErrors(__SHORT_FILE__, __FUNCTION__, __LINE__, "query-device-uuid")) {
        LOG_ERROR("gl: failed to query current device UUID");
        return result;
    }

    std::copy(std::begin(uuid), std::end(uuid), result.begin());
    return result;
}

bool GlExtra::init(void* get_proc_address(const char*)) {
    do {
        if (inited) break;
        if (! gladLoadGLLoader((GLADloadproc)get_proc_address)) {
            LOG_ERROR("gl: Failed to initialize GLAD");
            break;
        }
        LOG_INFO("gl: OpenGL version %d.%d loaded", GLVersion.major, GLVersion.minor);
        LOG_INFO("gl: ext memory_object=%d memory_object_fd=%d semaphore=%d semaphore_fd=%d",
                 GLAD_GL_EXT_memory_object,
                 GLAD_GL_EXT_memory_object_fd,
                 GLAD_GL_EXT_semaphore,
                 GLAD_GL_EXT_semaphore_fd);
        if (! (GLAD_GL_EXT_memory_object && GLAD_GL_EXT_memory_object_fd)) {
            LOG_ERROR("gl: required memory object FD import extensions are not available");
            break;
        }
        bool is_low_gl = ! GLAD_GL_VERSION_4_2 && ! GLAD_GL_ES_VERSION_3_0;
        if (is_low_gl) {
            LOG_INFO("gl: Low opengl version, may not work properly");
        }
        pImpl->uuid = getUUID();

        std::string gl_verdor_name { (const char*)glGetString(GL_VENDOR) };
        LOG_INFO("gl: OpenGL vendor string: %s", gl_verdor_name.c_str());

        if (! is_low_gl) {
            int              num { 0 };
            std::vector<int> tex_tilings;
            glGetInternalformativ(GL_TEXTURE_2D, GL_RGBA8, GL_NUM_TILING_TYPES_EXT, 1, &num);
            if (num <= 0) {
                LOG_ERROR("gl: can't get texture tiling support info");
                break;
            }
            num = std::min(num, 2);
            tex_tilings.resize(num);

            glGetInternalformativ(GL_TEXTURE_2D,
                                  GL_RGBA8,
                                  GL_TILING_TYPES_EXT,
                                  tex_tilings.size(),
                                  tex_tilings.data());
            CHECK_GL_ERROR_IF_DEBUG();

            bool support_optimal { false }, support_linear { false };
            for (auto& tiling : tex_tilings) {
                if (tiling == GL_OPTIMAL_TILING_EXT) {
                    support_optimal = true;
                } else if (tiling == GL_LINEAR_TILING_EXT) {
                    support_linear = true;
                }
            }
            if (! support_optimal && ! support_linear) {
                LOG_ERROR("gl: no supported tiling mode");
                break;
            }

            if (support_optimal) {
                m_tiling = wallpaper::TexTiling::OPTIMAL;
            } else if (support_linear) {
                m_tiling = wallpaper::TexTiling::LINEAR;
            }

            // linear, fix for amd
            // https://gitlab.freedesktop.org/mesa/mesa/-/issues/2456
            if (support_linear && gl_verdor_name.find("AMD") != std::string::npos) {
                m_tiling = wallpaper::TexTiling::LINEAR;
            }
        }
        if (m_tiling == wallpaper::TexTiling::OPTIMAL) {
            LOG_INFO("gl: external tex using optimal tiling");
        } else {
            LOG_INFO("gl: external tex using linear tiling");
        }

        inited = true;
    } while (false);
    return inited;
}

std::span<const std::uint8_t> GlExtra::uuid() const { return pImpl->uuid; }

TexTiling GlExtra::tiling() const { return m_tiling; }

uint GlExtra::genExTexture(ExHandle& handle) {
    uint memobject, tex;
    DrainGlErrors(__SHORT_FILE__, __FUNCTION__, __LINE__, "before-external-import");
    LOG_INFO("gl: importing external texture id=%d fd=%d size=%zu extent=%dx%d tiling=%s",
             handle.id(),
             handle.fd,
             handle.size,
             handle.width,
             handle.height,
             m_tiling == TexTiling::OPTIMAL ? "optimal" : "linear");

    if (! glCreateMemoryObjectsEXT || ! glImportMemoryFdEXT || ! glTexStorageMem2DEXT) {
        LOG_ERROR("gl: required memory object functions are unavailable");
        return 0;
    }

    glCreateMemoryObjectsEXT(1, &memobject);
    if (DrainGlErrors(__SHORT_FILE__, __FUNCTION__, __LINE__, "create-memory-object")) {
        return 0;
    }
    if (! glIsMemoryObjectEXT(memobject)) {
        LOG_ERROR("gl: created memory object handle is invalid");
        glDeleteMemoryObjectsEXT(1, &memobject);
        return 0;
    }

    glImportMemoryFdEXT(memobject, handle.size, GL_HANDLE_TYPE_OPAQUE_FD_EXT, handle.fd);
    if (DrainGlErrors(__SHORT_FILE__, __FUNCTION__, __LINE__, "import-memory-fd")) {
        glDeleteMemoryObjectsEXT(1, &memobject);
        return 0;
    }

    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    if (DrainGlErrors(__SHORT_FILE__, __FUNCTION__, __LINE__, "create-bind-texture")) {
        glDeleteTextures(1, &tex);
        glDeleteMemoryObjectsEXT(1, &memobject);
        return 0;
    }

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_TILING_EXT,
        (m_tiling == TexTiling::OPTIMAL ? GL_OPTIMAL_TILING_EXT : GL_LINEAR_TILING_EXT));
    if (DrainGlErrors(__SHORT_FILE__, __FUNCTION__, __LINE__, "set-texture-tiling")) {
        glDeleteTextures(1, &tex);
        glDeleteMemoryObjectsEXT(1, &memobject);
        return 0;
    }

    // Imported textures only expose a single level. Leaving the default mipmap
    // minification filter makes the texture incomplete and Qt Quick samples black.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    if (DrainGlErrors(__SHORT_FILE__, __FUNCTION__, __LINE__, "set-texture-sampling")) {
        glDeleteTextures(1, &tex);
        glDeleteMemoryObjectsEXT(1, &memobject);
        return 0;
    }

    glTexStorageMem2DEXT(GL_TEXTURE_2D, 1, GL_RGBA8, handle.width, handle.height, memobject, 0);
    if (DrainGlErrors(__SHORT_FILE__, __FUNCTION__, __LINE__, "storage-mem-2d")) {
        glDeleteTextures(1, &tex);
        glDeleteMemoryObjectsEXT(1, &memobject);
        return 0;
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteMemoryObjectsEXT(1, &memobject);
    handle.fd = -1;
    LOG_INFO("gl: imported external texture id=%d gltex=%u", handle.id(), tex);
    return tex;
}

bool GlExtra::supportsExternalSemaphoreInterop() const {
    return inited && GLAD_GL_EXT_semaphore && GLAD_GL_EXT_semaphore_fd &&
           glGenSemaphoresEXT && glDeleteSemaphoresEXT && glImportSemaphoreFdEXT &&
           glWaitSemaphoreEXT && glSignalSemaphoreEXT;
}

bool GlExtra::importExSemaphores(ExHandle& handle, uint& ready_semaphore, uint& release_semaphore) {
    ready_semaphore   = 0;
    release_semaphore = 0;

    if (! supportsExternalSemaphoreInterop()) {
        LOG_INFO("gl: external semaphore interop unavailable, using fallback sync");
        return false;
    }
    if (handle.ready_fd < 0 || handle.release_fd < 0) {
        LOG_ERROR("gl: missing exported semaphore fds for external texture id=%d", handle.id());
        return false;
    }

    DrainGlErrors(__SHORT_FILE__, __FUNCTION__, __LINE__, "before-import-semaphore-fds");
    glGenSemaphoresEXT(1, &ready_semaphore);
    glGenSemaphoresEXT(1, &release_semaphore);
    if (DrainGlErrors(__SHORT_FILE__, __FUNCTION__, __LINE__, "create-semaphores")) {
        deleteSemaphore(ready_semaphore);
        deleteSemaphore(release_semaphore);
        return false;
    }

    glImportSemaphoreFdEXT(ready_semaphore, GL_HANDLE_TYPE_OPAQUE_FD_EXT, handle.ready_fd);
    if (DrainGlErrors(__SHORT_FILE__, __FUNCTION__, __LINE__, "import-ready-semaphore-fd")) {
        close(handle.ready_fd);
        close(handle.release_fd);
        deleteSemaphore(ready_semaphore);
        deleteSemaphore(release_semaphore);
        ready_semaphore   = 0;
        release_semaphore = 0;
        return false;
    }
    handle.ready_fd = -1;

    glImportSemaphoreFdEXT(release_semaphore, GL_HANDLE_TYPE_OPAQUE_FD_EXT, handle.release_fd);
    if (DrainGlErrors(__SHORT_FILE__, __FUNCTION__, __LINE__, "import-release-semaphore-fd")) {
        close(handle.release_fd);
        deleteSemaphore(ready_semaphore);
        deleteSemaphore(release_semaphore);
        ready_semaphore   = 0;
        release_semaphore = 0;
        return false;
    }
    handle.release_fd = -1;

    LOG_INFO("gl: imported external semaphores for texture id=%d ready=%u release=%u",
             handle.id(),
             ready_semaphore,
             release_semaphore);
    return true;
}

bool GlExtra::waitSemaphoreTexture(uint semaphore, uint texture) {
    if (! semaphore || ! texture) return false;

    const GLenum layout = GL_LAYOUT_GENERAL_EXT;
    if (ShouldLogHighFrequency(s_wait_ready_begin_log_counter)) {
        LOG_INFO("gl: waiting on external ready semaphore=%u texture=%u", semaphore, texture);
    }
    DrainGlErrors(__SHORT_FILE__, __FUNCTION__, __LINE__, "before-wait-semaphore-texture");
    glWaitSemaphoreEXT(semaphore, 0, nullptr, 1, &texture, &layout);
    if (DrainGlErrors(__SHORT_FILE__, __FUNCTION__, __LINE__, "wait-semaphore-texture")) {
        return false;
    }
    if (ShouldLogHighFrequency(s_wait_ready_done_log_counter)) {
        LOG_INFO("gl: wait on external ready semaphore=%u completed", semaphore);
    }
    return true;
}

bool GlExtra::signalSemaphoreTexture(uint semaphore, uint texture) {
    if (! semaphore || ! texture) return false;

    const GLenum layout = GL_LAYOUT_GENERAL_EXT;
    if (ShouldLogHighFrequency(s_signal_release_begin_log_counter)) {
        LOG_INFO("gl: signaling external release semaphore=%u texture=%u", semaphore, texture);
    }
    DrainGlErrors(__SHORT_FILE__, __FUNCTION__, __LINE__, "before-signal-semaphore-texture");
    glSignalSemaphoreEXT(semaphore, 0, nullptr, 1, &texture, &layout);
    if (DrainGlErrors(__SHORT_FILE__, __FUNCTION__, __LINE__, "signal-semaphore-texture")) {
        return false;
    }
    if (ShouldLogHighFrequency(s_signal_release_done_log_counter)) {
        LOG_INFO("gl: signal for external release semaphore=%u queued", semaphore);
    }
    return true;
}

void GlExtra::deleteTexture(uint tex) {
    glDeleteTextures(1, &tex);
    CHECK_GL_ERROR_IF_DEBUG();
}

void GlExtra::deleteSemaphore(uint semaphore) {
    if (! semaphore || ! glDeleteSemaphoresEXT) return;
    glDeleteSemaphoresEXT(1, &semaphore);
    CHECK_GL_ERROR_IF_DEBUG();
}

void GlExtra::finish() {
    if (! inited) return;
    DrainGlErrors(__SHORT_FILE__, __FUNCTION__, __LINE__, "before-glFinish");
    glFinish();
    DrainGlErrors(__SHORT_FILE__, __FUNCTION__, __LINE__, "after-glFinish");
}
