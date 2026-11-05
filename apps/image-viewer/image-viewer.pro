QT += gui quick quickcontrols2 concurrent dbus
CONFIG += c++20 qtquickcompiler
TARGET = omarchy-image-viewer
!versionAtLeast(QT_VERSION, 6.12.0): error(Image Viewer requires Qt 6.12 or later)
VIEWER_VERSION = $$cat($$PWD/VERSION, lines)
DEFINES += VIEWER_VERSION=\\\"$$VIEWER_VERSION\\\"

include(src/viewer.pri)
SOURCES += src/main.cpp src/theme.cpp src/translator.cpp
HEADERS += src/theme.h src/translator.h
RESOURCES += src/resources.qrc
