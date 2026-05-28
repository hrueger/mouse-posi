#pragma once
#include <QWidget>
#include <QString>

class NdiReceiver;
class QListWidget;
class QLabel;
class QPushButton;
class QTimer;

class NdiPanel : public QWidget {
    Q_OBJECT
public:
    explicit NdiPanel(NdiReceiver* ndi, QWidget* parent = nullptr);

    QString selectedSource() const;
    void    setCurrentSource(const QString& source);

signals:
    void sourceSelected(QString source);

private:
    void refresh();

    NdiReceiver* ndi_;
    QListWidget* list_;
    QLabel*      statusLabel_;
    QPushButton* refreshBtn_;
    QPushButton* connectBtn_;
    QTimer*      autoRefreshTimer_;
};
