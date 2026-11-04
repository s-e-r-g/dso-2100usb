// Draws the glyphs on a 20 x 14 grid, at three times the size so they stay sharp when scaled.

#include "gui/Icons.hpp"

#include <QPainter>
#include <QPixmap>
#include <QPolygonF>

#include <array>
#include <cmath>
#include <numbers>

namespace Gui
{

namespace
{

constexpr int    kWidth     = 20;
constexpr int    kHeight    = 14;
constexpr int    kScale     = 3;
constexpr double kLineWidth = 1.6;
constexpr double kDotSize   = 2.4;

// One wave: the samples of the Vectors and Dots icons, so the two read as the same signal.
constexpr auto kWave = std::to_array<QPointF>(
    {{1.5, 9.5}, {5.0, 3.5}, {8.5, 10.5}, {12.0, 3.0}, {15.5, 9.0}, {18.5, 4.5}});

void Draw(QPainter& p, Glyph glyph, const QColor& color)
{
    p.setPen(QPen(color, kLineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    switch (glyph)
    {
    case Glyph::kVectors: p.drawPolyline(kWave.data(), static_cast<int>(kWave.size())); break;
    case Glyph::kDots:
        p.setPen(QPen(color, kDotSize, Qt::SolidLine, Qt::RoundCap));
        p.drawPoints(kWave.data(), static_cast<int>(kWave.size()));
        break;
    case Glyph::kRun:
    case Glyph::kSingle:
    {
        const double x = glyph == Glyph::kRun ? 6.0 : 4.5;
        QPolygonF    arrow;
        arrow << QPointF(x, 2) << QPointF(x + 9, 7) << QPointF(x, 12);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPolygon(arrow);
        if (glyph == Glyph::kSingle)
        {
            p.drawRect(QRectF(x + 9.5, 2, 2, 10)); // stops after one frame
        }
        break;
    }
    case Glyph::kStop:
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRect(QRectF(5, 2, 10, 10));
        break;
    case Glyph::kTimeView:
    case Glyph::kXyView:
    {
        // a sine against time, and the figure two sines make against each other
        constexpr int kSteps = 48;
        QPolygonF     line;
        for (int i = 0; i <= kSteps; ++i)
        {
            const double a = 2 * std::numbers::pi * i / kSteps;
            line << (glyph == Glyph::kTimeView
                         ? QPointF(2 + 16.0 * i / kSteps, 7 - 4.5 * std::sin(a))
                         : QPointF(10 + 7 * std::sin(a), 7 - 5 * std::sin(2 * a)));
        }
        p.drawPolyline(line);
        break;
    }
    case Glyph::kCursorX:
        p.drawLine(QPointF(5, 1.5), QPointF(5, 12.5));
        p.drawLine(QPointF(15, 1.5), QPointF(15, 12.5));
        p.setPen(QPen(color, 1.0));
        p.drawLine(QPointF(7, 7), QPointF(13, 7));
        break;
    case Glyph::kCursorY:
        p.drawLine(QPointF(3, 3), QPointF(17, 3));
        p.drawLine(QPointF(3, 11), QPointF(17, 11));
        p.setPen(QPen(color, 1.0));
        p.drawLine(QPointF(10, 5), QPointF(10, 9));
        break;
    case Glyph::kRising:
    {
        constexpr auto kEdge = std::to_array<QPointF>({{2, 11}, {8, 11}, {12, 3}, {18, 3}});
        p.drawPolyline(kEdge.data(), static_cast<int>(kEdge.size()));
        break;
    }
    case Glyph::kFalling:
    {
        constexpr auto kEdge = std::to_array<QPointF>({{2, 3}, {8, 3}, {12, 11}, {18, 11}});
        p.drawPolyline(kEdge.data(), static_cast<int>(kEdge.size()));
        break;
    }
    }
}

QPixmap Render(Glyph glyph, const QColor& color)
{
    QPixmap px(kWidth * kScale, kHeight * kScale);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(kScale, kScale);
    Draw(p, glyph, color);
    p.end();
    px.setDevicePixelRatio(kScale);
    return px;
}

}

QIcon MakeIcon(Glyph glyph, const QColor& off, const QColor& on)
{
    QIcon icon;
    icon.addPixmap(Render(glyph, off), QIcon::Normal, QIcon::Off);
    icon.addPixmap(Render(glyph, on), QIcon::Normal, QIcon::On);
    return icon;
}

}
