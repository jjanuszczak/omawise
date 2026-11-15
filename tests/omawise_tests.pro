QT += core gui testlib quick quickcontrols2 network
CONFIG += c++17 console testcase link_pkgconfig qtquickcompiler
DEFINES += OMAWISE_TESTING
PKGCONFIG += libsecret-1
CONFIG -= app_bundle
TEMPLATE = app
TARGET = omawise_tests

INCLUDEPATH += ../src
HEADERS += ../src/backend.h ../src/theme.h
SOURCES += omawise_tests.cpp ../src/backend.cpp ../src/theme.cpp
RESOURCES += ../src/resources.qrc
