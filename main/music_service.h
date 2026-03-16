#ifndef _MUSIC_SERVICE_H_
#define _MUSIC_SERVICE_H_

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cJSON.h>

struct SongInfo {
    std::string id;       // encodeId (used in /stream?id=...)
    std::string title;
    std::string artist;
    std::string album;
    std::string url;      // direct streamUrl if present in search results
    int duration = 0;     // seconds
};

class MusicService {
public:
    static MusicService& GetInstance() {
        static MusicService instance;
        return instance;
    }

    MusicService(const MusicService&) = delete;
    MusicService& operator=(const MusicService&) = delete;

    /**
     * Initialize with the proxy server base URL, e.g. "http://192.168.x.x:3000"
     */
    bool Initialize(const std::string& base_url);

    /** Connect (no-op for HTTP – kept for API compat) */
    bool Connect();
    void Disconnect();

    /** Fetch popular / chart songs */
    std::vector<SongInfo> GetPopularSongs();

    /** Search by keyword, return up to `limit` results */
    std::vector<SongInfo> SearchSongs(const std::string& query, int limit = 10);

    /** Build the /stream URL for a given song_id (for reference) */
    std::string GetSongUrl(const std::string& song_id);

    /**
     * Resolve encodeId → real MP3 URL via /stream?id=…, then stream & play.
     */
    bool PlaySong(const std::string& song_id);

    /**
     * Search by title, pick first result, then play it.
     */
    bool PlaySongByTitle(const std::string& title);

    /**
     * Stream and play an MP3 directly from the given HTTPS/HTTP URL.
     * Spawns a background task; returns immediately.
     *
     * IMPORTANT: this now uses minimp3 to decode each frame and writes
     * raw PCM to I2S via AudioCodec::Write() — NOT Opus queues.
     */
    bool PlaySongDirect(const std::string& stream_url);

    /** Stop current playback */
    void Stop();

    /** True while a stream task is active */
    bool IsPlaying() const { return is_playing_; }

    /** Called with "playing" | "stopped" | "not_found" */
    void SetStatusCallback(std::function<void(const std::string& status)> callback) {
        status_callback_ = callback;
    }

private:
    MusicService();
    ~MusicService();

    std::string ws_url_;
    bool is_connected_ = false;
    bool is_playing_   = false;
    std::string current_song_id_;
    std::function<void(const std::string& status)> status_callback_;

    // Blocking HTTP GET — returns full body (JSON only, not for large MP3s)
    std::string HttpGet(const std::string& url);

    // Legacy / internal
    cJSON* MakeRequest(const std::string& method, cJSON* params);
    static std::vector<SongInfo> ParseSongList(const cJSON* json);
};

#endif // _MUSIC_SERVICE_H_
