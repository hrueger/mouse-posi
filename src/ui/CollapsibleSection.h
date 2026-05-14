#pragma once
#include <QWidget>
#include <QPropertyAnimation>

class QVBoxLayout;
class QToolButton;
class QFrame;

class CollapsibleSection : public QWidget {
    Q_OBJECT
    Q_PROPERTY(int bodyHeight READ bodyHeight WRITE setBodyHeight)
public:
    explicit CollapsibleSection(const QString& title, QWidget* parent = nullptr);

    void setContentWidget(QWidget* widget);
    void setExpanded(bool expanded);
    bool isExpanded() const { return expanded_; }

    int  bodyHeight() const;
    void setBodyHeight(int h);

signals:
    void expandedChanged(bool expanded);

private:
    void toggleExpanded();
    void updateChevron();

    QToolButton*       header_;
    QFrame*            body_;
    QVBoxLayout*       bodyLayout_;
    QPropertyAnimation animation_;
    bool               expanded_ = true;
    int                collapsedHeight_ = 0;
    int                expandedHeight_  = 0;
};
