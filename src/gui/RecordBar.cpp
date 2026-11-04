// Record overview: one min/max column per pixel, with the part off screen dimmed, so the bright
// window reads as a lens over the capture.

#include "gui/RecordBar.hpp"

#include "core/Format.hpp"
#include "gui/Theme.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace Gui
{

namespace
{

constexpr int    kHeightPx     = 38;
constexpr int    kMinWidthPx   = 200;
constexpr double kMarginPx     = 24; // lines the bar up with the graticule of the scope view
constexpr double kMinWindowPx  = 6;  // deep zoom: the window must stay visible and grabbable
constexpr double kMarkerHalfPx = 4;
constexpr int    kLabelFontPx  = 10;
constexpr int    kWheelStep    = 120;
constexpr int    kTraceAlpha   = 200;

const QColor kBarBackground{0x10, 0x12, 0x16};
const QColor kOutsideShade{0, 0, 0, 165};
const QColor kWindowFill{255, 255, 255, 16};
const QColor kWindowBorder{0xdc, 0xdf, 0xe4};
const QColor kLabel{0x8b, 0x92, 0x9b};

}

RecordBar::RecordBar(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    setToolTip(tr("The whole capture. Drag the window or click to move in time,\n"
                  "wheel to zoom, double-click to return to the trigger."));
}

QSize RecordBar::sizeHint() const
{
    return {800, kHeightPx};
}

QSize RecordBar::minimumSizeHint() const
{
    return {kMinWidthPx, kHeightPx};
}

void RecordBar::SetCapture(const App::FramePtr& frame, const App::DisplayData& d,
                           const ViewState& view)
{
    m_frame        = frame;
    m_triggerIndex = d.triggerIndex;
    m_startIndex   = d.screenStartIndex;
    m_screenLen    = d.samplesPerDiv * Core::kHDivCount;
    m_chOn         = view.chOn;
    m_offsetDiv    = view.offsetDiv;
    update();
}

double RecordBar::IndexToX(double index) const
{
    const double len = m_frame ? static_cast<double>(m_frame->Len()) : 1.0;
    return kMarginPx + index / len * (width() - 2 * kMarginPx);
}

double RecordBar::XToIndex(double x) const
{
    const double len = m_frame ? static_cast<double>(m_frame->Len()) : 1.0;
    return (x - kMarginPx) / (width() - 2 * kMarginPx) * len;
}

QRectF RecordBar::WindowRect() const
{
    const double x0     = IndexToX(m_startIndex);
    const double x1     = IndexToX(m_startIndex + m_screenLen);
    const double w      = std::max(x1 - x0, kMinWindowPx);
    const double center = (x0 + x1) / 2;
    return {center - w / 2, 1.0, w, height() - 2.0};
}

void RecordBar::DrawTrace(QPainter& p, size_t channel) const
{
    const std::vector<float>& s = m_frame->volts.at(channel);
    if (s.empty())
    {
        return;
    }
    const double vdiv   = m_frame->ch.at(channel).VoltsPerDiv();
    const double offset = m_offsetDiv.at(channel);
    const double h      = height() - 4.0;
    const auto   yOf    = [&](float v)
    { return std::clamp(height() / 2.0 - (v / vdiv + offset) / Core::kVDivCount * h, 2.0, h + 2); };

    QColor c = Theme::kChannel.at(channel);
    c.setAlpha(kTraceAlpha);
    p.setPen(c);
    const int x0 = static_cast<int>(kMarginPx);
    const int x1 = width() - x0;
    for (int x = x0; x < x1; ++x)
    {
        const auto a = static_cast<size_t>(std::max(0.0, XToIndex(x)));
        const auto b = std::min(s.size(), static_cast<size_t>(std::max(0.0, XToIndex(x + 1))) + 1);
        if (a >= b)
        {
            continue;
        }
        const auto [lo, hi] = std::minmax_element(s.begin() + static_cast<std::ptrdiff_t>(a),
                                                  s.begin() + static_cast<std::ptrdiff_t>(b));
        p.drawLine(QPointF(x, yOf(*hi)), QPointF(x, yOf(*lo)));
    }
}

void RecordBar::paintEvent(QPaintEvent* /*e*/)
{
    QPainter p(this);
    p.fillRect(rect(), kBarBackground);
    if (!m_frame || m_frame->Len() == 0)
    {
        return;
    }
    for (size_t c = 0; c < m_chOn.size(); ++c)
    {
        if (m_chOn.at(c))
        {
            DrawTrace(p, c);
        }
    }

    // dim what is off screen, then frame what is on it
    const QRectF win = WindowRect();
    p.fillRect(QRectF(0, 0, win.left(), height()), kOutsideShade);
    p.fillRect(QRectF(win.right(), 0, width() - win.right(), height()), kOutsideShade);
    p.fillRect(win, kWindowFill);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(kWindowBorder, m_dragging ? 2.0 : 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(win.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);

    // trigger position
    const double tx = IndexToX(m_triggerIndex);
    p.setPen(QPen(Theme::kTrigger, 1));
    p.drawLine(QPointF(tx, 0), QPointF(tx, height()));
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::kTrigger);
    const QPointF marker[] = {
        {tx - kMarkerHalfPx, 0}, {tx + kMarkerHalfPx, 0}, {tx, 2 * kMarkerHalfPx}};
    p.drawPolygon(marker, 3);
    p.setRenderHint(QPainter::Antialiasing, false);

    // whole record on the left, share on screen on the right
    QFont f = font();
    f.setPixelSize(kLabelFontPx);
    p.setFont(f);
    p.setPen(kLabel);
    const double  len    = static_cast<double>(m_frame->Len());
    const QString record = QString::fromStdString(Core::FormatSi(len * m_frame->Dt(), "s", 3));
    const QString zoom   = QStringLiteral("×%1").arg(len / m_screenLen, 0, 'f', 1);
    p.drawText(QRectF(2, 0, kMarginPx * 2, height()), Qt::AlignLeft | Qt::AlignTop, record);
    p.drawText(QRectF(width() - 2 * kMarginPx - 2, 0, kMarginPx * 2, height()),
               Qt::AlignRight | Qt::AlignTop, zoom);
}

void RecordBar::MoveWindowTo(double centerIndex)
{
    if (!m_frame)
    {
        return;
    }
    const double len  = static_cast<double>(m_frame->Len());
    const double half = std::min(m_screenLen, len) / 2;
    const double c    = std::clamp(centerIndex, half, len - half);
    emit         DelayDragged((c - m_triggerIndex) * m_frame->Dt());
}

void RecordBar::mousePressEvent(QMouseEvent* e)
{
    if (!m_frame || e->button() != Qt::LeftButton)
    {
        return;
    }
    const double x      = e->position().x();
    const double center = m_startIndex + m_screenLen / 2;
    m_dragging          = true;
    m_grabOffset        = WindowRect().contains(e->position()) ? XToIndex(x) - center : 0;
    setCursor(Qt::ClosedHandCursor);
    MoveWindowTo(XToIndex(x) - m_grabOffset);
    update();
}

void RecordBar::mouseMoveEvent(QMouseEvent* e)
{
    if (m_dragging)
    {
        MoveWindowTo(XToIndex(e->position().x()) - m_grabOffset);
    }
    else if (m_frame)
    {
        setCursor(WindowRect().contains(e->position()) ? Qt::OpenHandCursor
                                                       : Qt::PointingHandCursor);
    }
}

void RecordBar::mouseReleaseEvent(QMouseEvent* /*e*/)
{
    m_dragging = false;
    unsetCursor();
    update();
}

void RecordBar::mouseDoubleClickEvent(QMouseEvent* /*e*/)
{
    emit DelayDragged(0.0);
}

void RecordBar::wheelEvent(QWheelEvent* e)
{
    const int steps = e->angleDelta().y() / kWheelStep;
    if (steps != 0)
    {
        emit TimeZoom(-steps);
        e->accept();
    }
}

}
