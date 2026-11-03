// Main window. The models (m_acqSettings, m_viewSettings, m_display) are the truth: widgets
// write into them and SyncUi() writes them back to the widgets (m_syncing breaks the loop).

#include "gui/MainWindow.hpp"

#include "core/Format.hpp"
#include "gui/Theme.hpp"
#include "gui/Widgets.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStatusBar>
#include <QTableWidget>
#include <QTextStream>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace Gui
{

namespace
{

constexpr int    kMathSource             = 2;
constexpr double kDefaultSecPerDiv       = 200e-6;
constexpr double kOffsetStepDiv          = 0.1;
constexpr double kMaxOffsetDiv           = 5.0;
constexpr int    kLevelDecimals          = 3;
constexpr double kLevelStepDiv           = 0.04;
constexpr double kLevelRangeDiv          = 5.0;
constexpr int    kStatusMs               = 6000;
constexpr int    kMeasRowsVisible        = 5;
constexpr int    kMeasRowHeight          = 22;
// autoset
constexpr double kAutosetStartSecPerDiv  = 1e-3;
constexpr double kAutosetFillDiv         = 3.6; // largest |V| may use this much of the 4 div
constexpr double kAutosetPeriodsOnScreen = 2.5;
constexpr double kAutosetMinCounts       = 3.0; // smaller swings are noise

constexpr auto kAverageChoices = std::to_array<int>({1, 2, 4, 8, 16, 32, 64, 128});

QString Si(double v, const char* unit, int digits = 4)
{
    return QString::fromStdString(Core::FormatSi(v, unit, digits));
}

QString ChannelName(int source)
{
    return source == kMathSource ? QObject::tr("Math") : QObject::tr("CH%1").arg(source + 1);
}

QString BadgeStyle(const QColor& c)
{
    return QStringLiteral("QLabel { color: black; background: %1; border-radius: 3px;"
                          " padding: 2px 6px; font-weight: bold; }")
        .arg(c.name());
}

QComboBox* MakeCombo(const QStringList& items)
{
    auto* c = new QComboBox;
    c->addItems(items);
    return c;
}

// A top bar control with a small caption above it.
QWidget* Captioned(const QString& caption, QWidget* control)
{
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(1);
    auto* label = new QLabel(caption);
    label->setObjectName(QStringLiteral("caption"));
    v->addWidget(label);
    v->addWidget(control);
    return w;
}

}

MainWindow::MainWindow(QByteArray firmwareHex, QWidget* parent)
    : QMainWindow(parent), m_acq(std::make_unique<App::Acquisition>(std::move(firmwareHex))),
      m_timeSteps(Core::TimePerDivChoices())
{
    setWindowTitle(tr("DSO-2100 Scope"));
    m_timeIndex = static_cast<size_t>(
        std::distance(m_timeSteps.begin(), std::lower_bound(m_timeSteps.begin(), m_timeSteps.end(),
                                                            kDefaultSecPerDiv * 0.999)));

    m_view        = new ScopeView;
    auto* central = new QWidget;
    auto* lay     = new QVBoxLayout(central);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);
    lay->addWidget(m_view, 1);
    lay->addWidget(BuildBottom());
    setCentralWidget(central);

    auto* dock = new QDockWidget(tr("Controls"), this);
    dock->setObjectName(QStringLiteral("controls"));
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    dock->setTitleBarWidget(new QWidget); // the height is better spent on the controls
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(BuildSidePanel());
    scroll->setMinimumWidth(300);
    dock->setWidget(scroll);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    BuildToolbar();
    BuildMenus();
    ConnectView();
    ConnectAcquisition();

    LoadSettings();
    m_profilePath = qEnvironmentVariable("DSO_PROFILE");
    if (!m_profilePath.isEmpty())
    {
        m_viewSettings.trig.mode = Core::TriggerMode::kAuto; // measure with frames on screen
    }
    SyncUi();
    const bool firstRun = !QSettings().contains(QStringLiteral("ch1/vdiv"));
    ApplyAcq();
    SetRunning(true);
    m_acq->start();
    if (firstRun)
    {
        Autoset(); // a sensible screen on the first start instead of factory defaults
    }
}

MainWindow::~MainWindow()
{
    m_acq.reset(); // join the thread before the widgets it signals go away
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    SaveSettings();
    e->accept();
}

// ---------- building ----------

void MainWindow::BuildMenus()
{
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("Save &screenshot..."), QKeySequence(Qt::CTRL | Qt::Key_P), this,
                    &MainWindow::SaveScreenshot);
    file->addAction(tr("Save waveform as &CSV..."), QKeySequence::Save, this, &MainWindow::SaveCsv);
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu* acq = menuBar()->addMenu(tr("&Acquire"));
    acq->addAction(tr("&Run / Stop"), QKeySequence(Qt::Key_Space), this, &MainWindow::RunStop);
    acq->addAction(tr("&Single"), QKeySequence(Qt::Key_S), this, &MainWindow::Single);
    acq->addAction(tr("&Autoset"), QKeySequence(Qt::Key_A), this, &MainWindow::Autoset);
    acq->addAction(tr("&Clear display"), QKeySequence(Qt::Key_C), this,
                   [this]
                   {
                       m_view->ClearPersistence();
                       ResetStats();
                   });

    QMenu* util = menuBar()->addMenu(tr("&Utility"));
    util->addAction(tr("&Default setup"), this, &MainWindow::DefaultSetup);
    util->addAction(tr("&Calibrate zero..."), this, &MainWindow::Calibrate);
    m_demoAction = util->addAction(tr("Demo signals when no scope is connected"));
    m_demoAction->setCheckable(true);
    m_demoAction->setChecked(true);
    connect(m_demoAction, &QAction::toggled, this, [this](bool on) { m_acq->SetDemoAllowed(on); });

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(
        tr("USB &driver setup..."), this,
        [this]
        {
            QMessageBox::information(
                this, tr("USB driver setup"),
                tr("The DSO-2100 appears on USB twice: as 0547:1006 until this program uploads the "
                   "firmware, then as 0547:1002.\n\n"
                   "Windows: bind WinUSB to both IDs once with Zadig (Options > List All Devices). "
                   "The original dso21001/dso21002.sys drivers are not needed.\n\n"
                   "Linux: install 60-dso2100.rules into /etc/udev/rules.d. fxload is not "
                   "needed.\n\n"
                   "Details: docs/DRIVERS.md"));
        });
    help->addAction(tr("&Mouse and keys..."), this,
                    [this]
                    {
                        QMessageBox::information(
                            this, tr("Mouse and keys"),
                            tr("Wheel: time/div    Ctrl+wheel: V/div of the last used channel\n"
                               "Drag screen: move in time    Double-click: delay to 0\n"
                               "Drag the coloured markers on the left: channel position\n"
                               "Drag the orange marker on the right: trigger level\n"
                               "Pinch (touch): time/div\n\n"
                               "Space: Run/Stop    S: Single    A: Autoset    C: clear"));
                    });
    help->addAction(tr("&About"), this,
                    [this]
                    {
                        QMessageBox::about(
                            this, tr("About"),
                            tr("<b>DSO-2100 Scope</b> %1<br>Open software for the Hantek/Voltcraft "
                               "DSO-2100 USB "
                               "oscilloscope.<br>Qt %2, libusb.")
                                .arg(QStringLiteral(DSO_VERSION), QString::fromLatin1(qVersion())));
                    });
}

void MainWindow::BuildToolbar()
{
    QToolBar* tb = addToolBar(tr("Main"));
    tb->setObjectName(QStringLiteral("main"));
    tb->setMovable(false);
    tb->setFloatable(false);

    // what is pressed all the time, then the display tools, then status on the right
    m_runBtn = new QPushButton(tr("Run/Stop"));
    m_runBtn->setMinimumSize(96, 40);
    m_runBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_runBtn, &QPushButton::clicked, this, &MainWindow::RunStop);
    tb->addWidget(m_runBtn);

    auto* single = new QPushButton(tr("Single"));
    single->setMinimumHeight(40);
    single->setFocusPolicy(Qt::NoFocus);
    single->setToolTip(tr("Capture one triggered frame, then stop (S)"));
    connect(single, &QPushButton::clicked, this, &MainWindow::Single);
    tb->addWidget(single);

    auto* autoBtn = new QPushButton(tr("Autoset"));
    autoBtn->setMinimumHeight(40);
    autoBtn->setFocusPolicy(Qt::NoFocus);
    autoBtn->setToolTip(tr("Find the signal and set scale, time and trigger (A)"));
    connect(autoBtn, &QPushButton::clicked, this, &MainWindow::Autoset);
    tb->addWidget(autoBtn);

    tb->addSeparator();

    m_drawMode = new Segmented({tr("Vectors"), tr("Dots")});
    m_drawMode->SetToolTips({tr("Join the samples with lines"), tr("Show the samples only")});
    tb->addWidget(Captioned(tr("DRAW"), m_drawMode));
    connect(m_drawMode, &Segmented::IndexChanged, this,
            [this](int i)
            {
                m_display.dots = i == 1;
                UpdateView();
            });

    m_persist = new Segmented({tr("Off"), tr("Short"), tr("Long"), QStringLiteral("∞")});
    m_persist->SetToolTips({tr("No persistence"), tr("Old traces fade quickly"),
                            tr("Old traces fade slowly"), tr("Keep every trace until Clear")});
    tb->addWidget(Captioned(tr("PERSISTENCE"), m_persist));
    connect(m_persist, &Segmented::IndexChanged, this,
            [this](int i)
            {
                m_display.persistence = static_cast<Persistence>(i);
                m_view->ClearPersistence();
                UpdateView();
            });

    m_xy = new Segmented({QStringLiteral("YT"), QStringLiteral("XY")});
    m_xy->SetToolTips({tr("Voltage over time"), tr("CH1 on X, CH2 on Y")});
    tb->addWidget(Captioned(tr("VIEW"), m_xy));
    connect(m_xy, &Segmented::IndexChanged, this,
            [this](int i)
            {
                m_display.xy = i == 1;
                UpdateView();
            });

    m_cursorMode =
        new Segmented({tr("Off"), QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("XY")});
    m_cursorMode->SetToolTips({tr("No cursors"), tr("Time cursors: read ΔX and 1/ΔX"),
                               tr("Voltage cursors: read ΔY"), tr("Time and voltage cursors")});
    tb->addWidget(Captioned(tr("CURSORS"), m_cursorMode));
    connect(m_cursorMode, &Segmented::IndexChanged, this,
            [this](int i)
            {
                m_display.cursorMode = static_cast<CursorMode>(i);
                UpdateView();
            });

    tb->addSeparator();

    auto* clear = new QPushButton(tr("Clear"));
    clear->setFocusPolicy(Qt::NoFocus);
    clear->setToolTip(tr("Clear persistence and statistics (C)"));
    connect(clear, &QPushButton::clicked, this,
            [this]
            {
                m_view->ClearPersistence();
                ResetStats();
            });
    tb->addWidget(clear);

    auto* spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);

    m_trigStatus = new QLabel;
    m_trigStatus->setMinimumWidth(60);
    m_trigStatus->setFixedHeight(28);
    m_trigStatus->setAlignment(Qt::AlignCenter);
    m_trigStatus->setToolTip(tr("T'D: triggered   AUTO: no trigger, showing the signal anyway\n"
                                "WAIT: waiting for a trigger   STOP: stopped"));
    tb->addWidget(m_trigStatus);

    m_connLabel = new QLabel;
    m_connLabel->setContentsMargins(8, 0, 8, 0);
    statusBar()->addPermanentWidget(m_connLabel);
}

QWidget* MainWindow::BuildChannelBox(size_t c)
{
    ChannelUi& ui = m_ch.at(c);
    ui.box        = new QGroupBox(tr("CH%1").arg(c + 1));
    ui.box->setCheckable(true);
    ui.box->setStyleSheet(
        QStringLiteral("QGroupBox::title { color: %1; }").arg(Theme::kChannel.at(c).name()));
    auto* form = new QFormLayout(ui.box);
    form->setVerticalSpacing(5);

    ui.vdiv     = new QComboBox;
    ui.coupling = new Segmented({tr("DC"), tr("AC"), tr("GND")});
    ui.probe = new Segmented({QStringLiteral("x1"), QStringLiteral("x10"), QStringLiteral("x100")});
    ui.coupling->SetToolTips({tr("DC: the whole signal, including its average level"),
                              tr("AC: removes the average level, shows only the changes"),
                              tr("GND: shows the 0 V line")});
    ui.coupling->SetAccent(Theme::kChannel.at(c));
    ui.probe->SetAccent(Theme::kChannel.at(c));
    const QString probeTip = tr("Must match the switch on the probe");
    ui.probe->SetToolTips({probeTip, probeTip, probeTip});
    ui.invert = new QCheckBox(tr("Inv"));
    ui.invert->setToolTip(tr("Invert the trace"));
    ui.offset = new QDoubleSpinBox;
    ui.offset->setRange(-kMaxOffsetDiv, kMaxOffsetDiv);
    ui.offset->setSingleStep(kOffsetStepDiv);
    ui.offset->setSuffix(tr(" div"));
    ui.offset->setToolTip(tr("Position on screen; you can also drag the marker at the left edge"));
    auto* zero = new QPushButton(tr("0"));
    zero->setFixedWidth(30);
    zero->setToolTip(tr("Back to the centre"));
    auto* offRow = new QHBoxLayout;
    offRow->addWidget(ui.offset, 1);
    offRow->addWidget(zero);
    offRow->addWidget(ui.invert);

    ui.box->setToolTip(tr("Turn this channel on or off"));
    ui.vdiv->setToolTip(tr("Volts per division. Ctrl+wheel on the screen changes it too.\n"
                           "The signal must fit within 4 divisions above and below 0 V."));
    form->addRow(tr("Scale"), ui.vdiv);
    form->addRow(tr("Input"), ui.coupling);
    form->addRow(tr("Probe"), ui.probe);
    form->addRow(tr("Position"), offRow);

    const auto ch = [this, c]() -> Core::ChannelSettings& { return m_acqSettings.ch.at(c); };
    connect(ui.box, &QGroupBox::toggled, this,
            [this, ch](bool on)
            {
                if (!m_syncing)
                {
                    ch().enabled = on;
                    ApplyAcq();
                }
            });
    connect(ui.vdiv, &QComboBox::currentIndexChanged, this,
            [this, ch, c](int i)
            {
                if (!m_syncing && i >= 0)
                {
                    ch().vdivIndex = static_cast<size_t>(i);
                    m_selChannel   = static_cast<int>(c);
                    ApplyAcq();
                    SyncUi();
                }
            });
    connect(ui.coupling, &Segmented::IndexChanged, this,
            [this, ch](int i)
            {
                ch().coupling = static_cast<Core::Coupling>(i);
                ApplyAcq();
            });
    connect(ui.probe, &Segmented::IndexChanged, this,
            [this, ch, c](int i)
            {
                const int old = ch().probe;
                ch().probe    = Core::kProbeChoices.at(static_cast<size_t>(i));
                if (m_viewSettings.trig.source == static_cast<Core::TriggerSource>(c))
                {
                    m_viewSettings.trig.levelV *= static_cast<double>(ch().probe) / old;
                }
                ApplyAcq();
                SyncUi();
            });
    connect(ui.invert, &QCheckBox::toggled, this,
            [this, ch](bool on)
            {
                if (!m_syncing)
                {
                    ch().invert = on;
                    ApplyAcq();
                }
            });
    connect(ui.offset, &QDoubleSpinBox::valueChanged, this,
            [this, c](double v)
            {
                if (!m_syncing)
                {
                    m_display.offsetDiv.at(c) = v;
                    m_selChannel              = static_cast<int>(c);
                    UpdateView();
                }
            });
    connect(zero, &QPushButton::clicked, ui.offset, [o = ui.offset] { o->setValue(0); });
    return ui.box;
}

QWidget* MainWindow::BuildSidePanel()
{
    auto* panel = new QWidget;
    auto* v     = new QVBoxLayout(panel);
    v->setContentsMargins(6, 2, 6, 6);
    v->setSpacing(6);
    v->addWidget(BuildChannelBox(0));
    v->addWidget(BuildChannelBox(1));

    // horizontal
    auto* hBox = new QGroupBox(tr("Horizontal"));
    auto* hf   = new QFormLayout(hBox);
    hf->setVerticalSpacing(5);
    m_timeCombo = new QComboBox;
    for (const double s : m_timeSteps)
    {
        m_timeCombo->addItem(Si(s, "s", 3) + tr("/div"));
    }
    m_delayLabel = new QLabel;
    auto* left   = new QPushButton(QStringLiteral("◀"));
    auto* center = new QPushButton(tr("0"));
    auto* right  = new QPushButton(QStringLiteral("▶"));
    for (QPushButton* b : {left, center, right})
    {
        b->setFixedWidth(30);
    }
    left->setToolTip(tr("Show later samples; or drag the screen"));
    right->setToolTip(tr("Show earlier samples; or drag the screen"));
    center->setToolTip(tr("Back to the trigger point; or double-click the screen"));
    auto* delayRow = new QHBoxLayout;
    delayRow->addWidget(m_delayLabel, 1);
    delayRow->addWidget(left);
    delayRow->addWidget(center);
    delayRow->addWidget(right);
    m_timeCombo->setToolTip(tr("Time per division. The mouse wheel on the screen changes it too."));
    hf->addRow(tr("Scale"), m_timeCombo);
    hf->addRow(tr("Delay"), delayRow);
    v->addWidget(hBox);
    connect(m_timeCombo, &QComboBox::currentIndexChanged, this,
            [this](int i)
            {
                if (!m_syncing && i >= 0)
                {
                    m_timeIndex = static_cast<size_t>(i);
                    ApplyAcq();
                }
            });
    const auto shiftDelay = [this](double divs)
    {
        m_viewSettings.delaySec =
            ClampDelay(m_viewSettings.delaySec + divs * m_viewSettings.secPerDiv);
        UpdateView();
    };
    connect(left, &QPushButton::clicked, this, [shiftDelay] { shiftDelay(1.0); });
    connect(right, &QPushButton::clicked, this, [shiftDelay] { shiftDelay(-1.0); });
    connect(center, &QPushButton::clicked, this,
            [this]
            {
                m_viewSettings.delaySec = 0;
                UpdateView();
            });

    // trigger
    auto* tBox = new QGroupBox(tr("Trigger"));
    tBox->setStyleSheet(
        QStringLiteral("QGroupBox::title { color: %1; }").arg(Theme::kTrigger.name()));
    auto* tf = new QFormLayout(tBox);
    tf->setVerticalSpacing(5);
    m_trigMode   = new Segmented({tr("Auto"), tr("Normal"), tr("Single")});
    m_trigSource = new Segmented({tr("CH1"), tr("CH2"), tr("EXT")});
    m_trigEdge   = new Segmented({tr("↑ Rising"), tr("↓ Falling")});
    for (Segmented* s : {m_trigMode, m_trigSource, m_trigEdge})
    {
        s->SetAccent(Theme::kTrigger);
    }
    m_trigSource->SetToolTips({tr("Trigger on channel 1"), tr("Trigger on channel 2"),
                               tr("Trigger on the EXT input (hardware trigger only)")});
    m_trigEdge->SetToolTips({tr("Trigger when the signal goes up through the level"),
                             tr("Trigger when the signal goes down through the level")});
    m_trigMode->SetToolTips({tr("Show the signal even without a trigger"),
                             tr("Update the screen only on a trigger"),
                             tr("One triggered frame, then stop")});
    m_trigLevel = new QDoubleSpinBox;
    m_trigLevel->setDecimals(kLevelDecimals);
    m_trigLevel->setSuffix(QStringLiteral(" V"));
    m_trigLevel->setToolTip(tr("You can also drag the orange marker at the right edge"));
    auto* fifty = new QPushButton(tr("50%"));
    fifty->setFixedWidth(46);
    fifty->setToolTip(tr("Set the level to the middle of the signal"));
    auto* levelRow = new QHBoxLayout;
    levelRow->addWidget(m_trigLevel, 1);
    levelRow->addWidget(fifty);
    m_hwTrig = new QCheckBox(tr("Hardware trigger"));
    m_hwTrig->setToolTip(tr("Let the scope wait for the edge itself (always on for EXT).\n"
                            "Off: capture free-running and find the edge in software,\n"
                            "which allows any trigger level."));
    tf->addRow(tr("Mode"), m_trigMode);
    tf->addRow(tr("Source"), m_trigSource);
    tf->addRow(tr("Slope"), m_trigEdge);
    tf->addRow(tr("Level"), levelRow);
    tf->addRow(QString(), m_hwTrig);
    v->addWidget(tBox);
    connect(m_trigMode, &Segmented::IndexChanged, this,
            [this](int i)
            {
                m_viewSettings.trig.mode = static_cast<Core::TriggerMode>(i);
                if (m_viewSettings.trig.mode == Core::TriggerMode::kSingle)
                {
                    Single();
                }
                else
                {
                    ApplyAcq();
                }
            });
    connect(m_trigSource, &Segmented::IndexChanged, this,
            [this](int i)
            {
                m_viewSettings.trig.source = static_cast<Core::TriggerSource>(i);
                ApplyAcq();
                SyncUi();
            });
    connect(m_trigEdge, &Segmented::IndexChanged, this,
            [this](int i)
            {
                m_viewSettings.trig.edge = static_cast<Core::TriggerEdge>(i);
                ApplyAcq();
            });
    connect(m_trigLevel, &QDoubleSpinBox::valueChanged, this,
            [this](double lv)
            {
                if (!m_syncing)
                {
                    m_viewSettings.trig.levelV = lv;
                    UpdateView();
                }
            });
    connect(fifty, &QPushButton::clicked, this,
            [this]
            {
                if (m_lastData && m_viewSettings.trig.source != Core::TriggerSource::kExt)
                {
                    const auto& m =
                        m_lastData->meas.at(static_cast<size_t>(m_viewSettings.trig.source));
                    const double top  = m.at(static_cast<size_t>(Core::MeasureKind::kVtop));
                    const double base = m.at(static_cast<size_t>(Core::MeasureKind::kVbase));
                    if (std::isfinite(top) && std::isfinite(base))
                    {
                        m_viewSettings.trig.levelV = (top + base) / 2;
                        SyncUi();
                        UpdateView();
                    }
                }
            });
    connect(m_hwTrig, &QCheckBox::toggled, this,
            [this](bool on)
            {
                if (!m_syncing)
                {
                    m_viewSettings.trig.hardwareTrigger = on;
                    ApplyAcq();
                }
            });

    // what is needed now and then: folded away, so the panel fits without scrolling
    auto* acquire = new Section(tr("Acquire"), false);
    auto* af      = new QFormLayout(acquire->Body());
    af->setContentsMargins(6, 2, 2, 4);
    m_avgCombo = new QComboBox;
    for (const int n : kAverageChoices)
    {
        m_avgCombo->addItem(n == 1 ? tr("Normal") : tr("Average %1").arg(n));
    }
    m_avgCombo->setToolTip(tr("Average several triggered frames to reduce noise.\n"
                              "Needs a stable trigger; do not use it on digital buses."));
    af->addRow(tr("Mode"), m_avgCombo);
    v->addWidget(acquire);
    connect(m_avgCombo, &QComboBox::currentIndexChanged, this,
            [this](int i)
            {
                if (!m_syncing && i >= 0)
                {
                    m_viewSettings.averages = kAverageChoices.at(static_cast<size_t>(i));
                    m_proc.ResetAverage();
                }
            });

    auto* math = new Section(tr("Math"), false);
    auto* mf   = new QFormLayout(math->Body());
    mf->setContentsMargins(6, 2, 2, 4);
    mf->setVerticalSpacing(5);
    m_mathOp = MakeCombo({tr("Off"), tr("A + B"), tr("A − B"), tr("A × B"), tr("FFT (A)")});
    m_mathA  = new Segmented({tr("CH1"), tr("CH2")});
    m_mathB  = new Segmented({tr("CH1"), tr("CH2")});
    m_mathA->SetAccent(Theme::kMath);
    m_mathB->SetAccent(Theme::kMath);
    m_fftWindow = new QComboBox;
    for (int w = 0; w <= static_cast<int>(Core::FftWindow::kFlatTop); ++w)
    {
        m_fftWindow->addItem(
            QString::fromUtf8(Core::FftWindowName(static_cast<Core::FftWindow>(w))));
    }
    m_mathOp->setToolTip(tr("Show a calculated trace in magenta, or the spectrum (FFT) below"));
    m_mathA->SetToolTips({tr("First input: channel 1"), tr("First input: channel 2")});
    m_mathB->SetToolTips({tr("Second input: channel 1"), tr("Second input: channel 2")});
    m_fftWindow->setToolTip(tr("Hann suits most signals; Flat top reads amplitudes best"));
    mf->addRow(tr("Function"), m_mathOp);
    mf->addRow(tr("A"), m_mathA);
    mf->addRow(tr("B"), m_mathB);
    mf->addRow(tr("Window"), m_fftWindow);
    v->addWidget(math);
    const auto mathChanged = [this]
    {
        if (m_syncing)
        {
            return;
        }
        m_viewSettings.math      = static_cast<App::MathOp>(m_mathOp->currentIndex());
        m_viewSettings.mathA     = static_cast<size_t>(m_mathA->Index());
        m_viewSettings.mathB     = static_cast<size_t>(m_mathB->Index());
        m_viewSettings.fftWindow = static_cast<Core::FftWindow>(m_fftWindow->currentIndex());
        UpdateView();
    };
    connect(m_mathOp, &QComboBox::currentIndexChanged, this, mathChanged);
    connect(m_fftWindow, &QComboBox::currentIndexChanged, this, mathChanged);
    connect(m_mathA, &Segmented::IndexChanged, this, mathChanged);
    connect(m_mathB, &Segmented::IndexChanged, this, mathChanged);

    auto* cursors = new Section(tr("Cursors"), false);
    auto* cf      = new QFormLayout(cursors->Body());
    cf->setContentsMargins(6, 2, 2, 4);
    m_cursorSrc = new Segmented({tr("CH1"), tr("CH2"), tr("Math")});
    m_cursorSrc->SetToolTips({tr("Y cursors read volts of channel 1"),
                              tr("Y cursors read volts of channel 2"),
                              tr("Y cursors read the math trace")});
    cf->addRow(tr("Y reads"), m_cursorSrc);
    v->addWidget(cursors);
    connect(m_cursorSrc, &Segmented::IndexChanged, this,
            [this](int i)
            {
                m_display.cursorSource = i;
                UpdateView();
            });

    auto* refs = new Section(tr("Reference"), false);
    auto* rg   = new QGridLayout(refs->Body());
    rg->setContentsMargins(6, 2, 2, 4);
    for (size_t i = 0; i < 2; ++i)
    {
        auto* save = new QPushButton(tr("CH%1 → R%1").arg(i + 1));
        save->setToolTip(tr("Store the trace on screen as a reference to compare with"));
        m_refShow.at(i) = new QCheckBox(tr("Show R%1").arg(i + 1));
        m_refShow.at(i)->setEnabled(false);
        m_refShow.at(i)->setToolTip(tr("Show or hide the stored reference"));
        m_refShow.at(i)->setStyleSheet(QStringLiteral("color: %1").arg(Theme::kRef.at(i).name()));
        rg->addWidget(save, static_cast<int>(i), 0);
        rg->addWidget(m_refShow.at(i), static_cast<int>(i), 1);
        connect(save, &QPushButton::clicked, this, [this, i] { SaveReference(i); });
        connect(m_refShow.at(i), &QCheckBox::toggled, this, [this](bool) { UpdateView(); });
    }
    v->addWidget(refs);
    v->addStretch(1);
    return panel;
}

QWidget* MainWindow::BuildBottom()
{
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);

    auto* badges = new QHBoxLayout;
    for (size_t c = 0; c < 2; ++c)
    {
        m_ch.at(c).badge = new QLabel;
        m_ch.at(c).badge->setStyleSheet(BadgeStyle(Theme::kChannel.at(c)));
        badges->addWidget(m_ch.at(c).badge);
    }
    m_timeBadge = new QLabel;
    m_trigBadge = new QLabel;
    m_trigBadge->setStyleSheet(BadgeStyle(Theme::kTrigger));
    m_rateBadge = new QLabel;
    badges->addSpacing(12);
    badges->addWidget(m_timeBadge);
    badges->addSpacing(12);
    badges->addWidget(m_trigBadge);
    badges->addStretch(1);
    badges->addWidget(m_rateBadge);
    v->addLayout(badges);

    // measurements: foldable, the trace gets the height back
    auto* meas = new Section(tr("Measurements"), true);
    auto* mv   = new QVBoxLayout(meas->Body());
    mv->setContentsMargins(0, 0, 0, 0);
    mv->setSpacing(4);
    auto* row = new QHBoxLayout;
    auto* add = new QToolButton;
    add->setText(tr("Add measurement ▾"));
    add->setObjectName(QStringLiteral("action"));
    add->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(add);
    for (int src = 0; src <= kMathSource; ++src)
    {
        QMenu* sub = menu->addMenu(ChannelName(src));
        for (size_t k = 0; k < Core::kMeasureCount; ++k)
        {
            const auto kind = static_cast<Core::MeasureKind>(k);
            sub->addAction(QString::fromUtf8(Core::GetMeasureInfo(kind).name), this,
                           [this, src, kind] { AddMeasurement(src, kind); });
        }
    }
    add->setMenu(menu);
    auto* clear = new QPushButton(tr("Clear all"));
    m_stats     = new QCheckBox(tr("Statistics"));
    m_stats->setChecked(true);
    auto* reset = new QPushButton(tr("Reset statistics"));
    add->setToolTip(tr("Choose a channel and a value to measure"));
    clear->setToolTip(tr("Remove all measurements"));
    m_stats->setToolTip(tr("Show mean, minimum and maximum"));
    reset->setToolTip(tr("Start the statistics again"));
    row->addWidget(add);
    row->addWidget(clear);
    row->addSpacing(16);
    row->addWidget(m_stats);
    row->addWidget(reset);
    row->addStretch(1);
    mv->addLayout(row);

    m_measTable = new QTableWidget(0, 6);
    m_measTable->setHorizontalHeaderLabels(
        {tr("Source"), tr("Measurement"), tr("Current"), tr("Mean"), tr("Min"), tr("Max")});
    m_measTable->setToolTip(tr("Measured on what is on screen. Right-click a row to remove it."));
    m_measTable->verticalHeader()->setVisible(false);
    m_measTable->verticalHeader()->setDefaultSectionSize(kMeasRowHeight);
    m_measTable->setShowGrid(false);
    m_measTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_measTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_measTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_measTable->setContextMenuPolicy(Qt::ActionsContextMenu);
    m_measTable->setFixedHeight(
        m_measTable->horizontalHeader()->sizeHint().height() +
        kMeasRowsVisible * m_measTable->verticalHeader()->defaultSectionSize() + 4);
    auto* remove = new QAction(tr("Remove"), m_measTable);
    remove->setShortcut(QKeySequence::Delete);
    m_measTable->addAction(remove);
    mv->addWidget(m_measTable);
    v->addWidget(meas);

    connect(remove, &QAction::triggered, this,
            [this]
            {
                const int r = m_measTable->currentRow();
                if (r >= 0 && r < static_cast<int>(m_meas.size()))
                {
                    m_meas.erase(m_meas.begin() + r);
                    m_measTable->removeRow(r);
                }
            });
    connect(clear, &QPushButton::clicked, this,
            [this]
            {
                m_meas.clear();
                m_measTable->setRowCount(0);
            });
    connect(reset, &QPushButton::clicked, this, &MainWindow::ResetStats);
    connect(m_stats, &QCheckBox::toggled, this,
            [this](bool on)
            {
                for (int col = 3; col < 6; ++col)
                {
                    m_measTable->setColumnHidden(col, !on);
                }
            });
    return w;
}

void MainWindow::ConnectView()
{
    connect(m_view, &ScopeView::OffsetDragged, this,
            [this](int c, double div)
            {
                m_display.offsetDiv.at(static_cast<size_t>(c)) = std::round(div * 50) / 50;
                m_selChannel                                   = c;
                m_syncing                                      = true;
                m_ch.at(static_cast<size_t>(c))
                    .offset->setValue(m_display.offsetDiv.at(static_cast<size_t>(c)));
                m_syncing = false;
                UpdateView();
            });
    connect(m_view, &ScopeView::TriggerLevelDragged, this,
            [this](double volts)
            {
                m_viewSettings.trig.levelV = volts;
                m_syncing                  = true;
                m_trigLevel->setValue(volts);
                m_syncing = false;
                UpdateView();
            });
    connect(m_view, &ScopeView::DelayDragged, this,
            [this](double sec)
            {
                m_viewSettings.delaySec = ClampDelay(sec);
                UpdateView();
            });
    connect(m_view, &ScopeView::TimeZoom, this, &MainWindow::ZoomTime);
    connect(m_view, &ScopeView::VoltsZoom, this, &MainWindow::ZoomVolts);
}

void MainWindow::ConnectAcquisition()
{
    connect(m_acq.get(), &App::Acquisition::FrameReady, this, &MainWindow::OnFrame);
    connect(m_acq.get(), &App::Acquisition::ConnectionChanged, this,
            [this](const QString& text, bool hw)
            {
                m_connLabel->setText(text);
                m_connLabel->setStyleSheet(hw ? QStringLiteral("color: #5f5")
                                              : QStringLiteral("color: #fc6"));
                statusBar()->showMessage(text, kStatusMs);
                ResetStats();
            });
    connect(m_acq.get(), &App::Acquisition::DriverProblem, this, &MainWindow::ShowDriverProblem);
    connect(m_acq.get(), &App::Acquisition::ErrorMessage, this,
            [this](const QString& text) { statusBar()->showMessage(text, kStatusMs); });
    connect(m_acq.get(), &App::Acquisition::CalibrationDone, this,
            [this](const App::ZeroCalTable& table)
            {
                for (size_t c = 0; c < Core::kChannelCount; ++c)
                {
                    m_acqSettings.ch.at(c).zeroCal = table.at(c);
                }
                ApplyAcq();
                SaveSettings();
                QMessageBox::information(this, tr("Calibration"),
                                         tr("Zero calibration finished and saved."));
            });
}

// ---------- model -> widgets / device ----------

void MainWindow::FillVdivCombo(size_t c)
{
    QComboBox*    combo = m_ch.at(c).vdiv;
    const QString first =
        Si(Core::kHwVoltsPerDiv.front() * m_acqSettings.ch.at(c).probe, "V", 3) + tr("/div");
    if (combo->count() > 0 && combo->itemText(0) == first)
    {
        return; // same probe: the list is already right
    }
    combo->clear();
    for (const double v : Core::kHwVoltsPerDiv)
    {
        combo->addItem(Si(v * m_acqSettings.ch.at(c).probe, "V", 3) + tr("/div"));
    }
}

void MainWindow::SyncUi()
{
    m_syncing = true;
    for (size_t c = 0; c < Core::kChannelCount; ++c)
    {
        const Core::ChannelSettings& s  = m_acqSettings.ch.at(c);
        ChannelUi&                   ui = m_ch.at(c);
        ui.box->setChecked(s.enabled);
        FillVdivCombo(c);
        ui.vdiv->setCurrentIndex(static_cast<int>(s.vdivIndex));
        ui.coupling->SetIndex(static_cast<int>(s.coupling));
        ui.probe->SetIndex(static_cast<int>(std::distance(
            Core::kProbeChoices.begin(),
            std::find(Core::kProbeChoices.begin(), Core::kProbeChoices.end(), s.probe))));
        ui.invert->setChecked(s.invert);
        ui.offset->setValue(m_display.offsetDiv.at(c));
    }
    m_timeCombo->setCurrentIndex(static_cast<int>(m_timeIndex));

    const Core::TriggerSettings& t = m_viewSettings.trig;
    m_trigMode->SetIndex(static_cast<int>(t.mode));
    m_trigSource->SetIndex(static_cast<int>(t.source));
    m_trigEdge->SetIndex(static_cast<int>(t.edge));
    const bool   ext = t.source == Core::TriggerSource::kExt;
    const double vdiv =
        ext ? 1.0 : m_acqSettings.ch.at(static_cast<size_t>(t.source)).VoltsPerDiv();
    m_trigLevel->setEnabled(!ext);
    m_trigLevel->setRange(-kLevelRangeDiv * vdiv, kLevelRangeDiv * vdiv);
    m_trigLevel->setSingleStep(vdiv * kLevelStepDiv);
    m_trigLevel->setValue(t.levelV);
    m_hwTrig->setChecked(t.hardwareTrigger || ext);
    m_hwTrig->setEnabled(!ext);

    m_avgCombo->setCurrentIndex(static_cast<int>(std::distance(
        kAverageChoices.begin(),
        std::find(kAverageChoices.begin(), kAverageChoices.end(), m_viewSettings.averages))));
    m_persist->SetIndex(static_cast<int>(m_display.persistence));
    m_drawMode->SetIndex(m_display.dots ? 1 : 0);
    m_xy->SetIndex(m_display.xy ? 1 : 0);
    m_mathOp->setCurrentIndex(static_cast<int>(m_viewSettings.math));
    m_mathA->SetIndex(static_cast<int>(m_viewSettings.mathA));
    m_mathB->SetIndex(static_cast<int>(m_viewSettings.mathB));
    m_fftWindow->setCurrentIndex(static_cast<int>(m_viewSettings.fftWindow));
    m_cursorMode->SetIndex(static_cast<int>(m_display.cursorMode));
    m_cursorSrc->SetIndex(m_display.cursorSource);
    m_syncing = false;
}

void MainWindow::ApplyAcq()
{
    m_viewSettings.secPerDiv = m_timeSteps.at(m_timeIndex);
    m_acqSettings.rateIndex  = Core::PickRateIndex(m_viewSettings.secPerDiv);
    m_acqSettings.trig       = m_viewSettings.trig;
    m_viewSettings.delaySec  = ClampDelay(m_viewSettings.delaySec);
    m_acq->SetSettings(m_acqSettings);
    ++m_sentGen;
    m_proc.ResetAverage();
    ResetStats();
    UpdateView();
}

double MainWindow::ClampDelay(double sec) const
{
    if (!m_running && m_lastFrame && m_lastData)
    {
        // stopped: the screen centre may go anywhere in the held record
        const double dt = m_lastFrame->Dt();
        return std::clamp(sec, -m_lastData->triggerIndex * dt,
                          (static_cast<double>(m_lastFrame->Len()) - m_lastData->triggerIndex) *
                              dt);
    }
    const double fs = Core::kBaseSampleRateHz / Core::kRateDividers.at(m_acqSettings.rateIndex);
    const double recordSec = static_cast<double>(Core::kRecordLen) / fs;
    const double maxSec =
        std::max(0.0, (recordSec - m_viewSettings.secPerDiv * Core::kHDivCount) / 2.0);
    return std::clamp(sec, -maxSec, maxSec);
}

void MainWindow::UpdateView()
{
    for (size_t c = 0; c < Core::kChannelCount; ++c)
    {
        m_display.chOn.at(c)        = m_acqSettings.ch.at(c).enabled;
        m_display.voltsPerDiv.at(c) = m_acqSettings.ch.at(c).VoltsPerDiv();
    }
    const auto a              = m_viewSettings.mathA;
    const auto b              = m_viewSettings.mathB;
    m_display.math            = m_viewSettings.math;
    m_display.mathVoltsPerDiv = m_viewSettings.math == App::MathOp::kMul
                                    ? m_display.voltsPerDiv.at(a) * m_display.voltsPerDiv.at(b)
                                    : m_display.voltsPerDiv.at(a);
    m_display.mathUnit =
        m_viewSettings.math == App::MathOp::kMul ? QStringLiteral("V²") : QStringLiteral("V");
    m_display.mathOffsetDiv = 0;
    m_display.secPerDiv     = m_viewSettings.secPerDiv;
    m_display.delaySec      = m_viewSettings.delaySec;
    m_display.trigLevelV    = m_viewSettings.trig.levelV;
    m_display.trigChannel   = m_viewSettings.trig.source == Core::TriggerSource::kExt
                                  ? -1
                                  : static_cast<int>(m_viewSettings.trig.source);
    for (size_t i = 0; i < 2; ++i)
    {
        m_display.refs.at(i) = m_refShow.at(i)->isChecked() ? m_refs.at(i) : std::nullopt;
    }
    m_view->SetState(m_display);
    UpdateBadges();
    if (!m_running && m_lastFrame)
    {
        RedrawStopped();
    }
}

void MainWindow::RedrawStopped()
{
    // Zoom and pan inside the held capture, like a bench scope. Auto: a window that no longer
    // holds the trigger point still shows the samples instead of nothing.
    App::ViewSettings v = m_viewSettings;
    v.trig.mode         = Core::TriggerMode::kAuto;
    v.averages          = 1;
    if (m_lastData)
    {
        v.heldTriggerIndex = m_lastData->triggerIndex; // stay on the edge that was captured
    }
    if (std::optional<App::DisplayData> d = m_proc.Process(*m_lastFrame, v))
    {
        m_view->SetData(*d);
        m_lastData = std::move(d);
    }
}

void MainWindow::UpdateBadges()
{
    static const std::array<QString, 3> kCoupling = {tr("DC"), tr("AC"), tr("GND")};
    for (size_t c = 0; c < Core::kChannelCount; ++c)
    {
        const Core::ChannelSettings& s = m_acqSettings.ch.at(c);
        m_ch.at(c).badge->setText(QStringLiteral("CH%1  %2/div  %3%4")
                                      .arg(c + 1)
                                      .arg(Si(s.VoltsPerDiv(), "V", 3))
                                      .arg(kCoupling.at(static_cast<size_t>(s.coupling)))
                                      .arg(s.invert ? tr("  inv") : QString()));
        if (m_ch.at(c).badge->isEnabled() != s.enabled)
        {
            m_ch.at(c).badge->setEnabled(s.enabled);
            m_ch.at(c).badge->setStyleSheet(
                BadgeStyle(s.enabled ? Theme::kChannel.at(c) : QColor(0x60, 0x60, 0x60)));
        }
    }
    m_timeBadge->setText(
        tr("H  %1/div   Delay %2")
            .arg(Si(m_viewSettings.secPerDiv, "s", 3), Si(m_viewSettings.delaySec, "s", 3)));
    m_delayLabel->setText(Si(m_viewSettings.delaySec, "s", 4));

    const Core::TriggerSettings& t = m_viewSettings.trig;
    const QString                edge =
        t.edge == Core::TriggerEdge::kRising ? QStringLiteral("↑") : QStringLiteral("↓");
    const QString src = t.source == Core::TriggerSource::kExt
                            ? tr("EXT")
                            : tr("CH%1").arg(static_cast<int>(t.source) + 1);
    m_trigBadge->setText(t.source == Core::TriggerSource::kExt
                             ? tr("T  EXT %1").arg(edge)
                             : tr("T  %1 %2 %3").arg(src, edge, Si(t.levelV, "V", 3)));
    const double fs = Core::kBaseSampleRateHz / Core::kRateDividers.at(m_acqSettings.rateIndex);
    m_rateBadge->setText(tr("%1   %2 pts").arg(Si(fs, "Sa/s", 3)).arg(Core::kRecordLen));
}

void MainWindow::ResetStats()
{
    for (MeasItem& m : m_meas)
    {
        m.count = 0;
    }
}

void MainWindow::SetTrigStatus(const QString& text, const QColor& color)
{
    if (m_trigStatus->text() == text)
    {
        return; // setStyleSheet re-polishes the widget: far too slow to run per frame
    }
    m_trigStatus->setText(text);
    m_trigStatus->setStyleSheet(
        QStringLiteral("QLabel { color: black; background: %1; border-radius: 3px;"
                       " padding: 3px 8px; font-weight: bold; }")
            .arg(color.name()));
}

void MainWindow::SetRunning(bool running)
{
    m_running = running;
    m_view->SetWaiting(false);
    m_acq->SetRunning(running);
    // Like the key on a bench scope: fixed label, the colour shows the state (green running, red
    // stopped). Changing the text would read as the action the button performs.
    m_runBtn->setToolTip(running ? tr("Running. Press to stop (Space)")
                                 : tr("Stopped. Press to run (Space)"));
    m_runBtn->setStyleSheet(
        running ? QStringLiteral(
                      "QPushButton { background: #1d7a2c; color: white; font-weight: bold; }")
                : QStringLiteral(
                      "QPushButton { background: #a51d1d; color: white; font-weight: bold; }"));
    if (!running)
    {
        SetTrigStatus(tr("STOP"), QColor(0xe0, 0x40, 0x40));
    }
}

// ---------- data ----------

void MainWindow::OnFrame(const App::FramePtr& frame)
{
    m_acq->FrameConsumed();
    if (!m_running || frame->settingsGen != m_sentGen)
    {
        return; // stale: taken before the last settings change
    }
    if (m_autosetStep > 0)
    {
        AutosetStep(*frame);
        return;
    }

    QElapsedTimer stageTimer;
    stageTimer.start();
    std::optional<App::DisplayData> d = m_proc.Process(*frame, m_viewSettings);
    const double processMs            = static_cast<double>(stageTimer.nsecsElapsed()) / 1e6;
    if (!d)
    {
        SetTrigStatus(tr("WAIT"), QColor(0xff, 0xb0, 0x30));
        m_view->SetWaiting(true);
        ProfileFrame(*frame, processMs, 0);
        return;
    }
    // Only a frame that reaches the screen may be held: after Stop, zoom and CSV must use what
    // is shown, not a later capture without a trigger.
    m_lastFrame = frame;
    stageTimer.restart();
    if (d->triggered)
    {
        SetTrigStatus(tr("T'D"), QColor(0x40, 0xd0, 0x60));
    }
    else
    {
        SetTrigStatus(tr("AUTO"), QColor(0xf0, 0xe0, 0x40));
    }
    m_view->SetWaiting(false);
    m_view->SetData(*d);
    UpdateMeasurements(*d);
    ProfileFrame(*frame, processMs, static_cast<double>(stageTimer.nsecsElapsed()) / 1e6);
    m_lastData = std::move(d);

    if (m_single)
    {
        m_single = false;
        SetRunning(false);
    }
}

void MainWindow::ProfileFrame(const Core::Frame& f, double processMs, double uiMs)
{
    if (m_profilePath.isEmpty())
    {
        return;
    }
    if (m_profCount == 0)
    {
        m_profTimer.start();
    }
    ++m_profCount;
    m_profSum.armMs += f.timing.armMs;
    m_profSum.waitMs += f.timing.waitMs;
    m_profSum.readMs += f.timing.readMs;
    m_profSum.convertMs += f.timing.convertMs;
    m_profProcessMs += processMs;
    m_profUiMs += uiMs;
    constexpr qint64 kReportMs = 1000;
    if (m_profTimer.elapsed() < kReportMs)
    {
        return;
    }
    QFile file(m_profilePath);
    if (file.open(QIODevice::Append | QIODevice::Text))
    {
        const double n = m_profCount;
        QTextStream(&file) << QStringLiteral("fps %1  arm %2  wait %3  usb-read %4  convert %5  "
                                             "process %6  ui %7  paint %8 ms\n")
                                  .arg(n * 1000.0 / static_cast<double>(m_profTimer.elapsed()), 0,
                                       'f', 1)
                                  .arg(m_profSum.armMs / n, 0, 'f', 2)
                                  .arg(m_profSum.waitMs / n, 0, 'f', 2)
                                  .arg(m_profSum.readMs / n, 0, 'f', 2)
                                  .arg(m_profSum.convertMs / n, 0, 'f', 2)
                                  .arg(m_profProcessMs / n, 0, 'f', 2)
                                  .arg(m_profUiMs / n, 0, 'f', 2)
                                  .arg(m_view->LastPaintMs(), 0, 'f', 2);
    }
    m_profCount     = 0;
    m_profSum       = {};
    m_profProcessMs = 0;
    m_profUiMs      = 0;
}

void MainWindow::AutosetStep(const Core::Frame& f)
{
    // Pick the smallest range that holds each signal (the DSO-2100 has no hardware offset, so
    // the signal must fit around 0 V), then a timebase showing a few periods.
    std::array<Core::MeasureValues, Core::kChannelCount> m{};
    size_t                                               best    = 0;
    double                                               bestAmp = -1;
    for (size_t c = 0; c < Core::kChannelCount; ++c)
    {
        Core::ChannelSettings& ch = m_acqSettings.ch.at(c);
        m.at(c)                   = Core::MeasureAll(f.volts.at(c), f.Dt());
        const double peak =
            std::max(std::fabs(m.at(c).at(static_cast<size_t>(Core::MeasureKind::kVmax))),
                     std::fabs(m.at(c).at(static_cast<size_t>(Core::MeasureKind::kVmin))));
        size_t idx = Core::kHwVoltsPerDiv.size() - 1;
        for (size_t i = 0; i < Core::kHwVoltsPerDiv.size(); ++i)
        {
            if (peak <= kAutosetFillDiv * Core::kHwVoltsPerDiv.at(i) * ch.probe)
            {
                idx = i;
                break;
            }
        }
        ch.vdivIndex       = idx;
        const double amp   = m.at(c).at(static_cast<size_t>(Core::MeasureKind::kVpp));
        const double noise = kAutosetMinCounts * ch.VoltsPerDiv() / Core::kCountsPerDiv;
        if (amp / ch.VoltsPerDiv() > bestAmp && amp > noise)
        {
            bestAmp = amp / ch.VoltsPerDiv();
            best    = c;
        }
    }

    if (m_autosetStep == 1)
    {
        m_autosetStep = 2;
        ApplyAcq();
        return;
    }

    m_autosetStep = 0;
    if (bestAmp > 0)
    {
        const Core::MeasureValues& mb   = m.at(best);
        const double               freq = mb.at(static_cast<size_t>(Core::MeasureKind::kFreq));
        if (std::isfinite(freq) && freq > 0)
        {
            const double want = kAutosetPeriodsOnScreen / freq / Core::kHDivCount;
            m_timeIndex       = m_timeSteps.size() - 1;
            for (size_t i = 0; i < m_timeSteps.size(); ++i)
            {
                if (m_timeSteps.at(i) >= want * 0.999)
                {
                    m_timeIndex = i;
                    break;
                }
            }
        }
        m_viewSettings.trig.source = static_cast<Core::TriggerSource>(best);
        m_viewSettings.trig.levelV = (mb.at(static_cast<size_t>(Core::MeasureKind::kVtop)) +
                                      mb.at(static_cast<size_t>(Core::MeasureKind::kVbase))) /
                                     2;
    }
    ApplyAcq();
    SyncUi();
    statusBar()->showMessage(tr("Autoset done"), kStatusMs);
}

void MainWindow::AddMeasurement(int source, Core::MeasureKind kind)
{
    m_meas.push_back({source, kind});
    const int row = m_measTable->rowCount();
    m_measTable->insertRow(row);
    const QColor color =
        source == kMathSource ? Theme::kMath : Theme::kChannel.at(static_cast<size_t>(source));
    auto* src = new QTableWidgetItem(ChannelName(source));
    src->setForeground(color);
    m_measTable->setItem(row, 0, src);
    m_measTable->setItem(row, 1,
                         new QTableWidgetItem(QString::fromUtf8(Core::GetMeasureInfo(kind).name)));
    for (int col = 2; col < 6; ++col)
    {
        auto* it = new QTableWidgetItem(QStringLiteral("----"));
        it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_measTable->setItem(row, col, it);
    }
}

void MainWindow::UpdateMeasurements(const App::DisplayData& d)
{
    for (size_t r = 0; r < m_meas.size(); ++r)
    {
        MeasItem&                  m = m_meas[r];
        const Core::MeasureValues& vals =
            m.source == kMathSource ? d.mathMeas : d.meas.at(static_cast<size_t>(m.source));
        const double v    = vals.at(static_cast<size_t>(m.kind));
        const auto&  info = Core::GetMeasureInfo(m.kind);
        std::string  unit(info.unit);
        if (m.source == kMathSource && m_viewSettings.math == App::MathOp::kMul && unit == "V")
        {
            unit = "V²";
        }
        const auto fmt = [&](double x) { return QString::fromStdString(Core::FormatSi(x, unit)); };
        m.last         = v;
        if (std::isfinite(v))
        {
            m.min = m.count == 0 ? v : std::min(m.min, v);
            m.max = m.count == 0 ? v : std::max(m.max, v);
            m.sum = m.count == 0 ? v : m.sum + v;
            ++m.count;
        }
        const int row = static_cast<int>(r);
        m_measTable->item(row, 2)->setText(fmt(v));
        const bool has = m.count > 0;
        m_measTable->item(row, 3)->setText(has ? fmt(m.sum / static_cast<double>(m.count))
                                               : QStringLiteral("----"));
        m_measTable->item(row, 4)->setText(has ? fmt(m.min) : QStringLiteral("----"));
        m_measTable->item(row, 5)->setText(has ? fmt(m.max) : QStringLiteral("----"));
    }
}

// ---------- actions ----------

void MainWindow::RunStop()
{
    m_single = false;
    if (m_viewSettings.trig.mode == Core::TriggerMode::kSingle && !m_running)
    {
        m_single = true;
    }
    SetRunning(!m_running);
}

void MainWindow::Single()
{
    m_viewSettings.trig.mode = Core::TriggerMode::kSingle;
    m_single                 = true;
    SyncUi();
    ApplyAcq();
    SetRunning(true);
}

void MainWindow::Autoset()
{
    for (Core::ChannelSettings& ch : m_acqSettings.ch)
    {
        ch.enabled   = true;
        ch.coupling  = Core::Coupling::kDc;
        ch.invert    = false;
        ch.vdivIndex = Core::kHwVoltsPerDiv.size() - 1;
    }
    m_display.offsetDiv                 = {0, 0};
    m_display.xy                        = false;
    m_viewSettings.delaySec             = 0;
    m_viewSettings.averages             = 1;
    m_viewSettings.trig.mode            = Core::TriggerMode::kAuto;
    m_viewSettings.trig.edge            = Core::TriggerEdge::kRising;
    m_viewSettings.trig.hardwareTrigger = false;
    if (m_viewSettings.trig.source == Core::TriggerSource::kExt)
    {
        m_viewSettings.trig.source = Core::TriggerSource::kCh1;
    }
    m_timeIndex = static_cast<size_t>(
        std::distance(m_timeSteps.begin(), std::lower_bound(m_timeSteps.begin(), m_timeSteps.end(),
                                                            kAutosetStartSecPerDiv * 0.999)));
    m_autosetStep = 1;
    SyncUi();
    ApplyAcq();
    if (!m_running)
    {
        SetRunning(true);
    }
    statusBar()->showMessage(tr("Autoset..."));
}

void MainWindow::DefaultSetup()
{
    const std::array<Core::ZeroCal, 2> cal = {m_acqSettings.ch[0].zeroCal,
                                              m_acqSettings.ch[1].zeroCal};
    m_acqSettings                          = {};
    m_viewSettings                         = {};
    m_display                              = {};
    for (size_t c = 0; c < 2; ++c)
    {
        m_acqSettings.ch.at(c).zeroCal = cal.at(c); // calibration belongs to the scope, keep it
    }
    m_timeIndex = static_cast<size_t>(
        std::distance(m_timeSteps.begin(), std::lower_bound(m_timeSteps.begin(), m_timeSteps.end(),
                                                            kDefaultSecPerDiv * 0.999)));
    SyncUi();
    ApplyAcq();
    SetRunning(true);
}

void MainWindow::Calibrate()
{
    const auto answer = QMessageBox::question(
        this, tr("Calibrate zero"),
        tr("Disconnect everything from CH1 and CH2 (or short the inputs), then press OK.\n\n"
           "The zero level is measured on every range and saved."),
        QMessageBox::Ok | QMessageBox::Cancel);
    if (answer == QMessageBox::Ok)
    {
        statusBar()->showMessage(tr("Calibrating..."));
        m_acq->RequestCalibration();
    }
}

void MainWindow::SaveScreenshot()
{
    const QString name = QFileDialog::getSaveFileName(
        this, tr("Save screenshot"),
        QStringLiteral("dso2100-%1.png")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))),
        tr("PNG images (*.png)"));
    if (!name.isEmpty() && !centralWidget()->grab().save(name))
    {
        QMessageBox::warning(this, tr("Save screenshot"), tr("Could not write %1").arg(name));
    }
}

void MainWindow::SaveCsv()
{
    if (!m_lastFrame || !m_lastData)
    {
        return;
    }
    const QString name = QFileDialog::getSaveFileName(
        this, tr("Save waveform"),
        QStringLiteral("dso2100-%1.csv")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))),
        tr("CSV files (*.csv)"));
    if (name.isEmpty())
    {
        return;
    }
    QFile file(name);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, tr("Save waveform"), tr("Could not write %1").arg(name));
        return;
    }
    // the whole record, time relative to the trigger point
    QTextStream        out(&file);
    const Core::Frame& f = *m_lastFrame;
    out << "# DSO-2100, " << f.sampleRateHz << " Sa/s, " << f.Len() << " samples\n";
    out << "time_s,ch1_V,ch2_V\n";
    for (size_t i = 0; i < f.Len(); ++i)
    {
        const double t = (static_cast<double>(i) - m_lastData->triggerIndex) * f.Dt();
        out << QString::number(t, 'g', 9) << ',' << f.volts[0][i] << ',' << f.volts[1][i] << '\n';
    }
}

void MainWindow::SaveReference(size_t slot)
{
    if (!m_lastData || !m_lastData->ch.at(slot).valid)
    {
        return;
    }
    m_refShow.at(slot)->setEnabled(true);
    m_refs.at(slot) = RefTrace{m_lastData->ch.at(slot), m_acqSettings.ch.at(slot).VoltsPerDiv(),
                               m_display.offsetDiv.at(slot)};
    if (m_refShow.at(slot)->isChecked())
    {
        UpdateView();
    }
    else
    {
        m_refShow.at(slot)->setChecked(true);
    }
}

void MainWindow::ShowDriverProblem(const QString& hint)
{
    if (m_driverBox == nullptr)
    {
        m_driverBox = new QMessageBox(QMessageBox::Warning, tr("USB driver needed"), QString(),
                                      QMessageBox::Ok, this);
        m_driverBox->setModal(false);
    }
    m_driverBox->setText(hint);
    m_driverBox->show();
    m_driverBox->raise();
}

void MainWindow::ZoomTime(int steps)
{
    const auto next = static_cast<int>(m_timeIndex) + steps;
    m_timeIndex =
        static_cast<size_t>(std::clamp(next, 0, static_cast<int>(m_timeSteps.size()) - 1));
    SyncUi();
    ApplyAcq();
}

void MainWindow::ZoomVolts(int steps)
{
    Core::ChannelSettings& ch   = m_acqSettings.ch.at(static_cast<size_t>(m_selChannel));
    const auto             next = static_cast<int>(ch.vdivIndex) + steps;
    ch.vdivIndex =
        static_cast<size_t>(std::clamp(next, 0, static_cast<int>(Core::kHwVoltsPerDiv.size()) - 1));
    SyncUi();
    ApplyAcq();
}

// ---------- settings ----------

void MainWindow::LoadSettings()
{
    QSettings s;
    restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(s.value(QStringLiteral("window/state")).toByteArray());
    for (size_t c = 0; c < Core::kChannelCount; ++c)
    {
        s.beginGroup(QStringLiteral("ch%1").arg(c + 1));
        Core::ChannelSettings& ch = m_acqSettings.ch.at(c);
        ch.enabled                = s.value(QStringLiteral("enabled"), ch.enabled).toBool();
        ch.vdivIndex =
            std::min(s.value(QStringLiteral("vdiv"), static_cast<uint>(ch.vdivIndex)).toUInt(),
                     static_cast<uint>(Core::kHwVoltsPerDiv.size() - 1));
        ch.coupling = static_cast<Core::Coupling>(
            std::clamp(s.value(QStringLiteral("coupling"), 0).toInt(), 0, 2));
        const int probe = s.value(QStringLiteral("probe"), 1).toInt();
        ch.probe  = std::find(Core::kProbeChoices.begin(), Core::kProbeChoices.end(), probe) !=
                            Core::kProbeChoices.end()
                        ? probe
                        : 1;
        ch.invert = s.value(QStringLiteral("invert"), false).toBool();
        m_display.offsetDiv.at(c) = s.value(QStringLiteral("offset"), 0.0).toDouble();
        const QVariantList cal    = s.value(QStringLiteral("zeroCal")).toList();
        if (cal.size() == static_cast<qsizetype>(ch.zeroCal.size()))
        {
            for (size_t i = 0; i < ch.zeroCal.size(); ++i)
            {
                ch.zeroCal.at(i) = cal.at(static_cast<qsizetype>(i)).toDouble();
            }
        }
        s.endGroup();
    }
    s.beginGroup(QStringLiteral("trigger"));
    Core::TriggerSettings& t = m_viewSettings.trig;
    t.mode                   = static_cast<Core::TriggerMode>(
        std::clamp(s.value(QStringLiteral("mode"), 0).toInt(), 0, 1)); // never start in Single
    t.source = static_cast<Core::TriggerSource>(
        std::clamp(s.value(QStringLiteral("source"), 0).toInt(), 0, 2));
    t.edge = static_cast<Core::TriggerEdge>(
        std::clamp(s.value(QStringLiteral("edge"), 0).toInt(), 0, 1));
    t.levelV          = s.value(QStringLiteral("level"), 0.0).toDouble();
    t.hardwareTrigger = s.value(QStringLiteral("hardware"), false).toBool();
    s.endGroup();

    m_timeIndex =
        std::min(s.value(QStringLiteral("time/index"), static_cast<uint>(m_timeIndex)).toUInt(),
                 static_cast<uint>(m_timeSteps.size() - 1));
    m_viewSettings.delaySec = s.value(QStringLiteral("time/delay"), 0.0).toDouble();
    const int avg           = s.value(QStringLiteral("acquire/averages"), 1).toInt();
    m_viewSettings.averages =
        std::find(kAverageChoices.begin(), kAverageChoices.end(), avg) != kAverageChoices.end()
            ? avg
            : 1;
    m_display.persistence = static_cast<Persistence>(
        std::clamp(s.value(QStringLiteral("display/persistence"), 0).toInt(), 0, 3));
    m_display.dots = s.value(QStringLiteral("display/dots"), false).toBool();
    m_display.xy   = s.value(QStringLiteral("display/xy"), false).toBool();
    m_viewSettings.math =
        static_cast<App::MathOp>(std::clamp(s.value(QStringLiteral("math/op"), 0).toInt(), 0, 4));
    m_viewSettings.mathA     = std::min(s.value(QStringLiteral("math/a"), 0).toUInt(), 1U);
    m_viewSettings.mathB     = std::min(s.value(QStringLiteral("math/b"), 1).toUInt(), 1U);
    m_viewSettings.fftWindow = static_cast<Core::FftWindow>(
        std::clamp(s.value(QStringLiteral("math/window"), 1).toInt(), 0, 4));
    m_display.cursorMode = static_cast<CursorMode>(
        std::clamp(s.value(QStringLiteral("cursor/mode"), 0).toInt(), 0, 3));
    m_display.cursorSource = std::clamp(s.value(QStringLiteral("cursor/source"), 0).toInt(), 0, 2);
    m_demoAction->setChecked(s.value(QStringLiteral("demo"), true).toBool());

    const QStringList meas =
        s.value(QStringLiteral("measurements"),
                QStringList{QStringLiteral("0:0"), QStringLiteral("0:2"), QStringLiteral("1:0"),
                            QStringLiteral("1:2"), QStringLiteral("1:9")})
            .toStringList();
    for (const QString& m : meas)
    {
        const QStringList parts = m.split(':');
        if (parts.size() == 2)
        {
            const int src  = parts[0].toInt();
            const int kind = parts[1].toInt();
            if (src >= 0 && src <= kMathSource && kind >= 0 &&
                kind < static_cast<int>(Core::kMeasureCount))
            {
                AddMeasurement(src, static_cast<Core::MeasureKind>(kind));
            }
        }
    }
    m_stats->setChecked(s.value(QStringLiteral("statistics"), true).toBool());
}

void MainWindow::SaveSettings() const
{
    QSettings s;
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("window/state"), saveState());
    for (size_t c = 0; c < Core::kChannelCount; ++c)
    {
        s.beginGroup(QStringLiteral("ch%1").arg(c + 1));
        const Core::ChannelSettings& ch = m_acqSettings.ch.at(c);
        s.setValue(QStringLiteral("enabled"), ch.enabled);
        s.setValue(QStringLiteral("vdiv"), static_cast<uint>(ch.vdivIndex));
        s.setValue(QStringLiteral("coupling"), static_cast<int>(ch.coupling));
        s.setValue(QStringLiteral("probe"), ch.probe);
        s.setValue(QStringLiteral("invert"), ch.invert);
        s.setValue(QStringLiteral("offset"), m_display.offsetDiv.at(c));
        QVariantList cal;
        for (const double z : ch.zeroCal)
        {
            cal << z;
        }
        s.setValue(QStringLiteral("zeroCal"), cal);
        s.endGroup();
    }
    const Core::TriggerSettings& t = m_viewSettings.trig;
    s.setValue(QStringLiteral("trigger/mode"), static_cast<int>(t.mode));
    s.setValue(QStringLiteral("trigger/source"), static_cast<int>(t.source));
    s.setValue(QStringLiteral("trigger/edge"), static_cast<int>(t.edge));
    s.setValue(QStringLiteral("trigger/level"), t.levelV);
    s.setValue(QStringLiteral("trigger/hardware"), t.hardwareTrigger);
    s.setValue(QStringLiteral("time/index"), static_cast<uint>(m_timeIndex));
    s.setValue(QStringLiteral("time/delay"), m_viewSettings.delaySec);
    s.setValue(QStringLiteral("acquire/averages"), m_viewSettings.averages);
    s.setValue(QStringLiteral("display/persistence"), static_cast<int>(m_display.persistence));
    s.setValue(QStringLiteral("display/dots"), m_display.dots);
    s.setValue(QStringLiteral("display/xy"), m_display.xy);
    s.setValue(QStringLiteral("math/op"), static_cast<int>(m_viewSettings.math));
    s.setValue(QStringLiteral("math/a"), static_cast<uint>(m_viewSettings.mathA));
    s.setValue(QStringLiteral("math/b"), static_cast<uint>(m_viewSettings.mathB));
    s.setValue(QStringLiteral("math/window"), static_cast<int>(m_viewSettings.fftWindow));
    s.setValue(QStringLiteral("cursor/mode"), static_cast<int>(m_display.cursorMode));
    s.setValue(QStringLiteral("cursor/source"), m_display.cursorSource);
    s.setValue(QStringLiteral("demo"), m_demoAction->isChecked());
    QStringList meas;
    for (const MeasItem& m : m_meas)
    {
        meas << QStringLiteral("%1:%2").arg(m.source).arg(static_cast<int>(m.kind));
    }
    s.setValue(QStringLiteral("measurements"), meas);
    s.setValue(QStringLiteral("statistics"), m_stats->isChecked());
}

}
