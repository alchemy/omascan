QT += core gui qml quick quickcontrols2 dbus concurrent

CONFIG += c++17 release
TARGET = omascan
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/cli.h \
    src/filepicker.h \
    src/pagemodel.h \
    src/pdfexport.h \
    src/portalfilepicker.h \
    src/scanimage.h \
    src/scanner.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/pagemodel.cpp \
    src/pdfexport.cpp \
    src/portalfilepicker.cpp \
    src/scanimage.cpp \
    src/scanner.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
