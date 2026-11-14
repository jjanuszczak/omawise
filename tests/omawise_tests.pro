QT += core gui testlib quick quickcontrols2
CONFIG += c++17 console testcase
CONFIG -= app_bundle
TEMPLATE = app
TARGET = omawise_tests

INCLUDEPATH += ../src
HEADERS += ../src/backend.h ../src/theme.h
SOURCES += omawise_tests.cpp ../src/backend.cpp ../src/theme.cpp
RESOURCES += ../src/resources.qrc
