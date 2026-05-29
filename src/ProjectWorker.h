#pragma once
#include <QObject>
#include <QString>
#include <QThread>
#include "Project.h"

class ProjectWorker : public QObject {
    Q_OBJECT
public:
    explicit ProjectWorker(QObject* parent = nullptr);

    void saveAsync(const Project& project, const QString& path);
    void loadAsync(const QString& path);

signals:
    void saveFinished();
    void saveFailed(const QString& error);
    void loadFinished(const Project& project);
    void loadFailed(const QString& error);
    void progress(int percent);  // 0-100 for save progress

private slots:
    void doSave();
    void doLoad();

private:
    Project cachedProject_;
    QString cachedPath_;
    enum class Operation { None, Save, Load };
    Operation pendingOp_ = Operation::None;
};
