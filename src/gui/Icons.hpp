#pragma once

// Button icons, drawn in code: no image files, and they follow the button colours.

#include <QColor>
#include <QIcon>

#include <cstdint>

namespace Gui
{

enum class Glyph : uint8_t
{
    kVectors,
    kDots,
    kRun,
    kSingle,
    kStop,
    kRising,
    kFalling,
    kTimeView,
    kXyView,
    kCursorX,
    kCursorY,
};

// An icon in colour `off`, or `on` while the button is checked.
QIcon MakeIcon(Glyph glyph, const QColor& off, const QColor& on);

}
