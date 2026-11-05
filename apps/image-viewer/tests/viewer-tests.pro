QT += testlib quickcontrols2
CONFIG += c++20 testcase qtquickcompiler
TARGET = viewer-tests

include(../src/viewer.pri)
SOURCES += viewer-tests.cpp imageio-tests.cpp backend-tests.cpp integration-tests.cpp qml-tests.cpp ../src/theme.cpp ../src/translator.cpp
HEADERS += viewer-tests.h ../src/theme.h ../src/translator.h
RESOURCES += ../src/resources.qrc
