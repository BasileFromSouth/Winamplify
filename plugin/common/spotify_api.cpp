#include "spotify_api.h"
#include "pkce.h"
#include "store.h"
#include "utf8.h"

#include <windows.h>
#include <ctime>
#include <mutex>
#include <sstream>

namespace spotify {
namespace {

std::mutex g_mu;

std::int64_t Now() { return (std::int64_t)time(nullptr); }

std::string JoinArtists(const JsonValue* artists) {
  if (!artists || artists->type != JsonValue::kArray) return {};
  std::string o;
  for (auto& a : artists->arr) {
    auto name = a.Str("name");
    if (name.empty()) continue;
    if (!o.empty()) o += ", ";
    o += name;
  }
  return o;
}

// Prefer ~640px, else largest available Spotify album image URL.
std::string BestAlbumArtUrl(const JsonValue* album_or_show) {
  if (!album_or_show) return {};
  const JsonValue* images = album_or_show->Get("images");
  if (!images || images->type != JsonValue::kArray || images->arr.empty()) return {};
  const JsonValue* best = nullptr;
  int best_w = -1;
  const JsonValue* mid = nullptr;
  int mid_dist = 1000000;
  for (auto& im : images->arr) {
    auto url = im.Str("url");
    if (url.empty()) continue;
    int w = (int)im.Num("width");
    if (w > best_w) {
      best_w = w;
      best = &im;
    }
    if (w > 0) {
      int d = w > 640 ? w - 640 : 640 - w;
      if (d < mid_dist) {
        mid_dist = d;
        mid = &im;
      }
    } else if (!mid) {
      mid = &im;
    }
  }
  const JsonValue* pick = mid ? mid : best;
  return pick ? pick->Str("url") : std::string{};
}

Track TrackFromItem(const JsonValue& item) {
  const JsonValue* tr = item.Get("item");
  if (!tr) tr = item.Get("track");
  const JsonValue& t = tr ? *tr : item;
  Track out;
  out.uri = t.Str("uri");
  if (out.uri.empty()) {
    auto id = t.Str("id");
    auto type = t.Str("type");
    if (id.empty()) id = item.Str("id");
    if (type.empty()) type = item.Str("type");
    if (!id.empty()) out.uri = "spotify:" + (type.empty() ? "track" : type) + ":" + id;
  }
  out.name = t.Str("name");
  out.duration_ms = (int)t.Num("duration_ms");
  if (auto* al = t.Get("album")) {
    out.album = al->Str("name");
    out.album_uri = al->Str("uri");
    if (out.album_uri.empty()) {
      auto id = al->Str("id");
      if (!id.empty()) out.album_uri = "spotify:album:" + id;
    }
  }
  if (auto* ar = t.Get("artists")) out.artist = JoinArtists(ar);
  return out;
}

std::string WithDevice(const std::string& path, const std::string& device) {
  if (device.empty()) return path;
  return path + (path.find('?') == std::string::npos ? "?" : "&") + "device_id=" + UrlEncode(device);
}

void AppendTracksFromArray(const JsonValue* items, std::vector<Track>* out) {
  if (!items || items->type != JsonValue::kArray) return;
  for (auto& it : items->arr) {
    auto t = TrackFromItem(it);
    if (!t.uri.empty()) out->push_back(t);
  }
}

}  // namespace

Api::Api(std::string client_id) : client_id_(std::move(client_id)) {}

bool Api::LoggedIn() {
  auto t = LoadTokens();
  return !t.refresh.empty() || !t.access.empty();
}

void Api::Logout() { ClearTokens(); }

bool Api::Refresh(std::string* err) {
  auto tok = LoadTokens();
  if (tok.refresh.empty()) {
    if (err) *err = "no refresh token";
    return false;
  }
  std::string body = "grant_type=refresh_token&refresh_token=" + UrlEncode(tok.refresh) +
                     "&client_id=" + UrlEncode(client_id_);
  auto r = Http::Request("POST", "https://accounts.spotify.com/api/token", {}, body,
                         "application/x-www-form-urlencoded");
  if (r.status != 200) {
    if (err) *err = "refresh HTTP " + std::to_string(r.status) + " " + r.body;
    return false;
  }
  JsonValue j;
  ParseJson(r.body, &j);
  tok.access = j.Str("access_token");
  auto nr = j.Str("refresh_token");
  if (!nr.empty()) tok.refresh = nr;
  tok.expires_at = Now() + (std::int64_t)j.Num("expires_in", 3600) - 60;
  if (tok.access.empty()) {
    if (err) *err = "refresh missing access_token";
    return false;
  }
  SaveTokens(tok);
  return true;
}

bool Api::EnsureToken(std::string* err) {
  std::lock_guard<std::mutex> lock(g_mu);
  auto tok = LoadTokens();
  if (tok.access.empty() || Now() >= tok.expires_at) return Refresh(err);
  return true;
}

bool Api::ExchangeCode(const std::string& code, const std::string& verifier, std::string* err) {
  std::string body = "grant_type=authorization_code&code=" + UrlEncode(code) +
                     "&redirect_uri=" + UrlEncode(kRedirectUri) +
                     "&client_id=" + UrlEncode(client_id_) +
                     "&code_verifier=" + UrlEncode(verifier);
  auto r = Http::Request("POST", "https://accounts.spotify.com/api/token", {}, body,
                         "application/x-www-form-urlencoded");
  if (r.status != 200) {
    if (err) *err = "token HTTP " + std::to_string(r.status) + " " + r.body;
    return false;
  }
  JsonValue j;
  ParseJson(r.body, &j);
  Tokens tok;
  tok.access = j.Str("access_token");
  tok.refresh = j.Str("refresh_token");
  tok.expires_at = Now() + (std::int64_t)j.Num("expires_in", 3600) - 60;
  if (tok.access.empty()) {
    if (err) *err = "empty token response";
    return false;
  }
  SaveTokens(tok);
  return true;
}

HttpResponse Api::Authed(const std::string& method, const std::string& path_and_query,
                         const std::string& body) {
  std::string err;
  if (!EnsureToken(&err)) {
    HttpResponse r;
    r.error = err;
    return r;
  }
  auto tok = LoadTokens();
  auto r = Http::Request(method, "https://api.spotify.com" + path_and_query, tok.access, body);
  if (r.status == 401) {
    if (Refresh(&err)) {
      tok = LoadTokens();
      r = Http::Request(method, "https://api.spotify.com" + path_and_query, tok.access, body);
    }
  }
  return r;
}

PlayerState Api::GetPlayer(std::string* err) {
  PlayerState st;
  auto r = Authed("GET", "/v1/me/player");
  if (r.status == 204) return st;
  if (r.status != 200) {
    if (err) *err = r.error.empty() ? ("HTTP " + std::to_string(r.status)) : r.error;
    return st;
  }
  JsonValue j;
  ParseJson(r.body, &j);
  st.available = true;
  st.is_playing = j.Bool("is_playing");
  st.progress_ms = (int)j.Num("progress_ms");
  st.shuffle = j.Bool("shuffle_state") ? "on" : "off";
  st.repeat = j.Str("repeat_state");
  if (auto* d = j.Get("device")) {
    st.device_id = d->Str("id");
    st.device_name = d->Str("name");
    st.volume = (int)d->Num("volume_percent", 50);
  }
  if (auto* act = j.Get("actions")) {
    if (auto* dis = act->Get("disallows")) {
      st.commands_restricted =
          dis->Bool("pausing") && dis->Bool("resuming") && dis->Bool("skipping_next");
    }
  }
  if (auto* ctx = j.Get("context")) st.context_uri = ctx->Str("uri");
  const JsonValue* item = j.Get("item");
  if (!item) item = j.Get("currently_playing");
  if (item) {
    st.title = item->Str("name");
    st.duration_ms = (int)item->Num("duration_ms");
    st.track_uri = item->Str("uri");
    if (auto* al = item->Get("album")) {
      st.album = al->Str("name");
      st.album_art_url = BestAlbumArtUrl(al);
    }
    if (auto* show = item->Get("show")) {
      if (st.album.empty()) st.album = show->Str("name");
      if (st.album_art_url.empty()) st.album_art_url = BestAlbumArtUrl(show);
    }
    if (auto* ar = item->Get("artists")) st.artist = JoinArtists(ar);
  }
  if (st.title.empty()) {
    auto r2 = Authed("GET", "/v1/me/player/currently-playing");
    if (r2.status == 200) {
      JsonValue j2;
      ParseJson(r2.body, &j2);
      if (auto* it = j2.Get("item")) {
        st.available = true;
        st.is_playing = j2.Bool("is_playing", st.is_playing);
        st.progress_ms = (int)j2.Num("progress_ms", st.progress_ms);
        st.title = it->Str("name");
        st.duration_ms = (int)it->Num("duration_ms");
        st.track_uri = it->Str("uri");
        if (auto* al = it->Get("album")) {
          st.album = al->Str("name");
          st.album_art_url = BestAlbumArtUrl(al);
        }
        if (auto* ar = it->Get("artists")) st.artist = JoinArtists(ar);
      }
    }
  }
  return st;
}

std::vector<Device> Api::GetDevices(std::string* err) {
  std::vector<Device> out;
  auto r = Authed("GET", "/v1/me/player/devices");
  if (r.status != 200) {
    if (err) *err = "devices HTTP " + std::to_string(r.status);
    return out;
  }
  JsonValue j;
  ParseJson(r.body, &j);
  auto* arr = j.Get("devices");
  if (!arr || arr->type != JsonValue::kArray) return out;
  for (auto& d : arr->arr) {
    Device x;
    x.id = d.Str("id");
    x.name = d.Str("name");
    x.type = d.Str("type");
    x.is_active = d.Bool("is_active");
    x.volume = (int)d.Num("volume_percent", 50);
    out.push_back(x);
  }
  return out;
}

bool Api::Transfer(const std::string& device_id, bool play, std::string* err) {
  std::string body = "{\"device_ids\":[\"" + JsonEscape(device_id) + "\"],\"play\":" +
                     (play ? "true" : "false") + "}";
  auto r = Authed("PUT", "/v1/me/player", body);
  if (r.status != 204 && r.status != 200) {
    if (err) *err = "transfer HTTP " + std::to_string(r.status) + " " + r.body;
    return false;
  }
  return true;
}

bool Api::Play(const std::string& device_id, std::string* err) {
  auto r = Authed("PUT", WithDevice("/v1/me/player/play", device_id), "{}");
  if (r.status != 204 && r.status != 200) {
    if (err) *err = "play HTTP " + std::to_string(r.status) + " " + r.body;
    return false;
  }
  return true;
}

bool Api::PlayUris(const std::vector<std::string>& uris, const std::string& device_id,
                   std::string* err) {
  std::string body = "{\"uris\":[";
  for (size_t i = 0; i < uris.size(); i++) {
    if (i) body += ",";
    body += "\"" + JsonEscape(uris[i]) + "\"";
  }
  body += "]}";
  auto r = Authed("PUT", WithDevice("/v1/me/player/play", device_id), body);
  if (r.status != 204 && r.status != 200) {
    if (err) *err = "play uris HTTP " + std::to_string(r.status) + " " + r.body;
    return false;
  }
  return true;
}

bool Api::PlayContext(const std::string& context_uri, int offset, const std::string& device_id,
                      std::string* err) {
  std::ostringstream body;
  body << "{\"context_uri\":\"" << JsonEscape(context_uri) << "\"";
  if (offset >= 0) body << ",\"offset\":{\"position\":" << offset << "}";
  body << ",\"position_ms\":0}";
  auto r = Authed("PUT", WithDevice("/v1/me/player/play", device_id), body.str());
  if (r.status != 204 && r.status != 200) {
    if (err) *err = "play context HTTP " + std::to_string(r.status) + " " + r.body;
    return false;
  }
  return true;
}

bool Api::PlayContextAt(const std::string& context_uri, const std::string& offset_uri,
                        const std::string& device_id, std::string* err) {
  std::string body = "{\"context_uri\":\"" + JsonEscape(context_uri) +
                     "\",\"offset\":{\"uri\":\"" + JsonEscape(offset_uri) +
                     "\"},\"position_ms\":0}";
  auto r = Authed("PUT", WithDevice("/v1/me/player/play", device_id), body);
  if (r.status != 204 && r.status != 200) {
    if (err) *err = "play at HTTP " + std::to_string(r.status) + " " + r.body;
    return false;
  }
  return true;
}

bool Api::Pause(const std::string& device_id, std::string* err) {
  auto r = Authed("PUT", WithDevice("/v1/me/player/pause", device_id));
  if (r.status != 204 && r.status != 200) {
    if (err) *err = "pause HTTP " + std::to_string(r.status);
    return false;
  }
  return true;
}

bool Api::Next(const std::string& device_id, std::string* err) {
  auto r = Authed("POST", WithDevice("/v1/me/player/next", device_id));
  if (r.status != 204 && r.status != 200) {
    if (err) *err = "next HTTP " + std::to_string(r.status);
    return false;
  }
  return true;
}

bool Api::Previous(const std::string& device_id, std::string* err) {
  auto r = Authed("POST", WithDevice("/v1/me/player/previous", device_id));
  if (r.status != 204 && r.status != 200) {
    if (err) *err = "prev HTTP " + std::to_string(r.status);
    return false;
  }
  return true;
}

bool Api::Seek(int position_ms, const std::string& device_id, std::string* err) {
  std::string path = "/v1/me/player/seek?position_ms=" + std::to_string(position_ms);
  auto r = Authed("PUT", WithDevice(path, device_id));
  if (r.status != 204 && r.status != 200) {
    if (err) *err = "seek HTTP " + std::to_string(r.status);
    return false;
  }
  return true;
}

bool Api::SetVolume(int percent, const std::string& device_id, std::string* err) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  std::string path = "/v1/me/player/volume?volume_percent=" + std::to_string(percent);
  auto r = Authed("PUT", WithDevice(path, device_id));
  if (r.status != 204 && r.status != 200) {
    if (err) *err = "volume HTTP " + std::to_string(r.status);
    return false;
  }
  return true;
}

bool Api::SetShuffle(bool on, const std::string& device_id, std::string* err) {
  std::string path = std::string("/v1/me/player/shuffle?state=") + (on ? "true" : "false");
  auto r = Authed("PUT", WithDevice(path, device_id));
  if (r.status != 204 && r.status != 200) {
    if (err) *err = "shuffle HTTP " + std::to_string(r.status) + " " + r.body;
    return false;
  }
  return true;
}

std::vector<Playlist> Api::GetPlaylists(std::string* err) {
  std::vector<Playlist> out;
  int offset = 0;
  for (;;) {
    std::string path = "/v1/me/playlists?limit=50&offset=" + std::to_string(offset);
    auto r = Authed("GET", path);
    if (r.status != 200) {
      if (err) *err = "playlists HTTP " + std::to_string(r.status) + " " + r.body;
      break;
    }
    JsonValue j;
    ParseJson(r.body, &j);
    auto* items = j.Get("items");
    if (!items || items->type != JsonValue::kArray) break;
    for (auto& it : items->arr) {
      Playlist p;
      p.id = it.Str("id");
      p.uri = it.Str("uri");
      p.name = it.Str("name");
      if (auto* t = it.Get("tracks")) p.tracks = (int)t->Num("total");
      if (auto* t = it.Get("items")) {
        int n = (int)t->Num("total");
        if (n) p.tracks = n;
      }
      if (!p.id.empty()) out.push_back(p);
    }
    if ((int)items->arr.size() < 50) break;
    offset += 50;
    if (offset > 2000) break;
  }
  return out;
}

std::vector<Track> Api::GetPlaylistTracks(const std::string& playlist_id, std::string* err) {
  std::vector<Track> out;
  if (playlist_id.empty()) {
    if (err) *err = "empty playlist id";
    return out;
  }
  int offset = 0;
  for (;;) {
    std::string path = "/v1/playlists/" + playlist_id + "/items?limit=50&offset=" +
                       std::to_string(offset) + "&additional_types=track";
    auto r = Authed("GET", path);
    if (r.status == 403) {
      auto meta = Authed("GET", "/v1/playlists/" + playlist_id);
      if (meta.status == 200) {
        JsonValue j;
        ParseJson(meta.body, &j);
        const JsonValue* block = j.Get("items");
        if (!block) block = j.Get("tracks");
        if (block) {
          if (block->type == JsonValue::kArray)
            AppendTracksFromArray(block, &out);
          else
            AppendTracksFromArray(block->Get("items"), &out);
        }
      }
      if (out.empty() && err)
        *err =
            "403: track list is only available for playlists you own (not Spotify mixes). Double-click = play.";
      break;
    }
    if (r.status != 200) {
      if (err) *err = "items HTTP " + std::to_string(r.status);
      break;
    }
    JsonValue j;
    ParseJson(r.body, &j);
    auto* items = j.Get("items");
    if (!items || items->type != JsonValue::kArray) break;
    AppendTracksFromArray(items, &out);
    if ((int)items->arr.size() < 50) break;
    offset += 50;
    if (offset > 5000) break;
  }
  return out;
}

std::vector<Track> Api::GetLikedTracks(std::string* err) {
  std::vector<Track> out;
  int offset = 0;
  for (;;) {
    std::string path = "/v1/me/tracks?limit=50&offset=" + std::to_string(offset);
    auto r = Authed("GET", path);
    if (r.status != 200) {
      if (err) *err = "liked HTTP " + std::to_string(r.status) + " " + r.body;
      break;
    }
    JsonValue j;
    ParseJson(r.body, &j);
    auto* items = j.Get("items");
    if (!items || items->type != JsonValue::kArray) break;
    for (auto& it : items->arr) {
      auto t = TrackFromItem(it);
      if (!t.uri.empty()) out.push_back(t);
    }
    if ((int)items->arr.size() < 50) break;
    offset += 50;
    if (offset > 5000) break;
  }
  return out;
}

std::vector<Track> Api::Search(const std::string& query, std::string* err) {
  std::vector<Track> out;
  if (query.empty()) return out;
  int offset = 0;
  for (int page = 0; page < 3; page++) {
    std::string path = "/v1/search?q=" + UrlEncode(query) +
                       "&type=track,album,playlist&limit=10&offset=" + std::to_string(offset);
    auto r = Authed("GET", path);
    if (r.status != 200) {
      if (err) *err = "search HTTP " + std::to_string(r.status) + " " + r.body;
      break;
    }
    JsonValue j;
    ParseJson(r.body, &j);
    if (auto* tracks = j.Path("tracks.items")) {
      if (tracks->type == JsonValue::kArray) {
        for (auto& it : tracks->arr) {
          auto t = TrackFromItem(it);
          if (!t.uri.empty()) out.push_back(t);
        }
      }
    }
    if (auto* albums = j.Path("albums.items")) {
      if (albums->type == JsonValue::kArray) {
        for (auto& it : albums->arr) {
          Track t;
          t.uri = it.Str("uri");
          if (t.uri.empty()) {
            auto id = it.Str("id");
            if (!id.empty()) t.uri = "spotify:album:" + id;
          }
          t.name = it.Str("name");
          t.album = t.name;
          t.duration_ms = 0;
          if (auto* ar = it.Get("artists")) t.artist = JoinArtists(ar);
          if (!t.uri.empty()) out.push_back(t);
        }
      }
    }
    if (auto* lists = j.Path("playlists.items")) {
      if (lists->type == JsonValue::kArray) {
        for (auto& it : lists->arr) {
          if (it.type != JsonValue::kObject) continue;
          Track t;
          t.uri = it.Str("uri");
          if (t.uri.empty()) {
            auto id = it.Str("id");
            if (!id.empty()) t.uri = "spotify:playlist:" + id;
          }
          t.name = it.Str("name");
          if (auto* owner = it.Get("owner")) t.artist = owner->Str("display_name");
          if (t.artist.empty()) t.artist = "Playlist";
          if (auto* tr = it.Get("tracks")) t.album = std::to_string((int)tr->Num("total")) + " tracks";
          if (!t.uri.empty()) out.push_back(t);
        }
      }
    }
    offset += 10;
  }
  return out;
}

}  // namespace spotify
