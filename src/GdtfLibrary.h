#pragma once
#include <QString>
#include <QList>
#include "Project.h"

struct GdtfChannelRow {
    QString attribute;
    int     coarse  = -1;
    int     fine    = -1;    // -1 = 8-bit only
    float   minDeg  = -270.f;
    float   maxDeg  =  270.f;
};

struct GdtfModeInfo {
    QString              name;
    int                  footprint = 0;
    QList<GdtfChannelRow> channels;
};

struct GdtfPreview {
    QString             path;
    QString             name;
    QString             manufacturer;
    QString             shortName;
    QString             description;
    QList<GdtfModeInfo> modes;
    bool                valid = false;
};

struct GdtfLibraryEntry {
    QString path;
    QString name;
    QString manufacturer;
};

class GdtfLibrary {
public:
    static QString                  libraryPath();
    static QList<GdtfLibraryEntry>  list();
    static bool                     importFile(const QString& srcPath, QString* outError = nullptr);
    static bool                     removeFile(const QString& path);
    static GdtfPreview              loadPreview(const QString& path);
    static GdtfDmxProfile           loadProfile(const QString& path, const QString& modeName = {}); // pan/tilt from named mode (default: mode 0)

    // MVR helpers — extract and work with GDTF files embedded in MVR ZIPs
    // Quick scan: returns filenames of .gdtf entries that are actually embedded in the ZIP
    static QStringList              listEmbeddedGdtfSpecs(const QByteArray& mvrData);
    static QByteArray               extractGdtfFromMvr(const QByteArray& mvrData, const QString& gdtfSpec);
    static GdtfPreview              loadPreviewFromData(const QByteArray& gdtfData);
    static GdtfDmxProfile           loadProfileFromData(const QByteArray& gdtfData);
    static bool                     importFromData(const QByteArray& gdtfData, const QString& fileName, QString* outError = nullptr);
};
