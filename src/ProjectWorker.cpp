#include "ProjectWorker.h"

ProjectWorker::ProjectWorker(QObject* parent)
    : QObject(parent)
{
}

void ProjectWorker::saveAsync(const Project& project, const QString& path) {
    cachedProject_ = project;
    cachedPath_ = path;
    pendingOp_ = Operation::Save;
    QMetaObject::invokeMethod(this, &ProjectWorker::doSave, Qt::QueuedConnection);
}

void ProjectWorker::loadAsync(const QString& path) {
    cachedPath_ = path;
    pendingOp_ = Operation::Load;
    QMetaObject::invokeMethod(this, &ProjectWorker::doLoad, Qt::QueuedConnection);
}

void ProjectWorker::doSave() {
    if (pendingOp_ != Operation::Save) return;
    pendingOp_ = Operation::None;

    try {
        emit progress(50);
        cachedProject_.save(cachedPath_);
        emit progress(100);
        emit saveFinished();
    } catch (const std::exception& e) {
        emit saveFailed(QString::fromStdString(e.what()));
    }
}

void ProjectWorker::doLoad() {
    if (pendingOp_ != Operation::Load) return;
    pendingOp_ = Operation::None;

    try {
        emit progress(50);
        Project p = Project::load(cachedPath_);
        emit progress(100);
        emit loadFinished(p);
    } catch (const std::exception& e) {
        emit loadFailed(QString::fromStdString(e.what()));
    }
}
