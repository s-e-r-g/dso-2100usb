// Application stylesheet: one place for the look of every control. Dark, so the bright traces
// stay the brightest thing on screen; controls are at least 28 px high for touch.

#include "gui/Theme.hpp"

namespace Gui::Theme
{

QString StyleSheet()
{
    return QStringLiteral(R"(
QMainWindow, QWidget { font-size: 12px; }
QToolBar { background: #1b1e23; border: none; border-bottom: 1px solid #30343b; spacing: 10px;
           padding: 4px 6px; }
QToolTip { background: #2b2f36; color: #dcdfe4; border: 1px solid #5a616b; padding: 4px; }
QStatusBar { background: #1b1e23; color: #9aa1aa; }
QStatusBar::item { border: none; }
QScrollArea { border: none; background: transparent; }

QLabel#caption { color: #8b929b; font-size: 10px; font-weight: 600; }

QGroupBox { background: #1f2227; border: 1px solid #30343b; border-radius: 6px;
            margin-top: 10px; padding: 8px 6px 6px 6px; font-weight: 600; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }
QGroupBox QLabel { font-weight: 400; color: #aab1ba; }

QPushButton, QComboBox, QDoubleSpinBox {
    min-height: 26px; background: #2b2f36; color: #dcdfe4; border: 1px solid #3c4149;
    border-radius: 4px; padding: 0 8px; font-weight: 400; }
QPushButton:hover, QComboBox:hover, QDoubleSpinBox:hover { border-color: #5a616b; }
QPushButton:pressed { background: #3a3f47; }
QPushButton:disabled, QComboBox:disabled, QDoubleSpinBox:disabled { color: #6b7078; }
QComboBox::drop-down { border: none; width: 20px; }
QComboBox::down-arrow { image: url(:/icons/arrow-down.png); }
QDoubleSpinBox { padding-right: 18px; }
QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 16px; border: none;
                                                         background: transparent; }
QDoubleSpinBox::up-arrow { image: url(:/icons/arrow-up.png); }
QDoubleSpinBox::down-arrow { image: url(:/icons/arrow-down.png); }

QToolButton#action { min-height: 26px; background: #2b2f36; color: #dcdfe4; padding: 0 8px;
                     border: 1px solid #3c4149; border-radius: 4px; }
QToolButton#action:hover { border-color: #5a616b; }
QToolButton#action::menu-indicator { image: none; }

QToolButton[segment] { min-height: 26px; padding: 0 6px; background: #2b2f36; color: #c4c9d0;
                       border: 1px solid #3c4149; font-weight: 500; }
QToolButton[segment]:hover { background: #343941; }
QToolButton[segment]:checked { background: #3d8bfd; border-color: #3d8bfd; color: white; }
QToolButton[segment="first"] { border-top-left-radius: 4px; border-bottom-left-radius: 4px; }
QToolButton[segment="middle"] { border-left: none; }
QToolButton[segment="last"] { border-left: none; border-top-right-radius: 4px;
                              border-bottom-right-radius: 4px; }

QToolButton[section="true"] { min-height: 26px; border: none; background: transparent;
                              color: #c4c9d0; font-weight: 600; text-align: left; }
QToolButton[section="true"]:hover { color: white; }

QPlainTextEdit { background: #17191d; border: 1px solid #30343b; border-radius: 4px; }
QTableWidget { background: #17191d; border: 1px solid #30343b; border-radius: 4px; }
QHeaderView::section { background: #1f2227; color: #8b929b; border: none; padding: 3px 6px;
                       font-size: 10px; font-weight: 600; }
)");
}

}
