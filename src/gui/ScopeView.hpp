#pragma once

// The scope screen: graticule, traces, persistence, markers, cursors, XY and the FFT pane.
// Mouse and touch: drag a channel marker (left edge) to move it, the trigger marker (right edge)
// to set the level, a cursor line to move it, the screen to shift the time; wheel or pinch zoom.

#include "app/Processor.hpp"

#include <QImage>
#include <QWidget>

#include <array>
#include <optional>

namespace Gui
{

enum class CursorMode : uint8_t
{
    kOff,
    kTime,
    kVoltage,
    kBoth,
};

enum class Persistence : uint8_t
{
    kOff,
    kShort,
    kLong,
    kInfinite,
};

// A stored reference waveform, drawn with the scale it was saved with.
struct RefTrace
{
    App::Trace trace;
    double     voltsPerDiv = 1;
    double     offsetDiv   = 0;
};

// Everything the view needs besides the data; set by the main window on every change.
struct ViewState
{
    std::array<bool, 2>   chOn            = {true, true};
    std::array<double, 2> voltsPerDiv     = {1, 1};
    std::array<double, 2> offsetDiv       = {0, 0};
    double                mathVoltsPerDiv = 1;
    double                mathOffsetDiv   = 0;
    QString               mathUnit        = QStringLiteral("V");
    App::MathOp           math            = App::MathOp::kOff;
    double                secPerDiv       = 1e-3;
    double                delaySec        = 0;
    double                trigLevelV      = 0;
    int                   trigChannel     = 0; // -1: EXT (no level marker)
    bool                  dots            = false;
    bool                  xy              = false;
    bool                  decodeOn        = false;          // show the decoded bus row
    bool                  gridTime        = false;          // time labels along the bottom
    std::array<bool, 2>   gridVolts       = {false, false}; // volts: CH1 left, CH2 right
    Persistence           persistence     = Persistence::kOff;
    CursorMode            cursorMode      = CursorMode::kOff;
    int                   cursorSource    = 0; // channel the voltage cursors read, 2 = math
    std::array<std::optional<RefTrace>, 2> refs;
};

class ScopeView : public QWidget
{
    Q_OBJECT

public:
    explicit ScopeView(QWidget* parent = nullptr);

    void             SetState(const ViewState& s);
    void             SetData(const App::DisplayData& d);
    void             ClearPersistence();
    // Normal/Single mode with no trigger lately: say so on screen, so a held trace does not
    // look like a frozen program.
    void             SetWaiting(bool waiting);
    double           LastPaintMs() const { return m_paintMs; } // duration of the last repaint
    const ViewState& State() const { return m_state; }

    QSize sizeHint() const override { return {800, 640}; }
    QSize minimumSizeHint() const override { return {320, 260}; }

signals:
    void OffsetDragged(int channel, double offsetDiv);
    void TriggerLevelDragged(double volts);
    void DelayDragged(double delaySec);
    void TimeZoom(int steps);  // + slower, - faster
    void VoltsZoom(int steps); // for the selected channel

protected:
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    bool event(QEvent* e) override;

private:
    enum class Drag : uint8_t
    {
        kNone,
        kOffset,
        kTrigger,
        kCursorX1,
        kCursorX2,
        kCursorY1,
        kCursorY2,
        kPan,
    };

    QRectF TimeRect() const;
    QRectF FftRect() const;
    double YToDiv(double y) const;
    double DivToY(double div) const;
    double XToDiv(double x) const;
    double DivToX(double div) const;

    void    DrawGrid(QPainter& p, const QRectF& r) const;
    void    DrawTrace(QPainter& p, const App::Trace& t, double vdiv, double offsetDiv,
                      const QColor& c) const;
    void    DrawXy(QPainter& p, const QColor& c) const;
    void    DrawSpectrum(QPainter& p) const;
    void    DrawDecode(QPainter& p) const;
    void    DrawGridLabels(QPainter& p) const;
    void    DrawMarkers(QPainter& p) const;
    void    DrawCursors(QPainter& p);
    void    UpdatePersistence();
    QString CursorText() const;

    ViewState                       m_state;
    std::optional<App::DisplayData> m_data;
    QImage                          m_persist;

    std::array<double, 2> m_curX = {3.0, 7.0}; // divisions
    std::array<double, 2> m_curY = {2.0, 6.0};

    Drag    m_drag        = Drag::kNone;
    int     m_dragChannel = 0;
    QPointF m_dragStart;
    double  m_dragStartValue = 0;
    double  m_pinchAccum     = 1.0;
    bool    m_waiting        = false;
    double  m_paintMs        = 0;
};

}
