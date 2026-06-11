#include "DmxAddrEdit.h"
#include <QHBoxLayout>
#include <QLineEdit>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QFocusEvent>

DmxAddrEdit::DmxAddrEdit(QWidget* parent) : QWidget(parent)
{
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(2, 0, 2, 0);
    lay->setSpacing(0);

    edit_ = new QLineEdit;
    edit_->setFrame(false);
    edit_->setAlignment(Qt::AlignCenter);
    edit_->setValidator(new QRegularExpressionValidator(
        QRegularExpression(R"(\d{1,5}\.\d{1,3})"), edit_));
    edit_->setPlaceholderText("1.1");

    lay->addWidget(edit_);

    connect(edit_, &QLineEdit::editingFinished, this, &DmxAddrEdit::commitEdit);
}

void DmxAddrEdit::setValue(int universe, int address)
{
    universe_ = qBound(1, universe, 65535);
    address_  = qBound(1, address,  512);
    updateDisplay();
}

void DmxAddrEdit::updateDisplay()
{
    edit_->setText(QString("%1.%2").arg(universe_).arg(address_));
}

void DmxAddrEdit::commitEdit()
{
    const QString text = edit_->text().trimmed();
    const int dot = text.indexOf('.');
    if (dot < 1) { updateDisplay(); return; }

    bool okU = false, okA = false;
    const int u = text.left(dot).toInt(&okU);
    const int a = text.mid(dot + 1).toInt(&okA);

    if (!okU || !okA || u < 1 || u > 65535 || a < 1 || a > 512) {
        updateDisplay();
        return;
    }

    if (u != universe_ || a != address_) {
        universe_ = u;
        address_  = a;
        emit valueChanged(u, a);
    }
    updateDisplay();
}
