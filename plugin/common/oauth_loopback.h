#pragma once
#include <functional>
#include <string>

namespace spotify {

// Starts localhost:43147, opens the browser, waits until code or timeout.
// Returns authorization code or empty on failure.
std::string RunOAuthLoopback(const std::string& authorize_url, std::string* error,
                             int timeout_ms = 180000);

}  // namespace spotify
