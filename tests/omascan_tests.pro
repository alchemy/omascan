QT += core gui quick quickcontrols2 testlib dbus concurrent
CONFIG += c++17 testcase
TARGET = omascan_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += \
    ../src/backend.h \
    ../src/filepicker.h \
    ../src/pagemodel.h \
    ../src/pdfexport.h \
    ../src/portalfilepicker.h \
    ../src/scanimage.h \
    ../src/scanner.h \
    ../src/theme.h \
    ../src/translator.h

SOURCES += \
    omascan_tests.cpp \
    ../src/backend.cpp \
    ../src/pagemodel.cpp \
    ../src/pdfexport.cpp \
    ../src/portalfilepicker.cpp \
    ../src/scanimage.cpp \
    ../src/scanner.cpp \
    ../src/theme.cpp \
    ../src/translator.cpp
