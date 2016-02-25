QT += widgets
HEADERS = src/explorer.hpp
SOURCES = src/explorer.cpp
RESOURCES = explorer.qrc
ICON = sample.icns
RC_FILE = explorer.rc

unix:!macx {
 LIBS +=  -lnetcdf
}

macx: {
 INCLUDEPATH += /usr/local/include
 LIBS += /usr/local/lib/libnetcdf.a
 LIBS += /usr/local/lib/libhdf5.a
 LIBS += /usr/local/lib/libhdf5_hl.a
 LIBS += /usr/local/lib/libsz.a
 LIBS += -lcurl -lz
}

