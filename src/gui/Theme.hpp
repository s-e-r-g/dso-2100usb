#pragma once

// Colours of the scope screen and the widgets that refer to a channel.

#include <QColor>
#include <QString>

#include <array>

namespace Gui::Theme
{

inline const QColor                kBackground{0x05, 0x07, 0x0a};
inline const QColor                kGrid{0x3c, 0x42, 0x48};
inline const QColor                kGridAxis{0x6a, 0x72, 0x7a};
inline const QColor                kText{0xd8, 0xdc, 0xe0};
inline const QColor                kTrigger{0xff, 0x8c, 0x1a};
inline const QColor                kMath{0xff, 0x4f, 0xd8};
inline const QColor                kCursor{0xf0, 0xf0, 0xf0};
inline const std::array<QColor, 2> kChannel = {QColor(0xf7, 0xe0, 0x1c), QColor(0x2c, 0xd5, 0xff)};
inline const std::array<QColor, 2> kRef     = {QColor(0xff, 0xa8, 0x60), QColor(0x9a, 0xff, 0x9a)};

QString StyleSheet(); // the look of all controls

}
