#include "GdtfLibraryDialog.h"
#include "GdtfShareDialog.h"
#include "GdtfMeshPreview.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QListWidget>
#include <QLabel>
#include <QComboBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QSet>
#include "../GdtfLibrary.h"
#include "../MvrImporter.h"

static const int RoleIsLocal = Qt::UserRole;
static const int RoleIndex   = Qt::UserRole + 1;

GdtfLibraryDialog::GdtfLibraryDialog(bool assignMode,
                                      const QList<MvrImportData>& mvrImports,
                                      QWidget* parent)
    : QDialog(parent), assignMode_(assignMode), mvrImports_(mvrImports)
{
    setWindowTitle(assignMode ? "Select GDTF Profile" : "GDTF Library");
    resize(1200, 640);
    setMinimumSize(800, 460);

    // ── Toolbar ──────────────────────────────────────────────────────────────
    auto* topBar = new QHBoxLayout;
    topBar->setContentsMargins(4, 4, 4, 0);
    topBar->setSpacing(4);

    auto* btnImport = new QPushButton("Import…");
    btnImport->setFixedWidth(80);
    btnDelete_ = new QPushButton("Delete");
    btnDelete_->setFixedWidth(80);
    btnDelete_->setEnabled(false);
    btnCopyToLib_ = new QPushButton("Copy to Library");
    btnCopyToLib_->setVisible(false);
    auto* btnShare = new QPushButton("Browse GDTF-Share…");

    topBar->addWidget(btnImport);
    topBar->addWidget(btnDelete_);
    topBar->addWidget(btnCopyToLib_);
    topBar->addStretch();
    topBar->addWidget(btnShare);

    // ── Left: list ───────────────────────────────────────────────────────────
    list_ = new QListWidget;
    list_->setMinimumWidth(280);

    // ── Middle: info + channels ───────────────────────────────────────────────
    auto* midWidget = new QWidget;
    auto* midLay = new QVBoxLayout(midWidget);
    midLay->setContentsMargins(8, 0, 4, 0);
    midLay->setSpacing(6);

    infoLabel_ = new QLabel("Select a GDTF from the list.");
    infoLabel_->setWordWrap(true);
    infoLabel_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    infoLabel_->setContentsMargins(0, 4, 0, 4);

    auto* modeRow = new QHBoxLayout;
    modeRow->addWidget(new QLabel("DMX Mode:"));
    modeCombo_ = new QComboBox;
    modeCombo_->setEnabled(false);
    modeRow->addWidget(modeCombo_, 1);

    channelTable_ = new QTableWidget(0, 4);
    channelTable_->setHorizontalHeaderLabels({"Attribute", "Coarse", "Fine", "Physical Range"});
    channelTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    channelTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    channelTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    channelTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    channelTable_->setColumnWidth(1, 55);
    channelTable_->setColumnWidth(2, 55);
    channelTable_->setColumnWidth(3, 160);
    channelTable_->verticalHeader()->hide();
    channelTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    channelTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    channelTable_->setAlternatingRowColors(true);

    midLay->addWidget(infoLabel_);
    midLay->addLayout(modeRow);
    midLay->addWidget(channelTable_, 1);

    // ── Right: 3D mesh preview ────────────────────────────────────────────────
    meshPreview_ = new GdtfMeshPreview;
    meshPreview_->setMinimumWidth(220);

    // ── Splitter ─────────────────────────────────────────────────────────────
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(list_);
    splitter->addWidget(midWidget);
    splitter->addWidget(meshPreview_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({300, 560, 340});

    // ── Bottom buttons — manual layout keeps Assign on the right ─────────────
    auto* bottomLay = new QHBoxLayout;
    bottomLay->setContentsMargins(8, 4, 8, 8);
    bottomLay->setSpacing(6);
    bottomLay->addStretch();

    if (assignMode) {
        auto* btnCancel = new QPushButton("Cancel");
        connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
        bottomLay->addWidget(btnCancel);

        btnAssign_ = new QPushButton("Assign");
        btnAssign_->setEnabled(false);
        btnAssign_->setDefault(true);
        bottomLay->addWidget(btnAssign_);
    } else {
        btnAssign_ = nullptr;
        auto* btnClose = new QPushButton("Close");
        connect(btnClose, &QPushButton::clicked, this, &QDialog::reject);
        bottomLay->addWidget(btnClose);
    }

    // ── Main layout ──────────────────────────────────────────────────────────
    auto* mainLay = new QVBoxLayout(this);
    mainLay->addLayout(topBar);
    mainLay->addWidget(splitter, 1);
    mainLay->addLayout(bottomLay);

    // ── Connections ──────────────────────────────────────────────────────────
    connect(btnImport,    &QPushButton::clicked, this, &GdtfLibraryDialog::onImport);
    connect(btnDelete_,   &QPushButton::clicked, this, &GdtfLibraryDialog::onDelete);
    connect(btnCopyToLib_,&QPushButton::clicked, this, &GdtfLibraryDialog::onCopyToLibrary);
    connect(btnShare,     &QPushButton::clicked, this, &GdtfLibraryDialog::onBrowseShare);
    connect(list_, &QListWidget::currentItemChanged, this, &GdtfLibraryDialog::onItemChanged);
    connect(modeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &GdtfLibraryDialog::onModeChanged);

    if (btnAssign_)
        connect(btnAssign_, &QPushButton::clicked, this, &GdtfLibraryDialog::onAssign);

    reloadList();
}

void GdtfLibraryDialog::reloadList()
{
    list_->clear();
    entries_ = GdtfLibrary::list();

    // ── Local library entries ─────────────────────────────────────────────────
    for (int i = 0; i < entries_.size(); ++i) {
        const auto& e = entries_[i];
        const QString label = e.manufacturer.isEmpty()
            ? e.name
            : e.manufacturer + " — " + e.name;
        auto* item = new QListWidgetItem(label);
        item->setData(RoleIsLocal, true);
        item->setData(RoleIndex,   i);
        list_->addItem(item);
    }

    // ── MVR GDTF entries (deduplicated, only actually-embedded GDTFs) ─────────
    mvrEntries_.clear();
    if (!mvrImports_.isEmpty()) {
        QSet<QString> localSpecs;
        for (const auto& e : entries_)
            localSpecs.insert(QFileInfo(e.path).fileName().toLower());

        QVector<QSet<QString>> embeddedPerImport(mvrImports_.size());
        for (int ii = 0; ii < mvrImports_.size(); ++ii) {
            if (mvrImports_[ii].mvrData.isEmpty()) continue;
            for (const QString& s : GdtfLibrary::listEmbeddedGdtfSpecs(mvrImports_[ii].mvrData))
                embeddedPerImport[ii].insert(s.toLower());
        }

        QSet<QString> seen;
        for (int ii = 0; ii < mvrImports_.size(); ++ii) {
            if (embeddedPerImport[ii].isEmpty()) continue;
            for (const auto& layer : mvrImports_[ii].layers) {
                for (const auto& obj : layer.objects) {
                    if (obj.gdtfSpec.isEmpty() || seen.contains(obj.gdtfSpec.toLower()))
                        continue;
                    if (!embeddedPerImport[ii].contains(obj.gdtfSpec.toLower()))
                        continue;
                    seen.insert(obj.gdtfSpec.toLower());

                    MvrEntry e;
                    e.gdtfSpec      = obj.gdtfSpec;
                    e.mvrImportIdx  = ii;
                    e.alsoInLibrary = localSpecs.contains(QFileInfo(obj.gdtfSpec).fileName().toLower());

                    const QString base = QFileInfo(obj.gdtfSpec).baseName();
                    const int at = base.indexOf('@');
                    if (at >= 0)
                        e.displayName = base.mid(at + 1) + " (" + base.left(at) + ")";
                    else
                        e.displayName = base;

                    mvrEntries_.append(e);
                }
            }
        }

        if (!mvrEntries_.isEmpty()) {
            auto* sep = new QListWidgetItem("── From MVR ──");
            sep->setFlags(Qt::NoItemFlags);
            QFont f = sep->font(); f.setItalic(true); sep->setFont(f);
            list_->addItem(sep);

            for (int i = 0; i < mvrEntries_.size(); ++i) {
                const auto& e = mvrEntries_[i];
                QString label = e.displayName;
                if (!e.alsoInLibrary) label += " ★";
                auto* item = new QListWidgetItem(label);
                item->setData(RoleIsLocal, false);
                item->setData(RoleIndex,   i);
                if (!e.alsoInLibrary)
                    item->setForeground(QColor("#e8a020"));
                item->setToolTip(e.alsoInLibrary
                    ? "From MVR — also in local library"
                    : "From MVR — not in local library (click 'Copy to Library' to save it)");
                list_->addItem(item);
            }
        }
    }

    clearPreview();
}

void GdtfLibraryDialog::onImport()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, "Import GDTF Files", {}, "GDTF Files (*.gdtf)");
    for (const auto& path : paths) {
        QString err;
        if (!GdtfLibrary::importFile(path, &err))
            QMessageBox::warning(this, "Import Failed", err);
    }
    reloadList();
}

void GdtfLibraryDialog::onDelete()
{
    auto* item = list_->currentItem();
    if (!item) return;
    if (!item->data(RoleIsLocal).toBool()) return;
    const int idx = item->data(RoleIndex).toInt();
    if (idx < 0 || idx >= entries_.size()) return;

    const QString path = entries_[idx].path;
    const QString name = item->text();
    if (QMessageBox::question(this, "Delete GDTF",
            QString("Remove \"%1\" from the library?\nThe file will be deleted.").arg(name),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return;
    GdtfLibrary::removeFile(path);
    reloadList();
}

void GdtfLibraryDialog::onCopyToLibrary()
{
    if (selectedMvrIdx_ < 0 || selectedMvrIdx_ >= mvrEntries_.size()) return;
    const auto& e = mvrEntries_[selectedMvrIdx_];
    const auto& mvrData = mvrImports_[e.mvrImportIdx].mvrData;

    const QByteArray gdtfData = GdtfLibrary::extractGdtfFromMvr(mvrData, e.gdtfSpec);
    if (gdtfData.isEmpty()) {
        QMessageBox::warning(this, "Extract Failed",
            "Could not extract GDTF data from the MVR file.");
        return;
    }

    QString err;
    if (!GdtfLibrary::importFromData(gdtfData, e.gdtfSpec, &err)) {
        QMessageBox::warning(this, "Import Failed",
            err.isEmpty() ? "Could not copy GDTF to library." : err);
        return;
    }

    const QString importedName = QFileInfo(e.gdtfSpec).fileName();
    reloadList();

    for (int r = 0; r < list_->count(); ++r) {
        auto* it = list_->item(r);
        if (it && it->data(RoleIsLocal).toBool()) {
            const int idx = it->data(RoleIndex).toInt();
            if (idx >= 0 && idx < entries_.size()) {
                if (QFileInfo(entries_[idx].path).fileName().compare(
                        importedName, Qt::CaseInsensitive) == 0) {
                    list_->setCurrentItem(it);
                    break;
                }
            }
        }
    }
}

void GdtfLibraryDialog::onBrowseShare()
{
    auto* dlg = new GdtfShareDialog(this);
    dlg->setWindowModality(Qt::WindowModal);
    connect(dlg, &QDialog::finished, this, [this, dlg](int) {
        dlg->deleteLater();
        reloadList();  // refresh in case user downloaded something
    });
    dlg->show();
}

void GdtfLibraryDialog::onAssign()
{
    if (selectedMvrIdx_ >= 0) {
        // MVR item — auto-copy to library first
        const auto& e = mvrEntries_[selectedMvrIdx_];
        const auto& mvrData = mvrImports_[e.mvrImportIdx].mvrData;
        const QByteArray gdtfData = GdtfLibrary::extractGdtfFromMvr(mvrData, e.gdtfSpec);
        if (gdtfData.isEmpty()) {
            QMessageBox::warning(this, "Extract Failed",
                "Could not extract GDTF data from the MVR file.");
            return;
        }
        QString err;
        if (!GdtfLibrary::importFromData(gdtfData, e.gdtfSpec, &err)) {
            QMessageBox::warning(this, "Import Failed",
                err.isEmpty() ? "Could not copy GDTF to library." : err);
            return;
        }
        // importFromData strips directory components — use only the filename
        selectedPath_ = GdtfLibrary::libraryPath() + "/" + QFileInfo(e.gdtfSpec).fileName();
    }

    if (!selectedPath_.isEmpty())
        accept();
}

void GdtfLibraryDialog::onItemChanged(QListWidgetItem* current, QListWidgetItem*)
{
    if (!current || !(current->flags() & Qt::ItemIsSelectable)) {
        clearPreview();
        btnDelete_->setEnabled(false);
        btnCopyToLib_->setVisible(false);
        if (btnAssign_) btnAssign_->setEnabled(false);
        return;
    }

    const bool isLocal = current->data(RoleIsLocal).toBool();
    const int  idx     = current->data(RoleIndex).toInt();

    if (isLocal) {
        if (idx < 0 || idx >= entries_.size()) return;

        btnDelete_->setEnabled(true);
        btnCopyToLib_->setVisible(false);
        selectedPath_   = entries_[idx].path;
        selectedMvrIdx_ = -1;

        const GdtfPreview preview = GdtfLibrary::loadPreview(selectedPath_);
        showPreview(preview);
        if (btnAssign_) btnAssign_->setEnabled(preview.valid);
        meshPreview_->setMeshes(MvrImporter::loadGdtfMeshes(selectedPath_));

    } else {
        // MVR entry
        if (idx < 0 || idx >= mvrEntries_.size()) return;
        const auto& mvrEntry = mvrEntries_[idx];

        btnDelete_->setEnabled(false);
        btnCopyToLib_->setVisible(!mvrEntry.alsoInLibrary);
        selectedPath_.clear();
        selectedMvrIdx_ = idx;

        const auto& mvrData = mvrImports_[mvrEntry.mvrImportIdx].mvrData;
        const QByteArray gdtfData = GdtfLibrary::extractGdtfFromMvr(mvrData, mvrEntry.gdtfSpec);

        if (!gdtfData.isEmpty()) {
            const GdtfPreview preview = GdtfLibrary::loadPreviewFromData(gdtfData);
            showPreview(preview);
            if (btnAssign_) btnAssign_->setEnabled(preview.valid);
            meshPreview_->setMeshes(MvrImporter::loadGdtfMeshesFromData(gdtfData));
        } else {
            clearPreview();
            infoLabel_->setText("<i>Could not extract GDTF from MVR — the MVR data may be missing.</i>");
            if (btnAssign_) btnAssign_->setEnabled(false);
        }
    }
}

void GdtfLibraryDialog::onModeChanged(int index)
{
    if (index < 0 || index >= currentPreview_.modes.size()) return;

    selectedModeName_ = currentPreview_.modes[index].name;

    const auto& mode = currentPreview_.modes[index];
    channelTable_->setRowCount(0);

    auto makeItem = [](const QString& t, Qt::Alignment align = Qt::AlignCenter) {
        auto* it = new QTableWidgetItem(t);
        it->setTextAlignment(align);
        return it;
    };

    for (const auto& ch : mode.channels) {
        const int row = channelTable_->rowCount();
        channelTable_->insertRow(row);
        channelTable_->setItem(row, 0, makeItem(ch.attribute, Qt::AlignLeft | Qt::AlignVCenter));
        channelTable_->setItem(row, 1, makeItem(QString::number(ch.coarse)));
        channelTable_->setItem(row, 2, makeItem(ch.fine >= 0 ? QString::number(ch.fine) : "—"));
        const QString range = QString("%1° … %2°")
            .arg(double(ch.minDeg), 0, 'f', 1)
            .arg(double(ch.maxDeg), 0, 'f', 1);
        channelTable_->setItem(row, 3, makeItem(range));
    }
}

void GdtfLibraryDialog::showPreview(const GdtfPreview& preview)
{
    currentPreview_ = preview;

    if (!preview.valid) {
        infoLabel_->setText("<i>Could not read GDTF file.</i>");
        modeCombo_->setEnabled(false);
        modeCombo_->clear();
        channelTable_->setRowCount(0);
        return;
    }

    QString info = QString("<b>%1 %2</b>").arg(preview.manufacturer, preview.name);
    if (!preview.shortName.isEmpty())
        info += QString(" <span style='color:grey'>(%1)</span>").arg(preview.shortName);
    if (!preview.description.isEmpty())
        info += "<br>" + preview.description;
    infoLabel_->setText(info);

    modeCombo_->blockSignals(true);
    modeCombo_->clear();
    for (const auto& m : preview.modes)
        modeCombo_->addItem(QString("%1  (%2ch)").arg(m.name).arg(m.footprint));
    modeCombo_->setEnabled(preview.modes.size() > 1);

    // Restore preselected mode (or keep index 0)
    int modeIdx = 0;
    if (!preselectedMode_.isEmpty()) {
        for (int i = 0; i < preview.modes.size(); ++i) {
            if (preview.modes[i].name == preselectedMode_) { modeIdx = i; break; }
        }
    }
    modeCombo_->setCurrentIndex(modeIdx);
    modeCombo_->blockSignals(false);

    onModeChanged(modeIdx);
}

void GdtfLibraryDialog::preselectEntry(const QString& gdtfSpec, const QString& modeName)
{
    preselectedSpec_ = gdtfSpec;
    preselectedMode_ = modeName;
    if (gdtfSpec.isEmpty()) return;

    const QString specFileName = QFileInfo(gdtfSpec).fileName().toLower();

    for (int r = 0; r < list_->count(); ++r) {
        auto* item = list_->item(r);
        if (!item || !(item->flags() & Qt::ItemIsSelectable)) continue;

        const bool isLocal = item->data(RoleIsLocal).toBool();
        const int  idx     = item->data(RoleIndex).toInt();

        if (isLocal && idx >= 0 && idx < entries_.size()) {
            if (QFileInfo(entries_[idx].path).fileName().toLower() == specFileName) {
                list_->setCurrentItem(item);
                return;
            }
        } else if (!isLocal && idx >= 0 && idx < mvrEntries_.size()) {
            if (QFileInfo(mvrEntries_[idx].gdtfSpec).fileName().toLower() == specFileName) {
                list_->setCurrentItem(item);
                return;
            }
        }
    }
}

void GdtfLibraryDialog::clearPreview()
{
    selectedPath_.clear();
    selectedModeName_.clear();
    selectedMvrIdx_ = -1;
    currentPreview_ = {};
    infoLabel_->setText("Select a GDTF from the list.");
    modeCombo_->clear();
    modeCombo_->setEnabled(false);
    channelTable_->setRowCount(0);
    meshPreview_->clearMeshes();
}
