QT += gui quick quickcontrols2 concurrent dbus
CONFIG += c++20 qtquickcompiler
TARGET = omarchy-image-viewer

include(src/viewer.pri)
SOURCES += src/main.cpp src/theme.cpp src/translator.cpp
HEADERS += src/theme.h src/translator.h
RESOURCES += src/resources.qrc
