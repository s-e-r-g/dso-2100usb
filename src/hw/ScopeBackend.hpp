#pragma once

// Data source interface: the DSO-2100 over USB or the built-in simulator.
// Called from the acquisition thread only.

#include "core/Scope.hpp"

#include <atomic>
#include <cstdint>
#include <string>

namespace Hw
{

enum class AcquireResult : uint8_t
{
    kOk,
    kAborted, // `abort` was set while waiting
    kTimeout, // no hardware trigger in Auto mode: the caller retries free-running
    kError,   // see LastError(); the device is closed
};

class ScopeBackend
{
public:
    // Captures one record into out.raw and out.sampleRateHz. `abort` is polled while waiting
    // for the trigger, so a settings change or a stop does not wait for the capture.
    virtual AcquireResult Acquire(const Core::AcqSettings& s, Core::Frame& out,
                                  const std::atomic<bool>& abort) = 0;

    const std::string& LastError() const { return m_error; }

    ScopeBackend(const ScopeBackend&)            = delete;
    ScopeBackend& operator=(const ScopeBackend&) = delete;

protected:
    ScopeBackend()  = default;
    ~ScopeBackend() = default; // owned as members, never deleted through the base

    std::string m_error;
};

}
