// Entry point: application setup, dark theme, embedded firmware, main window.

#include "app/Acquisition.hpp"
#include "gui/MainWindow.hpp"

#include <QApplication>
#include <QFile>
#include <QMessageBox>
#include <QPalette>
#include <QStyleFactory>
#include <QTimer>

namespace
{

void ApplyDarkTheme(QApplication& app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette p;
    p.setColor(QPalette::Window, QColor(0x23, 0x26, 0x2b));
    p.setColor(QPalette::WindowText, QColor(0xdc, 0xdf, 0xe4));
    p.setColor(QPalette::Base, QColor(0x18, 0x1a, 0x1e));
    p.setColor(QPalette::AlternateBase, QColor(0x2a, 0x2d, 0x33));
    p.setColor(QPalette::ToolTipBase, QColor(0x30, 0x33, 0x39));
    p.setColor(QPalette::ToolTipText, QColor(0xdc, 0xdf, 0xe4));
    p.setColor(QPalette::Text, QColor(0xdc, 0xdf, 0xe4));
    p.setColor(QPalette::Button, QColor(0x32, 0x36, 0x3d));
    p.setColor(QPalette::ButtonText, QColor(0xdc, 0xdf, 0xe4));
    p.setColor(QPalette::Highlight, QColor(0x2f, 0x6f, 0xb0));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x70, 0x74, 0x7a));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x70, 0x74, 0x7a));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x70, 0x74, 0x7a));
    app.setPalette(p);
}

}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("dso2100"));
    QApplication::setApplicationName(QStringLiteral("DSO-2100 Scope"));
    QApplication::setApplicationVersion(QStringLiteral(DSO_VERSION));
    ApplyDarkTheme(app);

    qRegisterMetaType<App::FramePtr>();
    qRegisterMetaType<App::ZeroCalTable>();

    QFile fw(QStringLiteral(":/firmware/dso2100-firmware.hex"));
    if (!fw.open(QIODevice::ReadOnly))
    {
        QMessageBox::critical(nullptr, QObject::tr("DSO-2100"),
                              QObject::tr("The embedded firmware image is missing."));
        return 1;
    }

    Gui::MainWindow w(fw.readAll());
    w.resize(1400, 900);
    w.show();

    // Smoke test: DSO_SCREENSHOT=<file.png> saves the window after a few frames and exits
    // (works headless with QT_QPA_PLATFORM=offscreen; used by CI and for docs).
    const QString shot = qEnvironmentVariable("DSO_SCREENSHOT");
    if (!shot.isEmpty())
    {
        constexpr int kShotDelayMs = 3000;
        QTimer::singleShot(kShotDelayMs, &w,
                           [&w, shot] { QApplication::exit(w.grab().save(shot) ? 0 : 1); });
    }
    return QApplication::exec();
}
