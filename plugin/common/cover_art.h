#pragma once
#include <string>

namespace spotify {

std::wstring CoverDir();

// Download cover into a per-track folder (folder.jpg + vis.lsv).
// Returns absolute path to that folder's vis.lsv so Winamp Album Art reloads
// (same vis.lsv path is cached forever by Winamp). Empty url/key clears.
std::wstring PrepareTrackCover(const std::string& track_key, const std::string& image_url);

}  // namespace spotify
