#include "GdtfLibrary.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <archive.h>
#include <archive_entry.h>
#include "Include/VectorworksMVR.h"
using namespace VectorworksMVR;

static QString toQStr(MvrString s) { return s ? QString::fromUtf8(s) : QString(); }

QString GdtfLibrary::libraryPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/gdtf";
}

QList<GdtfLibraryEntry> GdtfLibrary::list()
{
    QList<GdtfLibraryEntry> result;
    QDir dir(libraryPath());
    for (const auto& fi : dir.entryInfoList({"*.gdtf"}, QDir::Files, QDir::Name)) {
        IGdtfFixturePtr fix(IID_IGdtfFixture);
        GdtfLibraryEntry e;
        e.path = fi.absoluteFilePath();
        if (fix) {
            const QByteArray p = fi.absoluteFilePath().toUtf8();
            if (fix->ReadFromFile(p.constData()) == kVCOMError_NoError) {
                e.name         = toQStr(fix->GetName());
                e.manufacturer = toQStr(fix->GetManufacturer());
            }
        }
        if (e.name.isEmpty()) e.name = fi.baseName();
        result.append(e);
    }
    return result;
}

bool GdtfLibrary::importFile(const QString& srcPath, QString* outError)
{
    QDir().mkpath(libraryPath());
    const QString dest = libraryPath() + "/" + QFileInfo(srcPath).fileName();
    if (QFile::exists(dest)) QFile::remove(dest);
    if (!QFile::copy(srcPath, dest)) {
        if (outError) *outError = QStringLiteral("Could not copy file to library");
        return false;
    }
    return true;
}

bool GdtfLibrary::removeFile(const QString& path)
{
    return QFile::remove(path);
}

GdtfPreview GdtfLibrary::loadPreview(const QString& path)
{
    GdtfPreview preview;
    preview.path = path;

    IGdtfFixturePtr fix(IID_IGdtfFixture);
    if (!fix) return preview;
    const QByteArray p = path.toUtf8();
    if (fix->ReadFromFile(p.constData()) != kVCOMError_NoError) return preview;

    preview.name         = toQStr(fix->GetName());
    preview.manufacturer = toQStr(fix->GetManufacturer());
    preview.shortName    = toQStr(fix->GetShortName());
    preview.description  = toQStr(fix->GetFixtureTypeDescription());
    if (preview.name.isEmpty()) preview.name = QFileInfo(path).baseName();

    size_t modeCount = 0;
    fix->GetDmxModeCount(modeCount);

    for (size_t mi = 0; mi < modeCount; ++mi) {
        IGdtfDmxModePtr mode;
        if (fix->GetDmxModeAt(mi, &mode) != kVCOMError_NoError || !mode) continue;

        GdtfModeInfo modeInfo;
        modeInfo.name = toQStr(mode->GetName());

        // Get footprint for break 0
        size_t breakCount = 0;
        mode->GetBreakCount(breakCount);
        if (breakCount > 0) {
            size_t fp = 0;
            mode->GetFootprintForBreak(0, fp);
            modeInfo.footprint = int(fp);
        }

        size_t chCount = 0;
        mode->GetDmxChannelCount(chCount);

        int maxSlot = 0;
        for (size_t ci = 0; ci < chCount; ++ci) {
            IGdtfDmxChannelPtr ch;
            if (mode->GetDmxChannelAt(ci, &ch) != kVCOMError_NoError || !ch) continue;
            Sint32 coarse = -1, fine = -1;
            ch->GetCoarse(coarse);
            ch->GetFine(fine);
            if (coarse < 0) continue;
            maxSlot = qMax(maxSlot, int(coarse));
            if (fine >= 0) maxSlot = qMax(maxSlot, int(fine));

            size_t logCount = 0;
            ch->GetLogicalChannelCount(logCount);
            if (logCount == 0) continue;
            IGdtfDmxLogicalChannelPtr logCh;
            if (ch->GetLogicalChannelAt(0, &logCh) != kVCOMError_NoError || !logCh) continue;
            IGdtfAttributePtr attr;
            if (logCh->GetAttribute(&attr) != kVCOMError_NoError || !attr) continue;

            GdtfChannelRow row;
            row.attribute = toQStr(attr->GetName());
            row.coarse    = int(coarse);
            row.fine      = (fine >= 0) ? int(fine) : -1;

            size_t fnCount = 0;
            if (logCh->GetDmxFunctionCount(fnCount) == kVCOMError_NoError && fnCount > 0) {
                IGdtfDmxChannelFunctionPtr fn;
                if (logCh->GetDmxFunctionAt(0, &fn) == kVCOMError_NoError && fn) {
                    double from = -270.0, to = 270.0;
                    fn->GetPhysicalStart(from);
                    fn->GetPhysicalEnd(to);
                    row.minDeg = float(from);
                    row.maxDeg = float(to);
                }
            }
            modeInfo.channels.append(row);
        }
        if (modeInfo.footprint == 0) modeInfo.footprint = maxSlot;
        preview.modes.append(modeInfo);
    }
    preview.valid = true;
    return preview;
}

GdtfDmxProfile GdtfLibrary::loadProfile(const QString& path, const QString& modeName)
{
    const GdtfPreview prev = loadPreview(path);
    if (!prev.valid || prev.modes.isEmpty()) return {};

    int modeIdx = 0;
    if (!modeName.isEmpty()) {
        for (int i = 0; i < prev.modes.size(); ++i) {
            if (prev.modes[i].name == modeName) { modeIdx = i; break; }
        }
    }
    const auto& mode = prev.modes[modeIdx];

    GdtfDmxProfile profile;
    for (const auto& ch : mode.channels) {
        if (ch.attribute == QStringLiteral("Pan")) {
            profile.pan = { ch.coarse, ch.fine, ch.fine >= 0, ch.minDeg, ch.maxDeg };
        } else if (ch.attribute == QStringLiteral("Tilt")) {
            profile.tilt = { ch.coarse, ch.fine, ch.fine >= 0, ch.minDeg, ch.maxDeg };
        }
    }
    profile.valid     = true;
    profile.footprint = mode.footprint;
    profile.modeName  = mode.name;
    return profile;
}

// ─── MVR helpers ─────────────────────────────────────────────────────────────

static QTemporaryFile* writeMvrTemp(const QByteArray& mvrData)
{
    auto* tmp = new QTemporaryFile;
    tmp->setAutoRemove(true);
    if (!tmp->open() || tmp->write(mvrData) != mvrData.size()) {
        delete tmp;
        return nullptr;
    }
    tmp->flush();
    return tmp;
}

QStringList GdtfLibrary::listEmbeddedGdtfSpecs(const QByteArray& mvrData)
{
    QStringList result;
    QScopedPointer<QTemporaryFile> tmp(writeMvrTemp(mvrData));
    if (!tmp) return result;

    struct archive* a = archive_read_new();
    archive_read_support_format_zip(a);
    archive_read_support_filter_all(a);
    if (archive_read_open_filename(a, tmp->fileName().toUtf8().constData(), 10240) != ARCHIVE_OK) {
        archive_read_free(a);
        return result;
    }

    struct archive_entry* entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const QString name = QString::fromUtf8(archive_entry_pathname(entry));
        if (name.endsWith(".gdtf", Qt::CaseInsensitive))
            result.append(name);
        archive_read_data_skip(a);
    }
    archive_read_free(a);
    return result;
}

QByteArray GdtfLibrary::extractGdtfFromMvr(const QByteArray& mvrData, const QString& gdtfSpec)
{
    QTemporaryFile tmp;
    tmp.setAutoRemove(true);
    if (!tmp.open()) return {};
    tmp.write(mvrData);
    tmp.flush();

    const QByteArray utf8Path = tmp.fileName().toUtf8();
    const QByteArray utf8Spec = gdtfSpec.toUtf8();

    struct archive* a = archive_read_new();
    archive_read_support_format_zip(a);
    archive_read_support_filter_all(a);
    if (archive_read_open_filename(a, utf8Path.constData(), 10240) != ARCHIVE_OK) {
        archive_read_free(a);
        return {};
    }

    QByteArray result;
    struct archive_entry* entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (archive_entry_pathname(entry) == utf8Spec) {
            const size_t size = archive_entry_size(entry);
            QByteArray buf(int(size), 0);
            if (archive_read_data(a, buf.data(), size) > 0)
                result = buf;
            break;
        }
        archive_read_data_skip(a);
    }
    archive_read_free(a);
    return result;
}

static QString writeTempGdtf(const QByteArray& gdtfData)
{
    QTemporaryFile* tmp = new QTemporaryFile;
    tmp->setFileTemplate(QDir::tempPath() + "/onpoint_gdtf_XXXXXX.gdtf");
    tmp->setAutoRemove(false); // caller is responsible
    if (!tmp->open()) { delete tmp; return {}; }
    tmp->write(gdtfData);
    tmp->flush();
    const QString path = tmp->fileName();
    delete tmp; // close but file stays (AutoRemove=false)
    return path;
}

GdtfPreview GdtfLibrary::loadPreviewFromData(const QByteArray& gdtfData)
{
    const QString tmp = writeTempGdtf(gdtfData);
    if (tmp.isEmpty()) return {};
    const GdtfPreview result = loadPreview(tmp);
    QFile::remove(tmp);
    return result;
}

GdtfDmxProfile GdtfLibrary::loadProfileFromData(const QByteArray& gdtfData)
{
    const QString tmp = writeTempGdtf(gdtfData);
    if (tmp.isEmpty()) return {};
    const GdtfDmxProfile result = loadProfile(tmp);
    QFile::remove(tmp);
    return result;
}

bool GdtfLibrary::importFromData(const QByteArray& gdtfData, const QString& fileName,
                                  QString* outError)
{
    const QString tmp = writeTempGdtf(gdtfData);
    if (tmp.isEmpty()) {
        if (outError) *outError = "Could not write temporary file";
        return false;
    }
    // Rename to the intended filename before importing
    const QString named = QDir::tempPath() + "/" + QFileInfo(fileName).fileName();
    QFile::remove(named);
    QFile::rename(tmp, named);
    const bool ok = importFile(named, outError);
    QFile::remove(named);
    return ok;
}
