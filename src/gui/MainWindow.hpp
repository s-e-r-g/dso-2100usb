#pragma once

// Main window: scope screen, control panel (channels, timebase, trigger, acquire, display,
// math, cursors, references), measurement table and the run/stop/single/autoset toolbar.

#include "app/Acquisition.hpp"
#include "app/Processor.hpp"
#include "gui/ScopeView.hpp"

#include <QElapsedTimer>
#include <QMainWindow>

#include <array>
#include <memory>
#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QMessageBox;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

namespace Gui
{

class RecordBar;
class Section;
class Segmented;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QByteArray firmwareHex, QWidget* parent = nullptr);
    ~MainWindow() override;
    MainWindow(const MainWindow&)            = delete;
    MainWindow& operator=(const MainWindow&) = delete;

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    struct ChannelUi
    {
        QGroupBox*      box         = nullptr;
        QComboBox*      vdiv        = nullptr;
        Segmented*      coupling    = nullptr;
        Segmented*      probe       = nullptr;
        QCheckBox*      invert      = nullptr;
        QDoubleSpinBox* offset      = nullptr;
        QLabel*         badge       = nullptr;
        QComboBox*      barVdiv     = nullptr; // the chip under the screen
        QComboBox*      barCoupling = nullptr;
    };

    // one row of the measurement table, with its running statistics
    struct MeasItem
    {
        int               source = 0; // channel, 2 = math
        Core::MeasureKind kind   = Core::MeasureKind::kFreq;
        double            last   = 0;
        double            min    = 0;
        double            max    = 0;
        double            sum    = 0;
        uint64_t          count  = 0;
    };

    // ---------- building ----------
    void     BuildMenus();
    void     BuildToolbar();
    QWidget* BuildSidePanel();
    QWidget* BuildChannelBox(size_t c);
    QWidget* BuildChannelChip(size_t c);
    QWidget* BuildBottom();
    void     ConnectView();
    void     ConnectAcquisition();

    // ---------- model -> widgets / device ----------
    void SyncUi();
    void FillVdivCombo(size_t c);
    void ApplyAcq();      // settings the device needs: restarts the capture
    void UpdateView();    // display-only settings
    void RedrawStopped(); // stopped: show the held capture with the current zoom and delay
    void UpdateBadges();
    void ResetStats();
    void SetRunning(bool running);
    void SetTrigStatus(const QString& text, const QColor& color);

    // ---------- data ----------
    void OnFrame(const App::FramePtr& frame);
    void ProfileFrame(const Core::Frame& f, double processMs, double uiMs); // DSO_PROFILE=<file>
    void AutosetStep(const Core::Frame& f);
    void UpdateMeasurements(const App::DisplayData& d);
    void UpdateDecodeLog(const App::DisplayData& d);
    void AddMeasurement(int source, Core::MeasureKind kind);

    // ---------- actions ----------
    void   RunStop();
    void   Run();
    void   Stop();
    void   Single();
    void   Autoset();
    void   DefaultSetup();
    void   Calibrate();
    void   SaveScreenshot();
    void   SaveCsv();
    void   SaveReference(size_t slot);
    void   ShowDriverProblem(const QString& hint);
    void   ZoomTime(int steps);
    void   ZoomVolts(int steps);
    double ClampDelay(double sec) const; // limits the delay to what the record can show

    void LoadSettings();
    void SaveSettings() const;

    std::unique_ptr<App::Acquisition> m_acq;
    App::Processor                    m_proc;
    Core::AcqSettings                 m_acqSettings;
    App::ViewSettings                 m_viewSettings;
    ViewState                         m_display;
    std::vector<double>               m_timeSteps;
    size_t                            m_timeIndex = 0;
    uint64_t                          m_sentGen   = 1; // mirrors Acquisition's counter
    bool                              m_running   = true;
    bool                              m_single    = false;
    bool          m_syncing = true; // widgets are being written, not edited (also while building)
    int           m_autosetStep = 0;
    int           m_selChannel  = 0; // target of Ctrl+wheel
    App::FramePtr m_lastFrame;
    std::optional<App::DisplayData>        m_lastData;
    QString                                m_profilePath;
    QElapsedTimer                          m_profTimer;
    int                                    m_profCount = 0;
    Core::FrameTiming                      m_profSum;
    double                                 m_profProcessMs = 0;
    double                                 m_profUiMs      = 0;
    std::vector<MeasItem>                  m_meas;
    std::array<std::optional<RefTrace>, 2> m_refs;

    // widgets
    ScopeView*                m_view      = nullptr;
    RecordBar*                m_recordBar = nullptr;
    std::array<ChannelUi, 2>  m_ch;
    QComboBox*                m_timeCombo  = nullptr;
    QLabel*                   m_delayLabel = nullptr;
    Segmented*                m_trigMode   = nullptr;
    Segmented*                m_trigSource = nullptr;
    Segmented*                m_trigEdge   = nullptr;
    QDoubleSpinBox*           m_trigLevel  = nullptr;
    QCheckBox*                m_hwTrig     = nullptr;
    QComboBox*                m_avgCombo   = nullptr;
    Segmented*                m_persist    = nullptr;
    Segmented*                m_drawMode   = nullptr;
    Segmented*                m_xy         = nullptr;
    QComboBox*                m_mathOp     = nullptr;
    Segmented*                m_mathA      = nullptr;
    Segmented*                m_mathB      = nullptr;
    QComboBox*                m_fftWindow  = nullptr;
    Segmented*                m_cursorMode = nullptr;
    Segmented*                m_cursorSrc  = nullptr;
    std::array<QCheckBox*, 2> m_refShow{};
    QTableWidget*             m_measTable   = nullptr;
    QCheckBox*                m_stats       = nullptr;
    Segmented*                m_acqState    = nullptr;
    QLabel*                   m_trigStatus  = nullptr;
    QLabel*                   m_connLabel   = nullptr;
    QLabel*                   m_timeBadge   = nullptr;
    QLabel*                   m_trigBadge   = nullptr;
    QLabel*                   m_rateBadge   = nullptr;
    QAction*                  m_demoAction  = nullptr;
    Segmented*                m_gridLabels  = nullptr;
    Segmented*                m_decodeOn    = nullptr;
    Segmented*                m_decodeScl   = nullptr;
    QLabel*                   m_decodeFound = nullptr;
    QCheckBox*                m_decodeStart = nullptr;
    Section*                  m_logSection  = nullptr;
    QPlainTextEdit*           m_decodeLog   = nullptr;
    QMessageBox*              m_driverBox   = nullptr;
};

}
