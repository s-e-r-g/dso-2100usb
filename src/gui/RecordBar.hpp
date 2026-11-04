#pragma once

// Overview of the whole record above the screen: a miniature of both traces with a window
// showing the part on screen. Drag the window or click to move in time, wheel to zoom,
// double-click to return to the trigger.

#include "gui/ScopeView.hpp"

#include <QWidget>

#include <array>

namespace Gui
{

class RecordBar : public QWidget
{
    Q_OBJECT

public:
    explicit RecordBar(QWidget* parent = nullptr);

    // The capture on screen; `view` gives the scale and position of each trace.
    void SetCapture(const App::FramePtr& frame, const App::DisplayData& d, const ViewState& view);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void DelayDragged(double delaySec); // the screen centre should go here
    void TimeZoom(int steps);           // + slower, - faster

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    double IndexToX(double index) const;
    double XToIndex(double x) const;
    QRectF WindowRect() const;
    void   MoveWindowTo(double centerIndex);
    void   DrawTrace(QPainter& p, size_t channel) const;

    App::FramePtr         m_frame;
    double                m_triggerIndex = 0;
    double                m_startIndex   = 0; // first sample on screen
    double                m_screenLen    = 0; // samples on screen
    std::array<bool, 2>   m_chOn         = {true, true};
    std::array<double, 2> m_offsetDiv    = {0, 0};
    bool                  m_dragging     = false;
    double                m_grabOffset   = 0; // samples from the window centre to the mouse
};

}
