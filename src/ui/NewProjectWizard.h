#pragma once
#include <QWizard>
#include <QList>
#include "../Project.h"

class QLabel;
class QTableWidget;
class QPushButton;
class ModeSelectionWidget;

// ── Wizard pages ──────────────────────────────────────────────────────────────

class ModePage : public QWizardPage {
    Q_OBJECT
public:
    explicit ModePage(QWidget* parent = nullptr);
    bool isComplete() const override { return true; }
    OperatingMode selectedMode() const;
private:
    ModeSelectionWidget* modeWidget_;
};

class TrackersPage : public QWizardPage {
    Q_OBJECT
public:
    explicit TrackersPage(QWidget* parent = nullptr);
    bool isComplete() const override;
    QList<TrackerConfig> trackers() const;
private slots:
    void onAdd();
    void onRemove();
private:
    QTableWidget* table_;
    QPushButton*  addBtn_;
    QPushButton*  removeBtn_;
    int           nextId_ = 1;
    void updateComplete();
};

// ── Wizard ────────────────────────────────────────────────────────────────────

class NewProjectWizard : public QWizard {
    Q_OBJECT
public:
    explicit NewProjectWizard(QWidget* parent = nullptr);

    OperatingMode selectedMode() const;
    QList<TrackerConfig> trackers() const;

private:
    ModePage*    modePage_;
    TrackersPage* trackersPage_;
};
