#pragma once

#include <cstdint>

namespace pose_anchor::driver {

// Diagnostic-only throttling: callers must still perform the associated safety
// action on every pose. No allocations, timer, or clock read in this helper.
class DiagnosticRateLimiter {
public:
    [[nodiscard]] bool shouldLog(std::int64_t nowNs) noexcept {
        if (hasLogged_ && nowNs >= lastLogNs_ &&
            static_cast<std::uint64_t>(nowNs) - static_cast<std::uint64_t>(lastLogNs_) <
                1'000'000'000u) {
            return false;
        }
        hasLogged_ = true;
        lastLogNs_ = nowNs;
        return true;
    }

private:
    std::int64_t lastLogNs_{};
    bool hasLogged_{};
};

}  // namespace pose_anchor::driver
