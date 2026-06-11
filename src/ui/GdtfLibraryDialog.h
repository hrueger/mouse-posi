#pragma once
#include <QDialog>
#include "../GdtfLibrary.h"
#include "../Project.h"

class QListWidget;
class QListWidgetItem;
class QLabel;
class QComboBox;
class QTableWidget;
class QPushButton;
class GdtfMeshPreview;

class GdtfLibraryDialog : public QDialog {
    Q_OBJECT
public:
    // assignMode: true → shows Assign button; false → browse only
    // mvrImports: optional MVR imports whose embedded GDTFs will be shown in the list
    explicit GdtfLibraryDialog(bool assignMode,
                               const QList<MvrImportData>& mvrImports = {},
                               QWidget* parent = nullptr);

    // Valid after exec() == Accepted in assign mode
    QString selectedGdtfPath() const { return selectedPath_; }
    QString selectedModeName()  const { return selectedModeName_; }

    // Pre-select a fixture and mode before exec() (for re-assignment)
    void preselectEntry(const QString& gdtfSpec, const QString& modeName);

private slots:
    void onImport();
    void onDelete();
    void onCopyToLibrary();
    void onBrowseShare();
    void onAssign();
    void onItemChanged(QListWidgetItem* current, QListWidgetItem* previous);
    void onModeChanged(int index);

private:
    struct MvrEntry {
        QString gdtfSpec;      // filename in the MVR ZIP, e.g. "Robe Robin 600E.gdtf"
        int     mvrImportIdx;  // index into mvrImports_
        QString displayName;   // human-readable label for the list
        bool    alsoInLibrary; // true if the local library already has this spec
    };

    void reloadList();
    void showPreview(const GdtfPreview& preview);
    void clearPreview();

    QListWidget*     list_;
    QLabel*          infoLabel_;
    QComboBox*       modeCombo_;
    QTableWidget*    channelTable_;
    GdtfMeshPreview* meshPreview_;
    QPushButton*     btnDelete_;
    QPushButton*     btnCopyToLib_;
    QPushButton*     btnAssign_;     // null when !assignMode_

    bool         assignMode_;
    QString      selectedPath_;        // non-empty when local library entry is selected
    QString      selectedModeName_;    // mode name chosen in the combo
    int          selectedMvrIdx_ = -1; // >=0 when MVR entry is selected

    QString      preselectedSpec_;     // gdtfSpec to auto-select (set via preselectEntry)
    QString      preselectedMode_;     // mode name to auto-select

    GdtfPreview  currentPreview_;

    QList<GdtfLibraryEntry>  entries_;
    QList<MvrEntry>          mvrEntries_;
    QList<MvrImportData>     mvrImports_;
};
