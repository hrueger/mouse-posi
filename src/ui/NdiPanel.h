#pragma once
#include <QWidget>
#include <QString>

class NdiReceiver;
class QListWidget;
class QLabel;
class QPushButton;

class NdiPanel : public QWidget {
    Q_OBJECT
public:
    explicit NdiPanel(NdiReceiver* ndi, QWidget* parent = nullptr);

    QString selectedSource() const;
    void    setCurrentSource(const QString& source);

signals:
    void sourceSelected(QString source);

private:
    void scan();

    NdiReceiver* ndi_;
    QListWidget* list_;
    QLabel*      statusLabel_;
    QPushButton* scanBtn_;
    QPushButton* connectBtn_;
};
