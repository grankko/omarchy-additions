#include "photoitem.h"
#include "trace.h"

#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGTexture>
#include <memory>

namespace {
class PhotoNode : public QSGNode
{
public:
    explicit PhotoNode(QSGImageNode *imageNode) : image(imageNode)
    {
        image->setOwnsTexture(true);
        image->setFiltering(QSGTexture::Linear);
        appendChildNode(image);
    }

    QSGImageNode *image;
    qint64 key = 0;
};
}

PhotoItem::PhotoItem(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    if (qEnvironmentVariableIsSet("IMAGE_VIEWER_TRACE")) {
        connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow *window) {
            disconnect(m_swapConnection);
            if (window) {
                m_swapConnection = connect(window, &QQuickWindow::frameSwapped, this, [this] {
                    if (const auto id = m_pendingPresentation.exchange(0))
                        viewerTrace("frame-submitted", id);
                }, Qt::DirectConnection);
            }
        });
    }
}

void PhotoItem::setPhoto(const QImage &photo)
{
    if (m_photo.cacheKey() == photo.cacheKey())
        return;
    m_photo = photo;
    update();
    emit photoChanged();
}

void PhotoItem::geometryChange(const QRectF &current, const QRectF &previous)
{
    QQuickItem::geometryChange(current, previous);
    if (current.size() != previous.size())
        update();
}

QSGTexture *PhotoItem::createPhotoTexture()
{
    return window()->createTextureFromImage(m_photo);
}

QSGImageNode *PhotoItem::createPhotoNode()
{
    return window()->createImageNode();
}

QSGNode *PhotoItem::updatePaintNode(QSGNode *previous, UpdatePaintNodeData *)
{
    if (m_photo.isNull()) {
        delete previous;
        return nullptr;
    }
    const auto key = m_photo.cacheKey();
    if (m_failedKey == key) {
        delete previous;
        return nullptr;
    }

    auto *node = static_cast<PhotoNode *>(previous);
    if (!node || node->key != key) {
        std::unique_ptr<QSGTexture> texture(createPhotoTexture());
        if (texture && !node) {
            if (auto *imageNode = createPhotoNode())
                node = new PhotoNode(imageNode);
        }
        if (!texture || !node) {
            // Never present stale pixels under the new frame's file identity.
            delete node;
            m_failedKey = key;
            QMetaObject::invokeMethod(this, [this, key] {
                if (m_photo.cacheKey() == key)
                    emit presentationFailed();
            }, Qt::QueuedConnection);
            return nullptr;
        }
        node->image->setTexture(texture.release());
        node->key = key;
        if (qEnvironmentVariableIsSet("IMAGE_VIEWER_TRACE"))
            m_pendingPresentation.store(m_presentationId);
    }
    node->image->setRect(boundingRect());
    return node;
}
