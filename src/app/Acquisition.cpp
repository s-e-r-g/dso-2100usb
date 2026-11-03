// Acquisition loop: device discovery and the two-stage start-up, capture, conversion to volts.

#include "app/Acquisition.hpp"

#include "hw/DemoBackend.hpp"
#include "hw/Dso2100Usb.hpp"

#include <QElapsedTimer>

#include <exception>

namespace App
{

namespace
{

constexpr qint64 kProbeIntervalMs     = 1000;
constexpr qint64 kReenumerateMaxMs    = 8000; // firmware upload -> 0547:1002 shows up
constexpr int    kReenumeratePollMs   = 250;
constexpr int    kIdleSleepMs         = 20;
constexpr int    kMaxFramesInFlight   = 2;
constexpr size_t kCalibrationRateIdx  = 6; // 500 kS/s: quick, and noise averages out
constexpr int    kCalibrationCaptures = 2; // the first one lets the front end settle

}

Acquisition::Acquisition(QByteArray firmwareHex, QObject* parent)
    : QThread(parent), m_firmwareHex(std::move(firmwareHex))
{
}

Acquisition::~Acquisition()
{
    m_quit  = true;
    m_abort = true;
    wait();
}

void Acquisition::SetSettings(const Core::AcqSettings& s)
{
    {
        const QMutexLocker lock(&m_mutex);
        m_settings = s;
        ++m_settingsGen;
    }
    m_abort = true;
}

void Acquisition::SetRunning(bool running)
{
    m_running = running;
    if (!running)
    {
        m_abort = true;
    }
}

void Acquisition::SetDemoAllowed(bool allowed)
{
    m_demoAllowed = allowed;
}

void Acquisition::RequestCalibration()
{
    m_calibrate = true;
    m_abort     = true;
}

void Acquisition::FrameConsumed()
{
    --m_framesInFlight;
}

void Acquisition::SetConnection(const QString& text, bool hardware)
{
    if (text != m_connText)
    {
        m_connText = text;
        emit ConnectionChanged(text, hardware);
    }
}

void Acquisition::PollDevice(Hw::Dso2100Usb& usb)
{
    const Hw::DeviceProbe p = usb.Probe();
    if (!p.openError.empty())
    {
        const QString hint = QString::fromStdString(Hw::DriverHint(p.openErrorPid));
        if (hint != m_lastHint)
        {
            m_lastHint = hint;
            emit DriverProblem(hint);
        }
    }

    if (p.runningPresent && p.openErrorPid != Hw::Dso2100::kPidRunning)
    {
        if (usb.Open())
        {
            m_lastHint.clear();
        }
        else
        {
            emit ErrorMessage(QString::fromStdString(usb.LastError()));
        }
        return;
    }
    if (!p.bootloaderPresent || p.openErrorPid == Hw::Dso2100::kPidBootloader)
    {
        return;
    }

    // Stage 1: firmware upload, then wait for the device to come back as 0547:1002.
    SetConnection(tr("Loading firmware..."), false);
    if (!usb.UploadFirmware(m_firmwareHex.toStdString()))
    {
        emit ErrorMessage(QString::fromStdString(usb.LastError()));
        return;
    }
    QElapsedTimer t;
    t.start();
    while (!m_quit && t.elapsed() < kReenumerateMaxMs)
    {
        msleep(kReenumeratePollMs);
        const Hw::DeviceProbe again = usb.Probe();
        if (again.runningPresent)
        {
            if (again.openErrorPid == Hw::Dso2100::kPidRunning)
            {
                const QString hint = QString::fromStdString(Hw::DriverHint(again.openErrorPid));
                m_lastHint         = hint;
                emit DriverProblem(hint);
                return;
            }
            if (!usb.Open())
            {
                emit ErrorMessage(QString::fromStdString(usb.LastError()));
            }
            return;
        }
    }
    emit ErrorMessage(tr("Firmware was uploaded but the scope did not re-enumerate as 0547:1002."));
}

void Acquisition::Calibrate(Hw::ScopeBackend& backend)
{
    Core::AcqSettings s;
    {
        const QMutexLocker lock(&m_mutex);
        s = m_settings;
    }
    s.rateIndex            = kCalibrationRateIdx;
    s.trig.hardwareTrigger = false;
    s.trig.source          = Core::TriggerSource::kCh1;

    ZeroCalTable            table{};
    const std::atomic<bool> never{false};
    for (size_t r = 0; r < Core::kHwVoltsPerDiv.size(); ++r)
    {
        for (Core::ChannelSettings& ch : s.ch)
        {
            ch.vdivIndex = r;
            ch.coupling  = Core::Coupling::kDc;
        }
        Core::Frame f;
        for (int i = 0; i < kCalibrationCaptures; ++i)
        {
            if (backend.Acquire(s, f, never) != Hw::AcquireResult::kOk)
            {
                emit ErrorMessage(
                    tr("Calibration failed: %1").arg(QString::fromStdString(backend.LastError())));
                return;
            }
        }
        for (size_t c = 0; c < Core::kChannelCount; ++c)
        {
            double sum = 0;
            for (const uint8_t v : f.raw.at(c))
            {
                sum += v;
            }
            table.at(c).at(r) = sum / static_cast<double>(f.raw.at(c).size());
        }
    }
    emit CalibrationDone(table);
}

void Acquisition::run()
{
    Hw::Dso2100Usb  usb;
    Hw::DemoBackend demo;
    const bool      usbOk = usb.Init();
    if (!usbOk)
    {
        emit ErrorMessage(QString::fromStdString(usb.LastError()));
    }

    QElapsedTimer probeTimer;
    probeTimer.start();
    bool firstProbe = true;
    while (!m_quit)
    {
        try
        {
            if (usbOk && !usb.IsOpen() && (firstProbe || probeTimer.elapsed() > kProbeIntervalMs))
            {
                firstProbe = false;
                probeTimer.restart();
                PollDevice(usb);
            }

            Hw::ScopeBackend* backend = nullptr;
            if (usb.IsOpen())
            {
                backend = &usb;
                SetConnection(tr("DSO-2100 connected"), true);
            }
            else if (m_demoAllowed)
            {
                backend = &demo;
                SetConnection(tr("Demo signals (no scope)"), false);
            }
            else
            {
                SetConnection(tr("No scope connected"), false);
            }

            if (backend != nullptr && m_calibrate.exchange(false))
            {
                m_abort = false;
                Calibrate(*backend);
                continue;
            }
            if (backend == nullptr || !m_running || m_framesInFlight >= kMaxFramesInFlight)
            {
                msleep(kIdleSleepMs);
                continue;
            }

            Core::AcqSettings s;
            uint64_t          gen = 0;
            {
                const QMutexLocker lock(&m_mutex);
                s       = m_settings;
                gen     = m_settingsGen;
                m_abort = false;
            }

            Core::Frame       f;
            Hw::AcquireResult r = backend->Acquire(s, f, m_abort);
            if (r == Hw::AcquireResult::kTimeout)
            {
                // Auto mode: no hardware trigger in time, so take a free-running capture.
                Core::AcqSettings freeRun    = s;
                freeRun.trig.hardwareTrigger = false;
                freeRun.trig.source          = Core::TriggerSource::kCh1;
                r                            = backend->Acquire(freeRun, f, m_abort);
            }
            if (r == Hw::AcquireResult::kAborted)
            {
                continue;
            }
            if (r == Hw::AcquireResult::kError)
            {
                emit ErrorMessage(QString::fromStdString(backend->LastError()));
                if (backend == &usb)
                {
                    usb.Close(); // probably unplugged: probe again
                }
                msleep(kIdleSleepMs);
                continue;
            }

            f.seq         = ++m_seq;
            f.settingsGen = gen;
            f.ch          = s.ch;
            f.trig        = s.trig;
            Core::ConvertToVolts(f);
            ++m_framesInFlight;
            emit FrameReady(std::make_shared<const Core::Frame>(std::move(f)));
        }
        catch (const std::exception& e)
        {
            // never let an exception end the thread silently
            emit ErrorMessage(tr("Acquisition error: %1").arg(QString::fromUtf8(e.what())));
            msleep(kProbeIntervalMs);
        }
    }
}

}
