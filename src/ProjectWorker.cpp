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
        const int n = cachedProject_.mvr.imports.size();
        // Fire progress BEFORE each write so the bar moves immediately.
        // entry -1 = about to write project.json, entry 0..n-1 = about to write MVR i.
        cachedProject_.save(cachedPath_, [this, n](int entry) {
            emit progress(10 + 85 * (entry + 1) / std::max(n + 1, 1));
        });
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
        emit progress(5);
        Project p = Project::load(cachedPath_);
        // MVR parsing (libmvrgdtf) must run on the main thread, so we stop here.
        // applyProject() will parse each embedded MVR and update the progress bar itself.
        emit progress(15);
        emit loadFinished(p, {});
    } catch (const std::exception& e) {
        emit loadFailed(QString::fromStdString(e.what()));
    }
}
