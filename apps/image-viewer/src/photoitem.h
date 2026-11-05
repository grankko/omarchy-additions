#pragma once

#include <QImage>
#include <QQuickItem>

class PhotoItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QImage photo READ photo WRITE setPhoto NOTIFY photoChanged)

public:
    explicit PhotoItem(QQuickItem *parent = nullptr);
    QImage photo() const { return m_photo; }
    void setPhoto(const QImage &photo);

signals:
    void photoChanged();

protected:
    void geometryChange(const QRectF &current, const QRectF &previous) override;
    QSGNode *updatePaintNode(QSGNode *node, UpdatePaintNodeData *) override;

private:
    QImage m_photo;
};
