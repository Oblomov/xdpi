lessThan(QT_MAJOR_VERSION, 5) {
	error("qtdpitest requires Qt version 5, $$[QT_VERSION] found")
}

TEMPLATE = app
VERSION = 1.0

TARGET = qtdpi

CONFIG += qt
CONFIG += debug warn_on

CONFIG += c++11 strict_c++

QMAKE_CXXFLAGS += -Wextra

SOURCES += main.cpp
