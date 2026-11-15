QT += core gui qml quick quickcontrols2 dbus network
CONFIG += c++17 release link_pkgconfig qtquickcompiler
PKGCONFIG += libsecret-1
TARGET = omawise
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/theme.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/theme.cpp

RESOURCES += src/resources.qrc
