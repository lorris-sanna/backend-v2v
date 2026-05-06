QT += core network websockets
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = SimulationServer
TEMPLATE = app

SOURCES += \
    Graphe.cpp \
    Voiture.cpp \
    SimulationServer.cpp \
    main_server.cpp \
    tinyxml2.cpp

HEADERS += \
    Graphe.h \
    Noeud.h \
    Voiture.h \
    SimulationServer.h \
    tinyxml2.h