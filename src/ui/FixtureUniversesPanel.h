#pragma once
#include <QWidget>
#include <QList>
#include "../Project.h"

class QTableWidget;
class QLabel;
class QCheckBox;
class QComboBox;
class QSpinBox;
class QLineEdit;

class FixtureUniversesPanel : public QWidget {
    Q_OBJECT
public:
    explicit FixtureUniversesPanel(QWidget* parent = nullptr);

    void setConfigs(const QList<FixtureUniverseConfig>& configs);
    void setMvrData(const MvrSettings& mvr);
    QList<FixtureUniverseConfig> configs() const;

signals:
    void fixtureUniverseConfigsChanged(QList<FixtureUniverseConfig> configs);

private slots:
    void onHeaderClicked(int col);

private:
    void rebuild();
    int  fixtureCountForUniverse(quint16 uni) const;
    void emitChanged();
    void updateRowEnabled(int row);

    enum Col {
        ColUniverse = 0, ColFixtures, ColFollowSpots,
        ColInEnabled, ColInProtocol, ColInMode, ColInIface, ColInIp,
        ColSameOutput, ColOutUniverse,
        ColOutEnabled, ColOutProtocol, ColOutMode, ColOutIface, ColOutIp,
        ColOutPriority,
        ColCount
    };

    struct RowWidgets {
        quint16    fixtureUniverse = 1;
        QCheckBox* followSpots  = nullptr;
        QCheckBox* inEnabled    = nullptr;
        QComboBox* inProtocol   = nullptr;
        QComboBox* inMode       = nullptr;
        QComboBox* inIface      = nullptr;
        QLineEdit* inIp         = nullptr;
        QCheckBox* sameOutput   = nullptr;
        QSpinBox*  outUniverse  = nullptr;
        QCheckBox* outEnabled   = nullptr;
        QComboBox* outProtocol  = nullptr;
        QComboBox* outMode      = nullptr;
        QComboBox* outIface     = nullptr;
        QLineEdit* outIp        = nullptr;
        QSpinBox*  outPriority  = nullptr;
    };

    QTableWidget*                table_;
    QLabel*                      emptyLabel_;
    QList<RowWidgets>            rowWidgets_;
    QList<FixtureUniverseConfig> configs_;
    MvrSettings                  mvr_;
    bool                         updating_ = false;
    int                          sortCol_  = ColUniverse;
    bool                         sortAsc_  = true;
};
