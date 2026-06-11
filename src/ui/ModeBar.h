#pragma once
#include <QWidget>
#include "../Project.h"

class QPushButton;

class ModeBar : public QWidget {
    Q_OBJECT
public:
    explicit ModeBar(QWidget* parent = nullptr);

    void setMode(OperatingMode mode);
    OperatingMode mode() const { return mode_; }

signals:
    void modeChanged(OperatingMode mode);

private:
    void updateButtons();

    QPushButton*  btnPsn_;
    QPushButton*  btnCam2D_;
    QPushButton*  btnDmx3D_;
    OperatingMode mode_ = OperatingMode::Stage3DPSN;
    bool          updating_ = false;
};
