#pragma once
#include <string>

namespace spotify {

constexpr const char* kRedirectUri = "http://127.0.0.1:43147/callback";
constexpr const char* kScopes =
    "user-read-playback-state user-modify-playback-state user-read-currently-playing "
    "playlist-read-private playlist-read-collaborative user-library-read";

struct PkcePair {
  std::string verifier;
  std::string challenge;
};

PkcePair MakePkce();
std::string RandomUrlToken(int nbytes);

}  // namespace spotify
