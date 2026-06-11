#pragma once
#include <QWidget>

class QLineEdit;

// Compact universe.address editor — displays and edits a DMX address as "U.CH"
// (e.g. "1.24"). Validates universe 1–65535 and channel 1–512.
class DmxAddrEdit : public QWidget {
    Q_OBJECT
public:
    explicit DmxAddrEdit(QWidget* parent = nullptr);

    void setValue(int universe, int address);
    int  universe() const { return universe_; }
    int  address()  const { return address_; }

signals:
    void valueChanged(int universe, int address);

private:
    void updateDisplay();
    void commitEdit();

    QLineEdit* edit_;
    int        universe_ = 1;
    int        address_  = 1;
};
