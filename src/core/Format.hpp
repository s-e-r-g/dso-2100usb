#pragma once

// Engineering-notation formatting for readouts ("1.25 kHz", "500 mV").

#include <string>
#include <string_view>

namespace Core
{

// `digits` significant digits with an SI prefix; NaN or infinity prints "----".
std::string FormatSi(double value, std::string_view unit, int digits = 4);

}
