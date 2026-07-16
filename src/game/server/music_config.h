#ifndef GAME_SERVER_MUSIC_CONFIG_H
#define GAME_SERVER_MUSIC_CONFIG_H

namespace MusicConfig
{
inline constexpr const char *SEARCH_ENDPOINT = "http://127.0.0.1:5000/search?name=%s";
inline constexpr const char *DOWNLOAD_ENDPOINT = "http://127.0.0.1:5000/download";
inline constexpr const char *UPLOAD_ENDPOINT = "http://127.0.0.1:5000/upload_map";

inline constexpr const char *AUDIO_FILE = "data/musicso/%s.opus";
inline constexpr const char *LYRICS_FILE = "data/musicso/%s.lrc";
inline constexpr const char *ORIGIN_MAP_FILE = "data/originmaps/%s.map";
inline constexpr const char *TARGET_MAP_FILE = "maps/%s.map";
inline constexpr const char *PLAYLIST_FILE = "data/musico/playlist.txt";
inline constexpr const char *LYRICS_STATE_FILE = "data/musico/lyrics_state.txt";
inline constexpr const char *STATE_DIR = "data/musico";
inline constexpr const char *WEBMAP_DIR = "data/webmaps";
inline constexpr const char *WEBMAP_FILE = "data/webmaps/%s_%s.map";

inline constexpr int SONG_SEARCH_GLOBAL_COOLDOWN_SECONDS = 10;
inline constexpr int SONG_SEARCH_PLAYER_COOLDOWN_SECONDS = 100;
inline constexpr int PRELOAD_SKIP_LOG_INTERVAL_SECONDS = 5;
inline constexpr int PRELOAD_FAILURE_RETRY_SECONDS = 30;
inline constexpr int MAX_PRELOAD_FAILURES = 3;
inline constexpr int PLAYLIST_VISIBLE_SONGS = 10;
inline constexpr int SEARCH_RESULT_LIMIT = 10;
inline constexpr int QUEUE_LOG_PREVIEW_SONGS = 3;
inline constexpr float NEXT_SONG_PRELOAD_PROGRESS = 0.2f;
inline constexpr float MAX_SONG_DURATION_SECONDS = 3600.0f;
}

#endif
