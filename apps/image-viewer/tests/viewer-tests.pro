QT += testlib quickcontrols2
CONFIG += c++20 testcase qtquickcompiler
TARGET = viewer-tests

include(../src/viewer.pri)
SOURCES += viewer-tests.cpp ../src/theme.cpp
HEADERS += ../src/theme.h
RESOURCES += ../src/resources.qrc
