#pragma once
#include "http.h"
#include "json.h"

#include <string>
#include <vector>

namespace spotify {

struct PlayerState {
  bool available = false;
  bool is_playing = false;
  int progress_ms = 0;
  int duration_ms = 0;
  int volume = 50;
  std::string title;
  std::string artist;
  std::string album;
  std::string album_art_url;
  std::string track_uri;
  std::string context_uri;
  std::string device_id;
  std::string device_name;
  std::string shuffle;
  std::string repeat;
  bool commands_restricted = false;
};

struct Device {
  std::string id;
  std::string name;
  std::string type;
  bool is_active = false;
  int volume = 50;
};

struct Track {
  std::string uri;
  std::string name;
  std::string artist;
  std::string album;
  std::string album_uri;
  int duration_ms = 0;
};

struct Playlist {
  std::string id;
  std::string uri;
  std::string name;
  int tracks = 0;
};

class Api {
 public:
  explicit Api(std::string client_id);

  bool EnsureToken(std::string* err);
  bool ExchangeCode(const std::string& code, const std::string& verifier, std::string* err);
  void Logout();
  bool LoggedIn();

  PlayerState GetPlayer(std::string* err = nullptr);
  std::vector<Device> GetDevices(std::string* err = nullptr);
  bool Transfer(const std::string& device_id, bool play, std::string* err = nullptr);
  bool Play(const std::string& device_id, std::string* err = nullptr);
  bool PlayUris(const std::vector<std::string>& uris, const std::string& device_id,
                std::string* err = nullptr);
  bool PlayContext(const std::string& context_uri, int offset, const std::string& device_id,
                   std::string* err = nullptr);
  bool PlayContextAt(const std::string& context_uri, const std::string& offset_uri,
                     const std::string& device_id, std::string* err = nullptr);
  bool Pause(const std::string& device_id, std::string* err = nullptr);
  bool Next(const std::string& device_id, std::string* err = nullptr);
  bool Previous(const std::string& device_id, std::string* err = nullptr);
  bool Seek(int position_ms, const std::string& device_id, std::string* err = nullptr);
  bool SetVolume(int percent, const std::string& device_id, std::string* err = nullptr);
  bool SetShuffle(bool on, const std::string& device_id, std::string* err = nullptr);

  std::vector<Playlist> GetPlaylists(std::string* err = nullptr);
  std::vector<Track> GetPlaylistTracks(const std::string& playlist_id, std::string* err = nullptr);
  std::vector<Track> GetLikedTracks(std::string* err = nullptr);
  std::vector<Track> Search(const std::string& query, std::string* err = nullptr);

  std::string client_id() const { return client_id_; }

 private:
  HttpResponse Authed(const std::string& method, const std::string& path_and_query,
                      const std::string& body = {});
  bool Refresh(std::string* err);

  std::string client_id_;
};

}  // namespace spotify
