#pragma once
#include <QMainWindow>
#include <QTimer>
#include <QLabel>
#include <QPushButton>
#include <QMap>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>
#include <QSize>
#include <QtGlobal>
#include "Project.h"
#include "Calibration.h"

class VideoWidget;
class NdiReceiver;
class WebcamCapture;
class DeckLinkCapture;
class PsnSender;
class PsnReceiver;
class SessionManager;
class SidebarWidget;
class TrackersPanel;
class TrackerBar;
class StreamSourcePanel;
class NetworkSettingsPanel;
class StatsPanel;
class CalibrationPanel;
class SessionPanel;
class CollapsibleSection;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(NdiReceiver* ndi, QWidget* parent = nullptr);
    ~MainWindow() override;

    void loadProject(const Project& p);
    void setNdiSource(const QString& source);
    void setWebcamSource(const QString& device);
    void setDecklinkSource(const QString& device);

protected:
    void closeEvent(QCloseEvent*) override;
    void changeEvent(QEvent*) override;

private slots:
    void onTimer();
    void onFrameReady(const QImage& frame);
    void onWebcamFrameReady(const QImage& frame);
    void onDecklinkFrameReady(const QImage& frame);
    void onNewProject();
    void onOpenProject();
    void onSaveProject();
    void onSaveProjectAs();

private:
    void selectTracker(int id);
    bool isTrackerAllowed(int id) const;
    void applyProject();
    void updateWindowTitle();
    void saveRecent(const QString& path);
    QStringList recentProjects() const;
    void updateStatsTimer();
    void updateSessionStatus();
    void updateCalibStatus();
    void updateTrackerBarRestriction();
    void updateTrackersPanelPeers();
    void log(const QString& msg);

    void handleVideoFrame(const QImage& frame);

    VideoWidget*          video_;
    SidebarWidget*        sidebar_;
    TrackersPanel*        trackersPanel_;
    TrackerBar*           trackerBar_;
    StreamSourcePanel*    streamPanel_;
    NetworkSettingsPanel* networkPanel_;
    StatsPanel*           statsPanel_;
    CalibrationPanel*     calibrationPanel_;
    SessionPanel*         sessionPanel_;
    CollapsibleSection*   calibrationSection_ = nullptr;

    NdiReceiver*  ndi_;
    WebcamCapture* webcam_;
    DeckLinkCapture* decklink_;
    PsnSender*    psnSender_;
    PsnReceiver*  psnReceiver_;
    SessionManager* sessionMgr_;
    QTimer        timer_;
    QTimer        statsTimer_;
    QElapsedTimer statsElapsed_;

    Project     project_;
    QString     projectPath_;
    Calibration calibration_;

    QMap<int, QPair<float,float>> trackerPositions_;

    QLabel*       statusPos_;
    QLabel*       statusTracker_;
    QLabel*       statusNdi_;
    QLabel*       statusCalib_;
    QLabel*       statusSession_;
    QLabel*       statusPsnOut_;
    QPushButton*  leaveSessionBtn_;

    // Stats counters
    int     frameCount_   = 0;
    double  currentFps_   = 0.0;

    quint64 lastPsnTxPackets_ = 0;
    quint64 lastPsnRxPackets_ = 0;

    // Debug logging
    QFile   logFile_;
    QTextStream* logStream_ = nullptr;
    QSize   lastVideoFrameSize_;
    QMap<int, QPair<float,float>> lastLoggedPositions_;

    enum class VideoSourceKind { Ndi, Webcam, DeckLink };
    VideoSourceKind videoSourceKind_ = VideoSourceKind::Ndi;
    QString         videoSourceName_;
};
