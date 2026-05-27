#include "SceneBackend.hpp"

#include <QtGlobal>
#include <QtCore/QObject>
#include <QtCore/QDir>
#include <QtCore/QThread>

#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtGui/QOpenGLContext>
#include <QtQuick/QQuickWindow>

#include <QtGui/QOffscreenSurface>
#include <QtQuick/QSGSimpleTextureNode>
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
#include <QSGTexture>
#endif

#include <clocale>
#include <atomic>
#include <array>
#include <functional>

#include "glExtra.hpp"
#include "SceneWallpaper.hpp"
#include "SceneWallpaperSurface.hpp"
#include "Type.hpp"
#include "Utils/Platform.hpp"
#include <cstdio>
#include <qobjectdefs.h>
#include <unistd.h>

using namespace scenebackend;

Q_LOGGING_CATEGORY(wekdeScene, "wekde.scene")

#define _Q_INFO(fmt, ...) qCInfo(wekdeScene, fmt, __VA_ARGS__)

namespace
{
bool ShouldLogHighFrequency(std::atomic<uint64_t>& counter,
                            uint64_t               initial_burst = 6,
                            uint64_t               interval = 180) {
    const uint64_t count = counter.fetch_add(1, std::memory_order_relaxed) + 1;
    return count <= initial_burst || (count % interval) == 0;
}

std::atomic<uint64_t> s_release_queue_log_counter { 0 };

void* get_proc_address(const char* name) {
    QOpenGLContext* glctx = QOpenGLContext::currentContext();
    if (! glctx) return nullptr;

    return reinterpret_cast<void*>(glctx->getProcAddress(QByteArray(name)));
}

QSGTexture* createTextureFromGl(uint32_t handle, QSize size, QQuickWindow* window) {
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    return QNativeInterface::QSGOpenGLTexture::fromNative(handle, window, size);
#elif (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    return window->createTextureFromNativeObject(
        QQuickWindow::NativeObjectTexture, &handle, 0, size);
#else
    return window->createTextureFromId(handle, size);
#endif
}

QSGTexture* createFallbackTexture(QQuickWindow* window) {
    if (window == nullptr) {
        return nullptr;
    }

    QImage image(QSize(64, 64), QImage::Format_RGBA8888);
    image.fill(Qt::black);
    return window->createTextureFromImage(image);
}

wallpaper::FillMode ToWPFillMode(int fillMode) {
    switch ((SceneObject::FillMode)fillMode) {
    case SceneObject::FillMode::STRETCH: return wallpaper::FillMode::STRETCH;
    case SceneObject::FillMode::ASPECTFIT: return wallpaper::FillMode::ASPECTFIT;
    case SceneObject::FillMode::ASPECTCROP:
    default: return wallpaper::FillMode::ASPECTCROP;
    }
}

} // namespace

using sp_scene_t = std::shared_ptr<wallpaper::SceneWallpaper>;

namespace scenebackend
{

class TextureNode : public QObject, public QSGSimpleTextureNode {
    Q_OBJECT
public:
    typedef std::function<QSGTexture*(QQuickWindow*)> EatFrameOp;
    TextureNode(QQuickWindow* window, sp_scene_t scene, bool valid, EatFrameOp eatFrameOp)
        : m_texture(nullptr),
          m_scene(scene),
          m_enable_valid(valid),
          m_eatFrameOp(eatFrameOp),
          m_window(window),
          m_first_frame(false) {
        // Use a real Qt-created placeholder until the external Vulkan frame arrives.
        // Wrapping GL texture id 0 can produce an invalid QSGTexture and crash
        // QSGSimpleTextureNode::setTexture() on Plasma/Qt 6.
        m_texture      = createFallbackTexture(window);
        m_init_texture = m_texture;
        if (m_texture != nullptr) {
            setTexture(m_texture);
            setFiltering(QSGTexture::Linear);
        } else {
            qCWarning(wekdeScene, "failed to create initial fallback texture");
        }
        setOwnsTexture(false);
    }

    ~TextureNode() override {
        for (auto& item : texs_map) {
            auto& exh = item.second;
            m_glex.deleteSemaphore(exh.ready_semaphore);
            m_glex.deleteSemaphore(exh.release_semaphore);
            m_glex.deleteTexture(exh.gltex);
            delete exh.qsg;
        }
        delete m_init_texture;
        emit nodeDestroyed();
        _Q_INFO("Destroy texnode", "");
    }

    // only at qt render thread
    bool initGl() { return m_glex.init(get_proc_address); }

    // after gl, can run at any thread
    void initVulkan(uint16_t w, uint16_t h) {
        wallpaper::RenderInitInfo info;
        info.enable_valid_layer = m_enable_valid;
        info.offscreen          = true;
        info.offscreen_tiling   = m_glex.tiling();
        info.uuid               = m_glex.uuid();
        info.width              = w;
        info.height             = h;
        info.redraw_callback    = [this]() {
            Q_EMIT this->redraw();
        };

        auto cb = std::make_shared<wallpaper::FirstFrameCallback>([this]() {
            m_first_frame = true;
            Q_EMIT this->redraw();
        });
        m_scene->setPropertyObject(wallpaper::PROPERTY_FIRST_FRAME_CALLBACK, cb);
        // this send to looper, not in this thread
        m_scene->initVulkan(info);
    }

    void emitSceneFirstFrame() { Q_EMIT sceneFirstFrame(); }
signals:
    void textureInUse();
    void nodeDestroyed();
    void redraw();
    void sceneFirstFrame();

public slots:
    void newTexture() {
        if (! m_scene->inited() || m_scene->exSwapchain() == nullptr) return;

        if (m_current_tex_id >= 0) {
            auto current = texs_map.find(m_current_tex_id);
            if (current != texs_map.end() && ! current->second.explicit_sync) {
                // Fallback path when explicit semaphore interop is unavailable for a texture.
                m_glex.finish();
            }
        }

        wallpaper::ExHandle* exh = m_scene->exSwapchain()->eatFrame();
        if (exh != nullptr) {
            int id = exh->id();
            if (texs_map.count(id) == 0) {
                _Q_INFO("receive external texture(%dx%d) from fd: %d",
                        exh->width,
                        exh->height,
                        exh->fd);
                ExTex ex_tex;
                int   fd    = exh->fd;
                uint  gltex = m_glex.genExTexture(*exh);

                if (gltex == 0) {
                    qCWarning(wekdeScene,
                              "failed to import external texture id=%d fd=%d size=%dx%d",
                              id,
                              fd,
                              exh->width,
                              exh->height);
                    close(fd);
                    return;
                }

                ex_tex.gltex = gltex;
                ex_tex.qsg   = createTextureFromGl(gltex, QSize(exh->width, exh->height), m_window);
                ex_tex.explicit_sync = m_glex.importExSemaphores(*exh,
                                                                 ex_tex.ready_semaphore,
                                                                 ex_tex.release_semaphore);
                ex_tex.sync_state = exh->sync_state;
                texs_map[id] = ex_tex;
            }
            auto& newtex = texs_map.at(id);
            if (newtex.explicit_sync) {
                if (! m_glex.waitSemaphoreTexture(newtex.ready_semaphore, newtex.gltex)) {
                    qCWarning(wekdeScene,
                              "failed waiting on external ready semaphore for texture id=%d",
                              id);
                    newtex.explicit_sync = false;
                    m_glex.deleteSemaphore(newtex.ready_semaphore);
                    m_glex.deleteSemaphore(newtex.release_semaphore);
                    newtex.ready_semaphore   = 0;
                    newtex.release_semaphore = 0;
                }
            }
            if (newtex.qsg != nullptr)
                m_texture = newtex.qsg;
            else
                m_texture = m_init_texture;

            newtex.pending_release = newtex.explicit_sync;
            m_current_tex_id       = id;
            setTexture(m_texture);
            markDirty(DirtyMaterial);
            Q_EMIT textureInUse();

            bool expected = true;
            if (m_first_frame.compare_exchange_strong(expected, false)) {
                Q_EMIT sceneFirstFrame();
            }
        }
    }

    void frameRendered() {
        if (m_current_tex_id < 0) return;

        auto current = texs_map.find(m_current_tex_id);
        if (current == texs_map.end()) return;

        auto& ex_tex = current->second;
        if (! ex_tex.explicit_sync || ! ex_tex.pending_release) return;

        if (! m_glex.signalSemaphoreTexture(ex_tex.release_semaphore, ex_tex.gltex)) {
            qCWarning(wekdeScene,
                      "failed signaling external release semaphore for texture id=%d",
                      m_current_tex_id);
            m_glex.finish();
        } else if (ShouldLogHighFrequency(s_release_queue_log_counter)) {
            qCInfo(wekdeScene,
                   "queued external release for texture id=%d semaphore=%u",
                   m_current_tex_id,
                   ex_tex.release_semaphore);
        }

        if (ex_tex.sync_state) ex_tex.sync_state->release_ready.store(true);
        ex_tex.pending_release = false;
    }

private:
    sp_scene_t m_scene;
    bool       m_enable_valid;

    QSGTexture*       m_init_texture;
    QSGTexture*       m_texture;
    EatFrameOp        m_eatFrameOp;
    QQuickWindow*     m_window;
    std::atomic<bool> m_first_frame;

    GlExtra m_glex;

    struct ExTex {
        uint                                     gltex { 0 };
        QSGTexture*                              qsg { nullptr };
        uint                                     ready_semaphore { 0 };
        uint                                     release_semaphore { 0 };
        bool                                     explicit_sync { false };
        bool                                     pending_release { false };
        std::shared_ptr<wallpaper::ExHandleSyncState> sync_state;
    };
    std::unordered_map<int, ExTex> texs_map;
    int                            m_current_tex_id { -1 };
};

} // namespace scenebackend

SceneObject::SceneObject(QQuickItem* parent)
    : QQuickItem(parent), m_scene(std::make_shared<wallpaper::SceneWallpaper>()) {
    setFlag(ItemHasContents, true);
    m_scene->init();
    m_scene->setPropertyString(wallpaper::PROPERTY_CACHE_PATH, GetDefaultCachePath());
}

SceneObject::~SceneObject() { _Q_INFO("Destroy sceneobject", ""); }

void SceneObject::resizeFb() {
    QSize size;
    size.setWidth(this->width());
    size.setHeight(this->height());
}

void SceneObject::reportBackendError(const QString& message) {
    if (m_reportedBackendError) {
        return;
    }

    m_reportedBackendError = true;
    qCWarning(wekdeScene).noquote() << message;
    Q_EMIT backendError(message);
}

QSGNode* SceneObject::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    TextureNode* node = static_cast<TextureNode*>(oldNode);
    if (! node) {
        QQuickWindow* quickWindow = window();
        node = new TextureNode(quickWindow, m_scene, m_enable_valid, [this](QQuickWindow* window) {
            return (QSGTexture*)nullptr;
        });

        if (quickWindow == nullptr) {
            reportBackendError(
                QStringLiteral("Yakkai scene backend cannot start without a Qt Quick window."));
            return node;
        }

        if (QOpenGLContext::currentContext() == nullptr) {
            reportBackendError(QStringLiteral(
                "Yakkai scene backend cannot start because Plasma is not rendering this wallpaper with a current OpenGL context."));
            return node;
        }

        if (node->initGl()) {
            node->initVulkan(width()*quickWindow->devicePixelRatio(),
                             height()*quickWindow->devicePixelRatio());

            connect(
                node, &TextureNode::redraw, quickWindow, &QQuickWindow::update, Qt::QueuedConnection);
            connect(quickWindow,
                    &QQuickWindow::beforeRendering,
                    node,
                    &TextureNode::newTexture,
                    Qt::DirectConnection);
            connect(quickWindow,
                    &QQuickWindow::afterRendering,
                    node,
                    &TextureNode::frameRendered,
                    Qt::DirectConnection);
            connect(node, &TextureNode::sceneFirstFrame, this, &SceneObject::firstFrame);
        } else {
            reportBackendError(QStringLiteral(
                "Yakkai scene backend failed to initialize OpenGL/GLAD for external scene textures."));
        }
    }

    node->setRect(boundingRect());
    return node;
}

#define SET_PROPERTY(type, name, value) m_scene->setProperty##type(name, value);

void SceneObject::setScenePropertyQurl(std::string_view name, QUrl value) {
    auto str_value = QDir::toNativeSeparators(value.toLocalFile()).toStdString();
    SET_PROPERTY(String, name, str_value);
}
// qobject

QUrl SceneObject::source() const { return m_source; }
QUrl SceneObject::assets() const { return m_assets; }
QString SceneObject::scenePropertiesJson() const { return m_scenePropertiesJson; }
QString SceneObject::debugEffectCapturesPath() const { return m_debugEffectCapturesPath; }
QString SceneObject::debugEffectCaptureCommand() const { return m_debugEffectCaptureCommand; }

int   SceneObject::fps() const { return m_fps; }
int   SceneObject::fillMode() const { return m_fillMode; }
float SceneObject::speed() const { return m_speed; }
float SceneObject::volume() const { return m_volume; }
bool  SceneObject::muted() const { return m_muted; }

void SceneObject::setSource(const QUrl& source) {
    if (source == m_source) return;
    m_source = source;
    setScenePropertyQurl(wallpaper::PROPERTY_SOURCE, m_source);
    Q_EMIT sourceChanged();
}

void SceneObject::setAssets(const QUrl& assets) {
    if (m_assets == assets) return;
    m_assets = assets;
    setScenePropertyQurl(wallpaper::PROPERTY_ASSETS, m_assets);
}

void SceneObject::setScenePropertiesJson(const QString& value) {
    if (m_scenePropertiesJson == value) return;
    m_scenePropertiesJson = value;
    SET_PROPERTY(String,
                 wallpaper::PROPERTY_SCENE_PROPERTIES_JSON,
                 m_scenePropertiesJson.toStdString());
    Q_EMIT scenePropertiesJsonChanged();
}

void SceneObject::setDebugEffectCapturesPath(const QString& value) {
    if (m_debugEffectCapturesPath == value) return;
    m_debugEffectCapturesPath = value;
    SET_PROPERTY(String,
                 wallpaper::PROPERTY_DEBUG_EFFECT_CAPTURES,
                 m_debugEffectCapturesPath.toStdString());
    Q_EMIT debugEffectCapturesPathChanged();
}

void SceneObject::setDebugEffectCaptureCommand(const QString& value) {
    if (m_debugEffectCaptureCommand == value) return;
    m_debugEffectCaptureCommand = value;
    SET_PROPERTY(String,
                 wallpaper::PROPERTY_DEBUG_EFFECT_CAPTURE_COMMAND,
                 m_debugEffectCaptureCommand.toStdString());
    Q_EMIT debugEffectCaptureCommandChanged();
}

void SceneObject::setFps(int value) {
    if (m_fps == value) return;
    m_fps = value;
    SET_PROPERTY(Int32, wallpaper::PROPERTY_FPS, value);
    Q_EMIT fpsChanged();
}
void SceneObject::setFillMode(int value) {
    if (m_fillMode == value) return;
    m_fillMode = value;
    SET_PROPERTY(Int32, wallpaper::PROPERTY_FILLMODE, (int32_t)ToWPFillMode(value));
    Q_EMIT fillModeChanged();
}
void SceneObject::setSpeed(float value) {
    if (m_speed == value) return;
    m_speed = value;
    SET_PROPERTY(Float, wallpaper::PROPERTY_SPEED, value);
    Q_EMIT speedChanged();
}
void SceneObject::setVolume(float value) {
    if (m_volume == value) return;
    m_volume = value;
    SET_PROPERTY(Float, wallpaper::PROPERTY_VOLUME, value);
    Q_EMIT volumeChanged();
}
void SceneObject::setMuted(bool value) {
    if (m_muted == value) return;
    m_muted = value;
    SET_PROPERTY(Bool, wallpaper::PROPERTY_MUTED, value);
}

void SceneObject::play() { m_scene->play(); }
void SceneObject::pause() { m_scene->pause(); }

bool SceneObject::vulkanValid() const { return m_enable_valid; }
void SceneObject::enableVulkanValid() { m_enable_valid = true; }
void SceneObject::enableGenGraphviz() { SET_PROPERTY(Bool, wallpaper::PROPERTY_GRAPHIVZ, true); }

void SceneObject::setAcceptMouse(bool value) {
    if (value)
        setAcceptedMouseButtons(Qt::LeftButton);
    else
        setAcceptedMouseButtons(Qt::NoButton);
}

void SceneObject::setAcceptHover(bool value) { setAcceptHoverEvents(value); }

void SceneObject::mousePressEvent(QMouseEvent* event) {}
void SceneObject::mouseMoveEvent(QMouseEvent* event) {
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    auto pos = event->position();
#else
    auto pos = event->localPos();
#endif
    m_scene->mouseInput(pos.x() / width(), pos.y() / height());
}

void SceneObject::hoverMoveEvent(QHoverEvent* event) {
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    auto pos = event->position();
#else
    auto pos = event->posF();
#endif
    m_scene->mouseInput(pos.x() / width(), pos.y() / height());
}

std::string SceneObject::GetDefaultCachePath() {
    return wallpaper::platform::GetCachePath(CACHE_DIR);
}

#include "SceneBackend.moc"
