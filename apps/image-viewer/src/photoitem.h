#pragma once

#include <QImage>
#include <QQuickItem>
#include <atomic>

class QSGImageNode;
class QSGTexture;

class PhotoItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QImage photo READ photo WRITE setPhoto NOTIFY photoChanged)
    Q_PROPERTY(uint presentationId MEMBER m_presentationId)

public:
    explicit PhotoItem(QQuickItem *parent = nullptr);
    QImage photo() const { return m_photo; }
    void setPhoto(const QImage &photo);

signals:
    void photoChanged();
    void presentationFailed();

protected:
    virtual QSGTexture *createPhotoTexture();
    virtual QSGImageNode *createPhotoNode();
    void geometryChange(const QRectF &current, const QRectF &previous) override;
    QSGNode *updatePaintNode(QSGNode *node, UpdatePaintNodeData *) override;

private:
    QImage m_photo;
    qint64 m_failedKey = 0;
    uint m_presentationId = 0;
    std::atomic<uint> m_pendingPresentation{0};
    QMetaObject::Connection m_swapConnection;
};
