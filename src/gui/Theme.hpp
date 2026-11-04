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
inline const QColor                kGridLabel{0x6a, 0x72, 0x7a};
inline const QColor                kRunning{0x3f, 0xc8, 0x5a};
inline const QColor                kArmed{0xff, 0xb0, 0x30};
inline const QColor                kStopped{0xe8, 0x50, 0x50};
inline const QColor                kDecodeAddress{0x2f, 0x6f, 0xc0};
inline const QColor                kDecodeData{0x3a, 0x4a, 0x5c};
inline const QColor                kDecodeNack{0xc0, 0x30, 0x30};
inline const QColor                kDecodeStart{0x40, 0xd0, 0x60};
inline const QColor                kDecodeStop{0xe0, 0x60, 0x60};
inline const std::array<QColor, 2> kChannel = {QColor(0xf7, 0xe0, 0x1c), QColor(0x2c, 0xd5, 0xff)};
inline const std::array<QColor, 2> kRef     = {QColor(0xff, 0xa8, 0x60), QColor(0x9a, 0xff, 0x9a)};

QString StyleSheet(); // the look of all controls

}
