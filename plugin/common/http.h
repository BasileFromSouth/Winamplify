#pragma once
#include <string>

namespace spotify {

struct HttpResponse {
  int status = 0;
  std::string body;
  std::string error;
};

class Http {
 public:
  static HttpResponse Request(const std::string& method, const std::string& url,
                              const std::string& bearer = {},
                              const std::string& body = {},
                              const std::string& content_type = "application/json",
                              const std::string& accept = "application/json");
  // GET binary (JPEG/PNG cover art, etc.)
  static HttpResponse GetBinary(const std::string& url);
};

}  // namespace spotify
