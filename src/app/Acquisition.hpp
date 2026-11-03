#pragma once

// Acquisition thread. Finds the scope, uploads the firmware when it is still in bootloader mode
// (the job of the original first-stage driver), captures frames and falls back to the simulator
// when no scope is connected. Public functions may be called from the GUI thread.

#include "core/Scope.hpp"

#include <QByteArray>
#include <QMutex>
#include <QString>
#include <QThread>

#include <atomic>
#include <memory>

namespace Hw
{
class Dso2100Usb;
class ScopeBackend;
}

namespace App
{

using FramePtr     = std::shared_ptr<const Core::Frame>;
using ZeroCalTable = std::array<Core::ZeroCal, Core::kChannelCount>;

class Acquisition : public QThread
{
    Q_OBJECT

public:
    explicit Acquisition(QByteArray firmwareHex, QObject* parent = nullptr);
    ~Acquisition() override; // stops and joins the thread

    void SetSettings(const Core::AcqSettings& s); // aborts the capture in progress
    void SetRunning(bool running);
    void SetDemoAllowed(bool allowed);
    void RequestCalibration(); // inputs must be disconnected; result in CalibrationDone()
    void FrameConsumed();      // the GUI calls this per FrameReady(), for back-pressure

signals:
    void FrameReady(App::FramePtr frame);
    void ConnectionChanged(const QString& text, bool hardware);
    void DriverProblem(const QString& hint); // device present but not accessible
    void CalibrationDone(const App::ZeroCalTable& table);
    void ErrorMessage(const QString& text);

protected:
    void run() override;

private:
    void PollDevice(Hw::Dso2100Usb& usb);
    void Calibrate(Hw::ScopeBackend& backend);
    void SetConnection(const QString& text, bool hardware);

    const QByteArray  m_firmwareHex;
    QMutex            m_mutex; // guards m_settings and m_settingsGen
    Core::AcqSettings m_settings;
    uint64_t          m_settingsGen = 1;

    std::atomic<bool> m_abort{false};
    std::atomic<bool> m_quit{false};
    std::atomic<bool> m_running{true};
    std::atomic<bool> m_demoAllowed{true};
    std::atomic<bool> m_calibrate{false};
    std::atomic<int>  m_framesInFlight{0};

    // acquisition thread only
    uint64_t m_seq = 0;
    QString  m_connText;
    QString  m_lastHint;
};

}

Q_DECLARE_METATYPE(App::FramePtr)
Q_DECLARE_METATYPE(App::ZeroCalTable)
