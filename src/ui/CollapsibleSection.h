#pragma once
#include <QWidget>

class QVBoxLayout;
class QPushButton;
class QFrame;

class CollapsibleSection : public QWidget {
    Q_OBJECT
public:
    explicit CollapsibleSection(const QString& title, QWidget* parent = nullptr);

    void setContentWidget(QWidget* widget);
    void setExpanded(bool expanded);
    bool isExpanded() const { return expanded_; }

signals:
    void expandedChanged(bool expanded);

private:
    void toggleExpanded();
    void updateChevron();

    QPushButton* header_;
    QFrame*      body_;
    QVBoxLayout* bodyLayout_;
    QString      title_;
    bool         expanded_ = true;
};
