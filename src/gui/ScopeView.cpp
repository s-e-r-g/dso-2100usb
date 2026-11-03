// Scope screen painting and interaction. Long records are drawn as one min/max column per
// pixel, so 15000 samples per channel cost no more than a few hundred lines.

#include "gui/ScopeView.hpp"

#include "core/Format.hpp"
#include "gui/Theme.hpp"

#include <QGestureEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPinchGesture>

#include <algorithm>
#include <cmath>

namespace Gui
{

namespace
{

constexpr double kMarginLeft   = 24;
constexpr double kMarginRight  = 24;
constexpr double kMarginTop    = 14;
constexpr double kMarginBottom = 8;
constexpr double kFftGap       = 20;
constexpr double kTimeShareFft = 0.56; // of the height when the FFT pane is shown
constexpr double kGrabPx       = 8;
constexpr double kTickDiv      = 0.2;
constexpr double kTickPx       = 4;
constexpr double kFftTopDbv    = 20;
constexpr double kFftDbPerDiv  = 10;
constexpr double kPinchStep    = 1.3;
constexpr double kMaxOffsetDiv = 5;
constexpr int    kWheelStep    = 120;

// persistence: share of the old image kept per new frame
constexpr auto kDecay = std::to_array<double>({0.0, 0.75, 0.93, 1.0});

QString Si(double v, const char* unit)
{
    return QString::fromStdString(Core::FormatSi(v, unit));
}

}

ScopeView::ScopeView(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_AcceptTouchEvents);
    grabGesture(Qt::PinchGesture);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void ScopeView::SetState(const ViewState& s)
{
    const bool scaleChanged = s.secPerDiv != m_state.secPerDiv || s.delaySec != m_state.delaySec ||
                              s.voltsPerDiv != m_state.voltsPerDiv ||
                              s.offsetDiv != m_state.offsetDiv || s.xy != m_state.xy ||
                              s.math != m_state.math;
    m_state                 = s;
    if (scaleChanged)
    {
        ClearPersistence();
    }
    update();
}

void ScopeView::SetData(const App::DisplayData& d)
{
    m_data = d;
    UpdatePersistence();
    update();
}

void ScopeView::SetWaiting(bool waiting)
{
    if (waiting != m_waiting)
    {
        m_waiting = waiting;
        update();
    }
}

void ScopeView::ClearPersistence()
{
    if (!m_persist.isNull())
    {
        m_persist.fill(Qt::transparent);
    }
}

void ScopeView::resizeEvent(QResizeEvent* /*e*/)
{
    m_persist = QImage(size(), QImage::Format_ARGB32_Premultiplied);
    m_persist.fill(Qt::transparent);
}

// ---------- geometry ----------

QRectF ScopeView::TimeRect() const
{
    QRectF r(kMarginLeft, kMarginTop, width() - kMarginLeft - kMarginRight,
             height() - kMarginTop - kMarginBottom);
    if (m_state.math == App::MathOp::kFft)
    {
        r.setHeight(r.height() * kTimeShareFft - kFftGap / 2);
    }
    return r;
}

QRectF ScopeView::FftRect() const
{
    const QRectF t = TimeRect();
    return {t.left(), t.bottom() + kFftGap, t.width(),
            height() - kMarginBottom - t.bottom() - kFftGap};
}

double ScopeView::DivToY(double div) const
{
    const QRectF r = TimeRect();
    return r.center().y() - div * r.height() / Core::kVDivCount;
}

double ScopeView::YToDiv(double y) const
{
    const QRectF r = TimeRect();
    return (r.center().y() - y) / (r.height() / Core::kVDivCount);
}

double ScopeView::DivToX(double div) const
{
    const QRectF r = TimeRect();
    return r.left() + div * r.width() / Core::kHDivCount;
}

double ScopeView::XToDiv(double x) const
{
    const QRectF r = TimeRect();
    return (x - r.left()) / (r.width() / Core::kHDivCount);
}

// ---------- painting ----------

void ScopeView::DrawGrid(QPainter& p, const QRectF& r) const
{
    const double dx = r.width() / Core::kHDivCount;
    const double dy = r.height() / Core::kVDivCount;
    QPen         pen(Theme::kGrid, 1, Qt::DotLine);
    p.setPen(pen);
    for (int i = 1; i < Core::kHDivCount; ++i)
    {
        p.drawLine(QPointF(r.left() + i * dx, r.top()), QPointF(r.left() + i * dx, r.bottom()));
    }
    for (int i = 1; i < Core::kVDivCount; ++i)
    {
        p.drawLine(QPointF(r.left(), r.top() + i * dy), QPointF(r.right(), r.top() + i * dy));
    }
    pen.setStyle(Qt::SolidLine);
    pen.setColor(Theme::kGridAxis);
    p.setPen(pen);
    p.drawRect(r);
    const double cx = r.center().x();
    const double cy = r.center().y();
    for (double t = kTickDiv; t < Core::kHDivCount; t += kTickDiv)
    {
        const double x = r.left() + t * dx;
        p.drawLine(QPointF(x, cy - kTickPx), QPointF(x, cy + kTickPx));
    }
    for (double t = kTickDiv; t < Core::kVDivCount; t += kTickDiv)
    {
        const double y = r.top() + t * dy;
        p.drawLine(QPointF(cx - kTickPx, y), QPointF(cx + kTickPx, y));
    }
}

void ScopeView::DrawTrace(QPainter& p, const App::Trace& t, double vdiv, double offsetDiv,
                          const QColor& c) const
{
    if (!t.valid || t.v.empty())
    {
        return;
    }
    const QRectF r        = TimeRect();
    const double pxPerDiv = r.width() / Core::kHDivCount;
    const double pxPerSmp = t.dxDiv * pxPerDiv;
    const auto   xOf      = [&](size_t i)
    { return r.left() + (t.x0Div + static_cast<double>(i) * t.dxDiv) * pxPerDiv; };
    const auto yOf = [&](float v) { return DivToY(v / vdiv + offsetDiv); };

    p.setPen(QPen(c, 1.2));
    if (pxPerSmp < 0.5)
    {
        // several samples per pixel: one min/max column per pixel
        QPainterPath path;
        size_t       i     = 0;
        bool         first = true;
        while (i < t.v.size())
        {
            const double col = std::floor(xOf(i));
            float        lo  = t.v[i];
            float        hi  = t.v[i];
            for (; i < t.v.size() && std::floor(xOf(i)) == col; ++i)
            {
                lo = std::min(lo, t.v[i]);
                hi = std::max(hi, t.v[i]);
            }
            if (first)
            {
                path.moveTo(col, yOf(hi));
                first = false;
            }
            path.lineTo(col, yOf(hi));
            path.lineTo(col, yOf(lo));
        }
        p.drawPath(path);
        return;
    }
    if (m_state.dots)
    {
        p.setPen(QPen(c, 2.5, Qt::SolidLine, Qt::RoundCap));
        for (size_t i = 0; i < t.v.size(); ++i)
        {
            p.drawPoint(QPointF(xOf(i), yOf(t.v[i])));
        }
        return;
    }
    QPolygonF poly;
    poly.reserve(static_cast<qsizetype>(t.v.size()));
    for (size_t i = 0; i < t.v.size(); ++i)
    {
        poly << QPointF(xOf(i), yOf(t.v[i]));
    }
    p.drawPolyline(poly);
}

void ScopeView::DrawXy(QPainter& p, const QColor& c) const
{
    const App::Trace& a = m_data->ch[0];
    const App::Trace& b = m_data->ch[1];
    if (!a.valid || !b.valid)
    {
        return;
    }
    p.setPen(QPen(c, 1.5));
    const size_t n = std::min(a.v.size(), b.v.size());
    for (size_t i = 0; i < n; ++i)
    {
        const double x =
            DivToX(Core::kHDivCount / 2.0 + a.v[i] / m_state.voltsPerDiv[0] + m_state.offsetDiv[0]);
        const double y = DivToY(b.v[i] / m_state.voltsPerDiv[1] + m_state.offsetDiv[1]);
        p.drawPoint(QPointF(x, y));
    }
}

void ScopeView::DrawSpectrum(QPainter& p) const
{
    const QRectF r = FftRect();
    DrawGrid(p, r);
    if (!m_data || m_data->spectrumDbv.empty())
    {
        return;
    }
    const std::vector<float>& s       = m_data->spectrumDbv;
    const double              pxPerDb = r.height() / (Core::kVDivCount * kFftDbPerDiv);
    const auto                yOf     = [&](double db)
    { return std::clamp(r.top() + (kFftTopDbv - db) * pxPerDb, r.top(), r.bottom()); };

    p.save();
    p.setClipRect(r);
    p.setPen(QPen(Theme::kMath, 1.2));
    QPainterPath path;
    size_t       peak = 1;
    for (int col = 0; col < static_cast<int>(r.width()); ++col)
    {
        const auto b0 = static_cast<size_t>(static_cast<double>(col) / r.width() *
                                            static_cast<double>(s.size()));
        const auto b1 =
            std::max(b0 + 1, static_cast<size_t>(static_cast<double>(col + 1) / r.width() *
                                                 static_cast<double>(s.size())));
        float hi = -1000;
        for (size_t b = b0; b < std::min(b1, s.size()); ++b)
        {
            hi = std::max(hi, s[b]);
            if (b > 0 && s[b] > s[peak])
            {
                peak = b;
            }
        }
        const QPointF pt(r.left() + col, yOf(hi));
        if (col == 0)
        {
            path.moveTo(pt);
        }
        else
        {
            path.lineTo(pt);
        }
    }
    p.drawPath(path);
    p.restore();

    const double nyquist = m_data->sampleRateHz / 2.0;
    p.setPen(Theme::kText);
    p.drawText(QPointF(r.left() + 6, r.top() + 14),
               tr("FFT   %1/div   %2 dB/div   top %3 dBV   peak %4 @ %5")
                   .arg(Si(nyquist / Core::kHDivCount, "Hz"))
                   .arg(kFftDbPerDiv)
                   .arg(kFftTopDbv)
                   .arg(QString::number(s[peak], 'f', 1) + " dBV")
                   .arg(Si(static_cast<double>(peak) * m_data->spectrumBinHz, "Hz")));
}

void ScopeView::DrawMarkers(QPainter& p) const
{
    const QRectF r = TimeRect();
    QFont        f = font();
    f.setBold(true);
    p.setFont(f);

    // channel ground markers on the left edge
    for (int c = 0; c < 2; ++c)
    {
        if (!m_state.chOn.at(static_cast<size_t>(c)) || m_state.xy)
        {
            continue;
        }
        const double y =
            std::clamp(DivToY(m_state.offsetDiv.at(static_cast<size_t>(c))), r.top(), r.bottom());
        QPolygonF tag;
        tag << QPointF(2, y - 8) << QPointF(r.left() - 8, y - 8) << QPointF(r.left() - 1, y)
            << QPointF(r.left() - 8, y + 8) << QPointF(2, y + 8);
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::kChannel.at(static_cast<size_t>(c)));
        p.drawPolygon(tag);
        p.setPen(Qt::black);
        p.drawText(QRectF(2, y - 8, r.left() - 10, 16), Qt::AlignCenter, QString::number(c + 1));
    }

    // trigger level on the right edge
    if (m_state.trigChannel >= 0 && !m_state.xy)
    {
        const auto   tc = static_cast<size_t>(m_state.trigChannel);
        const double y  = std::clamp(
            DivToY(m_state.trigLevelV / m_state.voltsPerDiv.at(tc) + m_state.offsetDiv.at(tc)),
            r.top(), r.bottom());
        QPolygonF tag;
        tag << QPointF(width() - 2, y - 8) << QPointF(r.right() + 8, y - 8)
            << QPointF(r.right() + 1, y) << QPointF(r.right() + 8, y + 8)
            << QPointF(width() - 2, y + 8);
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::kTrigger);
        p.drawPolygon(tag);
        p.setPen(Qt::black);
        p.drawText(QRectF(r.right() + 8, y - 8, kMarginRight - 10, 16), Qt::AlignCenter,
                   QStringLiteral("T"));
        if (m_drag == Drag::kTrigger)
        {
            p.setPen(QPen(Theme::kTrigger, 1, Qt::DashLine));
            p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        }
    }

    // trigger position on the top edge
    if (!m_state.xy)
    {
        const double div = Core::kHDivCount / 2.0 - m_state.delaySec / m_state.secPerDiv;
        const double x   = std::clamp(DivToX(div), r.left(), r.right());
        QPolygonF    tag;
        tag << QPointF(x - 6, 1) << QPointF(x + 6, 1) << QPointF(x + 6, r.top() - 6)
            << QPointF(x, r.top() - 1) << QPointF(x - 6, r.top() - 6);
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::kTrigger);
        p.drawPolygon(tag);
    }
    p.setBrush(Qt::NoBrush);
    p.setFont(font());
}

QString ScopeView::CursorText() const
{
    QStringList lines;
    const auto  mode = m_state.cursorMode;
    if (mode == CursorMode::kTime || mode == CursorMode::kBoth)
    {
        const auto tAt = [&](double div)
        { return (div - Core::kHDivCount / 2.0) * m_state.secPerDiv + m_state.delaySec; };
        const double t1 = tAt(m_curX[0]);
        const double t2 = tAt(m_curX[1]);
        lines << QStringLiteral("X1 %1   X2 %2").arg(Si(t1, "s"), Si(t2, "s"));
        lines << QStringLiteral("ΔX %1   1/ΔX %2")
                     .arg(Si(t2 - t1, "s"), Si(1.0 / std::fabs(t2 - t1), "Hz"));
    }
    if (mode == CursorMode::kVoltage || mode == CursorMode::kBoth)
    {
        const bool        math = m_state.cursorSource == 2;
        const auto        src  = static_cast<size_t>(std::min(m_state.cursorSource, 1));
        const double      vdiv = math ? m_state.mathVoltsPerDiv : m_state.voltsPerDiv.at(src);
        const double      off  = math ? m_state.mathOffsetDiv : m_state.offsetDiv.at(src);
        const std::string unit = math ? m_state.mathUnit.toStdString() : "V";
        const auto   vAt = [&](double div) { return (div - Core::kVDivCount / 2.0 - off) * vdiv; };
        const double v1  = vAt(m_curY[0]);
        const double v2  = vAt(m_curY[1]);
        lines << QStringLiteral("Y1 %1   Y2 %2")
                     .arg(QString::fromStdString(Core::FormatSi(v1, unit)),
                          QString::fromStdString(Core::FormatSi(v2, unit)));
        lines << QStringLiteral("ΔY %1").arg(QString::fromStdString(Core::FormatSi(v2 - v1, unit)));
    }
    return lines.join('\n');
}

void ScopeView::DrawCursors(QPainter& p)
{
    const auto mode = m_state.cursorMode;
    if (mode == CursorMode::kOff)
    {
        return;
    }
    const QRectF r = TimeRect();
    p.setPen(QPen(Theme::kCursor, 1, Qt::DashLine));
    if (mode == CursorMode::kTime || mode == CursorMode::kBoth)
    {
        for (const double d : m_curX)
        {
            const double x = DivToX(d);
            p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        }
    }
    if (mode == CursorMode::kVoltage || mode == CursorMode::kBoth)
    {
        for (const double d : m_curY)
        {
            const double y = r.top() + (Core::kVDivCount - d) * r.height() / Core::kVDivCount;
            p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        }
    }
    const QString text = CursorText();
    const QRectF  box =
        p.fontMetrics().boundingRect(QRect(0, 0, 400, 200), 0, text).adjusted(-6, -4, 6, 4);
    const QRectF at(r.left() + 8, r.top() + 8, box.width(), box.height());
    p.fillRect(at, QColor(0, 0, 0, 190));
    p.setPen(Theme::kCursor);
    p.drawText(at.adjusted(6, 4, -6, -4), Qt::AlignLeft | Qt::AlignTop, text);
}

void ScopeView::UpdatePersistence()
{
    const auto mode = m_state.persistence;
    if (mode == Persistence::kOff || !m_data || m_persist.isNull())
    {
        return;
    }
    QPainter p(&m_persist);
    p.setRenderHint(QPainter::Antialiasing);
    if (mode != Persistence::kInfinite)
    {
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        p.fillRect(m_persist.rect(),
                   QColor(0, 0, 0, static_cast<int>(255 * kDecay.at(static_cast<size_t>(mode)))));
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }
    p.setClipRect(TimeRect());
    for (size_t c = 0; c < 2; ++c)
    {
        if (m_state.xy)
        {
            DrawXy(p, Theme::kChannel[0].darker(130));
            break;
        }
        if (m_state.chOn.at(c))
        {
            QColor col = Theme::kChannel.at(c);
            col.setAlpha(110);
            DrawTrace(p, m_data->ch.at(c), m_state.voltsPerDiv.at(c), m_state.offsetDiv.at(c), col);
        }
    }
}

void ScopeView::paintEvent(QPaintEvent* /*e*/)
{
    QPainter p(this);
    p.fillRect(rect(), Theme::kBackground);
    const QRectF r = TimeRect();
    DrawGrid(p, r);

    if (m_state.persistence != Persistence::kOff)
    {
        p.drawImage(0, 0, m_persist);
    }

    p.setRenderHint(QPainter::Antialiasing);
    p.save();
    p.setClipRect(r.adjusted(-1, -1, 1, 1));
    for (size_t i = 0; i < m_state.refs.size(); ++i)
    {
        if (m_state.refs.at(i) && !m_state.xy)
        {
            const RefTrace& ref = *m_state.refs.at(i);
            DrawTrace(p, ref.trace, ref.voltsPerDiv, ref.offsetDiv, Theme::kRef.at(i));
        }
    }
    if (m_data)
    {
        if (m_state.xy)
        {
            DrawXy(p, Theme::kChannel[0]);
        }
        else
        {
            for (size_t c = 0; c < 2; ++c)
            {
                if (m_state.chOn.at(c))
                {
                    DrawTrace(p, m_data->ch.at(c), m_state.voltsPerDiv.at(c),
                              m_state.offsetDiv.at(c), Theme::kChannel.at(c));
                }
            }
            if (m_data->math.valid && m_state.math != App::MathOp::kFft &&
                m_state.math != App::MathOp::kOff)
            {
                DrawTrace(p, m_data->math, m_state.mathVoltsPerDiv, m_state.mathOffsetDiv,
                          Theme::kMath);
            }
        }
    }
    p.restore();
    p.setRenderHint(QPainter::Antialiasing, false);

    DrawMarkers(p);
    DrawCursors(p);
    if (m_waiting)
    {
        p.setPen(Theme::kTrigger);
        p.drawText(TimeRect().adjusted(0, 6, 0, 0), Qt::AlignHCenter | Qt::AlignTop,
                   tr("Waiting for trigger  (no edge at the trigger level; Auto mode shows the "
                      "signal anyway)"));
    }
    if (m_state.math == App::MathOp::kFft)
    {
        DrawSpectrum(p);
    }
    if (!m_data)
    {
        p.setPen(Theme::kText);
        p.drawText(r, Qt::AlignCenter, tr("Waiting for data..."));
    }
}

// ---------- interaction ----------

void ScopeView::mousePressEvent(QMouseEvent* e)
{
    const QPointF pos = e->position();
    const QRectF  r   = TimeRect();
    m_dragStart       = pos;
    m_drag            = Drag::kNone;
    const auto mode   = m_state.cursorMode;

    if (mode == CursorMode::kTime || mode == CursorMode::kBoth)
    {
        for (size_t i = 0; i < 2; ++i)
        {
            if (std::fabs(pos.x() - DivToX(m_curX.at(i))) < kGrabPx && r.contains(pos))
            {
                m_drag = i == 0 ? Drag::kCursorX1 : Drag::kCursorX2;
                return;
            }
        }
    }
    if (mode == CursorMode::kVoltage || mode == CursorMode::kBoth)
    {
        for (size_t i = 0; i < 2; ++i)
        {
            const double y =
                r.top() + (Core::kVDivCount - m_curY.at(i)) * r.height() / Core::kVDivCount;
            if (std::fabs(pos.y() - y) < kGrabPx && r.contains(pos))
            {
                m_drag = i == 0 ? Drag::kCursorY1 : Drag::kCursorY2;
                return;
            }
        }
    }
    if (pos.x() < r.left() + kGrabPx)
    {
        double best = kGrabPx * 2;
        for (int c = 0; c < 2; ++c)
        {
            const double dist =
                std::fabs(pos.y() - DivToY(m_state.offsetDiv.at(static_cast<size_t>(c))));
            if (m_state.chOn.at(static_cast<size_t>(c)) && dist < best)
            {
                best          = dist;
                m_drag        = Drag::kOffset;
                m_dragChannel = c;
            }
        }
        if (m_drag != Drag::kNone)
        {
            return;
        }
    }
    if (pos.x() > r.right() - kGrabPx && m_state.trigChannel >= 0)
    {
        m_drag = Drag::kTrigger;
        mouseMoveEvent(e);
        return;
    }
    if (r.contains(pos) && e->button() == Qt::LeftButton)
    {
        m_drag           = Drag::kPan;
        m_dragStartValue = m_state.delaySec;
        setCursor(Qt::ClosedHandCursor);
    }
}

void ScopeView::mouseMoveEvent(QMouseEvent* e)
{
    const QPointF pos = e->position();
    const QRectF  r   = TimeRect();
    const double  vd = (r.bottom() - pos.y()) / (r.height() / Core::kVDivCount); // 0..8 from bottom
    switch (m_drag)
    {
    case Drag::kNone: break;
    case Drag::kOffset:
        emit OffsetDragged(m_dragChannel,
                           std::clamp(YToDiv(pos.y()), -kMaxOffsetDiv, kMaxOffsetDiv));
        break;
    case Drag::kTrigger:
    {
        const auto tc = static_cast<size_t>(m_state.trigChannel);
        emit       TriggerLevelDragged((YToDiv(pos.y()) - m_state.offsetDiv.at(tc)) *
                                       m_state.voltsPerDiv.at(tc));
        break;
    }
    case Drag::kCursorX1:
    case Drag::kCursorX2:
        m_curX.at(m_drag == Drag::kCursorX1 ? 0 : 1) = std::clamp(XToDiv(pos.x()), 0.0, 10.0);
        update();
        break;
    case Drag::kCursorY1:
    case Drag::kCursorY2:
        m_curY.at(m_drag == Drag::kCursorY1 ? 0 : 1) = std::clamp(vd, 0.0, 8.0);
        update();
        break;
    case Drag::kPan:
    {
        const double divs = (pos.x() - m_dragStart.x()) / (r.width() / Core::kHDivCount);
        emit         DelayDragged(m_dragStartValue - divs * m_state.secPerDiv);
        break;
    }
    }
}

void ScopeView::mouseReleaseEvent(QMouseEvent* /*e*/)
{
    m_drag = Drag::kNone;
    unsetCursor();
    update();
}

void ScopeView::mouseDoubleClickEvent(QMouseEvent* /*e*/)
{
    emit DelayDragged(0.0);
}

void ScopeView::wheelEvent(QWheelEvent* e)
{
    const int steps = e->angleDelta().y() / kWheelStep;
    if (steps == 0)
    {
        return;
    }
    if ((e->modifiers() & Qt::ControlModifier) != 0)
    {
        emit VoltsZoom(-steps);
    }
    else
    {
        emit TimeZoom(-steps);
    }
    e->accept();
}

bool ScopeView::event(QEvent* e)
{
    if (e->type() == QEvent::Gesture)
    {
        auto* ge = static_cast<QGestureEvent*>(e);
        if (auto* pinch = static_cast<QPinchGesture*>(ge->gesture(Qt::PinchGesture)))
        {
            if (pinch->state() == Qt::GestureStarted)
            {
                m_pinchAccum = 1.0;
            }
            m_pinchAccum *= pinch->scaleFactor();
            if (m_pinchAccum > kPinchStep)
            {
                emit TimeZoom(-1); // spread fingers: zoom in
                m_pinchAccum = 1.0;
            }
            else if (m_pinchAccum < 1.0 / kPinchStep)
            {
                emit TimeZoom(1);
                m_pinchAccum = 1.0;
            }
            ge->accept(pinch);
        }
        return true;
    }
    return QWidget::event(e);
}

}
