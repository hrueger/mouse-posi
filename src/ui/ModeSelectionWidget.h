#pragma once
#include <QWidget>
#include "../Project.h"

class QButtonGroup;

// Reusable mode-selection panel used by both SettingsDialog and NewProjectWizard.
class ModeSelectionWidget : public QWidget {
    Q_OBJECT
public:
    explicit ModeSelectionWidget(QWidget* parent = nullptr);

    void setMode(OperatingMode mode);
    OperatingMode mode() const;

signals:
    void modeChanged(OperatingMode mode);

private:
    QButtonGroup*  group_;
    bool           emitChanges_ = true;
};
