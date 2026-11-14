QT += core gui testlib
CONFIG += c++17 console testcase
CONFIG -= app_bundle
TEMPLATE = app
TARGET = rwreader_tests

INCLUDEPATH += ../src
HEADERS += ../src/backend.h
SOURCES += rwreader_tests.cpp ../src/backend.cpp
