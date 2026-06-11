#pragma once
#include <cstdint>
#include <QMainWindow>
#include <QDockWidget>
#include <QTimer>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QMap>
#include <QList>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>
#include <QSize>
#include <QThread>
#include <QVector3D>
#include <QtGlobal>
#include "Project.h"
#include "Calibration.h"
#include "MvrImporter.h"
#include "ProjectWorker.h"

class QPropertyAnimation;
class VideoWidget;
class NdiReceiver;
class WebcamCapture;
class DeckLinkCapture;
class PsnSender;
class PsnReceiver;
class SacnReceiver;
class DmxSender;
class DmxReceiver;
class InputAdapterBase;
class SessionManager;
class TrackersPanel;
class TrackerBar;
class StreamSourcePanel;
class NetworkSettingsPanel;
class StatsPanel;
class CalibrationPanel;
class SessionPanel;
class Stage3DPanel;
class StageItemsPanel;
class StagePropertiesPanel;
class WelcomeScreen;
class SettingsDialog;
class FixturesPanel;
class GdtfLibraryDialog;
namespace oclero { namespace qlementine { class ThemeManager; } }

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(NdiReceiver* ndi,
                       oclero::qlementine::ThemeManager* themeManager,
                       QWidget* parent = nullptr);
    ~MainWindow() override;

    void loadProject(const Project& p, const QList<MvrImport>& parsedImports = {});
    void setNdiSource(const QString& source);
    void setWebcamSource(const QString& device);
    void setDecklinkSource(const QString& deviceId, const QString& connection,
                           uint32_t displayMode = 0, bool allow10Bit = true);

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
    void onSaveFinished();
    void onSaveFailed(const QString& error);
    void onLoadFinished(const Project& project, const QList<MvrImport>& parsedImports);
    void onLoadFailed(const QString& error);
    void onWorkerProgress(int percent);

private:
    void selectTracker(int id);
    bool isTrackerAllowed(int id) const;
    void setCalibrationActive(bool on);
    void applyProject();
    void setProgressAnimated(int target); // animate progress bar to target value
    void updateWindowTitle();
    void saveRecent(const QString& path);
    QStringList recentProjects() const;
    void updateStatsTimer();
    void updateSessionStatus();
    void updateCalibStatus();
    void updateSaveStatus();
    void markDirty();
    void markSaved();
    void updateTrackerBarRestriction();
    void updateTrackersPanelPeers();
    void log(const QString& msg);

    void handleVideoFrame(const QImage& frame);
    void applyPlaneHeight(float h);
    void applyTheme(const QString& theme);
    float stageHeightAt(float x, float z) const;

    void updateCameraPosition();
    void updateSystemObjects();
    void syncAllStageObjects();
    void showWelcomeScreen();
    void showWorkspace();
    void openRecentProject(const QString& path);

    void reconfigureDmxOutput();
    void reconfigureInputAdapters();
    void sendDmxForMode();

    void openGdtfLibrary();
    void onAssignGdtf(int importIdx, int layerIdx, int objIdx);

    VideoWidget*          video_;
    TrackersPanel*        trackersPanel_;
    TrackerBar*           trackerBar_;
    StreamSourcePanel*    streamPanel_;
    StatsPanel*           statsPanel_;
    CalibrationPanel*     calibrationPanel_;
    SessionPanel*         sessionPanel_;
    Stage3DPanel*            stage3DPanel_;
    StageItemsPanel*         stageItemsPanel_;
    StagePropertiesPanel*    stagePropertiesPanel_;
    FixturesPanel*           fixturesPanel_;
    SettingsDialog*          settingsDialog_;
    GdtfLibraryDialog*       gdtfLibraryDialog_ = nullptr;

    QDockWidget* videoDock_;
    QDockWidget* sessionDock_;
    QDockWidget* calibrationDock_;
    QDockWidget* trackersDock_;
    QDockWidget* statsDock_;
    QDockWidget* stage3DDock_;
    QDockWidget* stageItemsDock_;
    QDockWidget* stagePropertiesDock_;
    QDockWidget* fixturesDock_;
    QList<QDockWidget*> panelDocks_;

    NdiReceiver*  ndi_;
    WebcamCapture* webcam_;
    DeckLinkCapture* decklink_;
    PsnSender*    psnSender_;
    PsnReceiver*  psnReceiver_;
    SacnReceiver* sacnReceiver_;
    QThread*      sacnThread_  = nullptr;
    DmxSender*    dmxSender_   = nullptr;
    DmxReceiver*  dmxReceiver_ = nullptr;
    QList<InputAdapterBase*> inputAdapters_;
    SessionManager* sessionMgr_;
    QTimer        timer_;
    QTimer        statsTimer_;
    QElapsedTimer statsElapsed_;

    Project     project_;
    QString     projectPath_;
    Calibration calibration_;
    bool        calibActive_ = false;

    QMap<int, QPair<float,float>> trackerPositions_;
    QMap<int, QPair<float,float>> trackerRawPositions_;

    float clickPlaneHeight_ = 0.0f;

    QLabel*       statusPos_;
    QLabel*       statusTracker_;
    QLabel*       statusNdi_;
    QLabel*       statusCalib_;
    QLabel*       statusSession_;
    QLabel*       statusPsnOut_;
    QLabel*       statusSacnIn_;
    QProgressBar* saveLoadProgressBar_ = nullptr;
    QPushButton*  leaveSessionBtn_;

    bool projectDirty_    = false;
    bool applyingProject_ = false;

    QList<StageObject> systemStageItems_;
    QList<MvrImport>   mvrImports_;
    QVector3D          cameraPos3D_;
    bool               camera3DValid_ = false;
    qint64  sacnLastReceivedMs_ = -1;  // ms since epoch, -1 = never
    QString sacnSourceName_;

    oclero::qlementine::ThemeManager* themeManager_ = nullptr;
    QString currentTheme_;

    WelcomeScreen* welcomeScreen_  = nullptr;
    QWidget*       centralContainer_ = nullptr;
    QByteArray     savedWindowState_;
    bool           workspaceActive_ = false;

    QAction* actSaveProject_   = nullptr;
    QAction* actSaveProjectAs_ = nullptr;
    QAction* actCloseProject_  = nullptr;

    // Stats counters
    int     frameCount_   = 0;
    double  currentFps_   = 0.0;
    int     videoFrameCount_ = 0;
    double  videoFps_        = 0.0;

    quint64 lastPsnTxPackets_  = 0;
    quint64 lastPsnRxPackets_  = 0;
    quint64 lastSacnRxPackets_ = 0;

    // Debug logging
    QFile   logFile_;
    QTextStream* logStream_ = nullptr;
    QSize   lastVideoFrameSize_;
    QMap<int, QPair<float,float>> lastLoggedPositions_;

    enum class VideoSourceKind { Ndi, Webcam, DeckLink };
    VideoSourceKind videoSourceKind_ = VideoSourceKind::Ndi;
    QString         videoSourceName_;

    ProjectWorker*  projectWorker_ = nullptr;
    QThread*        workerThread_ = nullptr;
    bool            isSavingOrLoading_ = false;
    QList<MvrImport> pendingParsedImports_; // pre-parsed by worker, consumed by applyProject()
    QPropertyAnimation* progressAnim_ = nullptr;
};
