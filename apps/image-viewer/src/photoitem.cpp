#include "photoitem.h"

#include <QQuickWindow>
#include <QSGImageNode>

namespace {
class PhotoNode : public QSGNode
{
public:
    explicit PhotoNode(QQuickWindow *window) : image(window->createImageNode())
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

QSGNode *PhotoItem::updatePaintNode(QSGNode *previous, UpdatePaintNodeData *)
{
    if (m_photo.isNull()) {
        delete previous;
        return nullptr;
    }
    auto *node = static_cast<PhotoNode *>(previous);
    if (!node)
        node = new PhotoNode(window());
    if (node->key != m_photo.cacheKey()) {
        auto *texture = window()->createTextureFromImage(m_photo);
        if (!texture)
            return node;
        node->image->setTexture(texture);
        node->key = m_photo.cacheKey();
    }
    node->image->setRect(boundingRect());
    return node;
}
