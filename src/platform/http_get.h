// One HTTPS GET with the system's own networking, for the new version notice
// (issue #90, frontend/update_check.h): WinHTTP on Windows, libcurl on Linux
// (loaded when the program asks, so a system without it still starts),
// NSURLSession on macOS and iOS, and Java's HttpURLConnection on Android
// (ChaotixActivity.httpGet). Each one checks the server's certificate the way
// the system does. It blocks for up to a few seconds, so it is called on a
// thread of its own.
#pragma once

#include <string>

namespace chaotix {

// The body of a 200 answer, at most 1 MB; false on any failure (no network,
// an error status, a timeout, no way to ask on this system).
bool http_get(const std::string& url, std::string* body);

}  // namespace chaotix
