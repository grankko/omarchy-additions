#include "viewer-tests.h"
#include "photoitem.h"

#include <QQuickStyle>
#include <QtTest>

QImage ViewerTests::fixture(int width, int height)
{
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(QColor("#33aa6699"));
    image.setPixelColor(0, 0, Qt::blue);
    image.setPixelColor(width - 1, height - 1, Qt::yellow);
    return image;
}

void ViewerTests::initTestCase()
{
    QQuickStyle::setStyle("Material");
    qmlRegisterType<PhotoItem>("Omarchy.Viewer", 1, 0, "PhotoItem");
}

QTEST_MAIN(ViewerTests)
