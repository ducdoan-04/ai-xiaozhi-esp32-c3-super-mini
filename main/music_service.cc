/**
 * MusicService - Correct MP3 Streaming Pipeline
 *
 * Pipeline:
 *   ESP32 → GET /search?q=...  → JSON list → pick encodeId
 *   ESP32 → GET /stream?id=... → JSON { streamUrl: "https://..." }
 *   ESP32 → GET streamUrl      → MP3 bytes (chunked HTTP)
 *   minimp3 decode each frame  → int16 PCM
 *   AudioCodec::Write()        → I2S → MAX98357A → Speaker
 *
 * Key design points:
 *  - HTTP streaming: esp_http_client_open() + esp_http_client_read() 
 *    so we never hold the whole MP3 in RAM.
 *  - A ring buffer accumulates raw bytes; minimp3 finds sync headers
 *    and decodes one frame at a time (up to 1152 samples * 2 channels).
 *  - After resampling to output_sample_rate_, we call codec->Write()
 *    directly - bypassing the Opus encode/decode path entirely since
 *    music is already PCM, not voice.
 *  - The stream task runs at a low priority so voice/AI is unaffected.
 */

#include "music_service.h"
#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <memory>
#include <sstream>
#include <iomanip>
#include "audio_codec.h"
#include "board.h"

// minimp3: single-file MP3 decoder — compile only here
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3        // skip AAC/MP1/MP2 to save flash
#include "minimp3.h"

#define TAG "MusicService"

// ──────────────────────────────────────────────
// URL Encoder
// ──────────────────────────────────────────────
static std::string UrlEncode(const std::string& str) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (char c : str) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << std::uppercase << '%' << std::setw(2) << int((unsigned char)c);
        }
    }
    return escaped.str();
}

// ──────────────────────────────────────────────
// Simple linear resampler (mono int16)
// ──────────────────────────────────────────────
static void ResampleMono(const int16_t* src, int src_len, int src_rate,
                          std::vector<int16_t>& dst, int dst_rate) {
    if (src_rate == dst_rate) {
        dst.assign(src, src + src_len);
        return;
    }
    int dst_len = (int)((int64_t)src_len * dst_rate / src_rate);
    dst.resize(dst_len);
    for (int i = 0; i < dst_len; i++) {
        int src_i = (int)((int64_t)i * src_rate / dst_rate);
        if (src_i >= src_len - 1) src_i = src_len - 1;
        dst[i] = src[src_i];
    }
}

// ──────────────────────────────────────────────
// Streaming task context (heap-allocated, deleted by task)
// ──────────────────────────────────────────────
struct StreamCtx {
    std::string url;
    int         output_sample_rate;  // from AudioCodec
};

// ──────────────────────────────────────────────
// MusicService
// ──────────────────────────────────────────────

MusicService::MusicService() {}

MusicService::~MusicService() {
    Stop();
}

bool MusicService::Initialize(const std::string& base_url) {
    ws_url_ = base_url;
    ESP_LOGI(TAG, "Music service initialized, server: %s", ws_url_.c_str());
    return true;
}

bool MusicService::Connect() {
    is_connected_ = true;
    return true;
}

void MusicService::Disconnect() {
    is_connected_ = false;
    is_playing_   = false;
}

// ──────────────────────────────────────────────
// HttpGet: blocking, returns full body as string
// Only used for small JSON responses (/search, /stream)
// ──────────────────────────────────────────────
std::string MusicService::HttpGet(const std::string& url) {
    std::string response;

    esp_http_client_config_t config = {};
    config.url    = url.c_str();
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 10000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return "";
    }

    // open the connection
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return "";
    }

    // read headers / content-length
    int content_len = esp_http_client_fetch_headers(client);
    int status      = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGW(TAG, "HTTP %d for %s", status, url.c_str());
    }

    // read body
    if (content_len > 0) {
        response.reserve(content_len);
    }
    char buf[512];
    int  len;
    while ((len = esp_http_client_read(client, buf, sizeof(buf))) > 0) {
        response.append(buf, len);
        if ((int)response.size() > 64 * 1024) {  // safety cap 64 KB
            ESP_LOGW(TAG, "Response too large, truncating");
            break;
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return response;
}

// ──────────────────────────────────────────────
// Song list / search helpers
// ──────────────────────────────────────────────
std::vector<SongInfo> MusicService::GetPopularSongs() {
    std::string response = HttpGet(ws_url_ + "/charts");
    if (response.empty()) return {};
    cJSON* json = cJSON_Parse(response.c_str());
    auto songs = ParseSongList(json);
    cJSON_Delete(json);
    return songs;
}

std::vector<SongInfo> MusicService::SearchSongs(const std::string& query, int limit) {
    std::string url = ws_url_ + "/search?q=" + UrlEncode(query);
    ESP_LOGI(TAG, "Search: %s", url.c_str());
    std::string response = HttpGet(url);
    if (response.empty()) return {};

    cJSON* json = cJSON_Parse(response.c_str());
    auto songs = ParseSongList(json);
    cJSON_Delete(json);
    if ((int)songs.size() > limit) songs.resize(limit);
    return songs;
}

std::string MusicService::GetSongUrl(const std::string& song_id) {
    return ws_url_ + "/stream?id=" + song_id;
}

// ──────────────────────────────────────────────
// PlaySong: resolve encodeId → real streamUrl → play
// ──────────────────────────────────────────────
bool MusicService::PlaySong(const std::string& song_id) {
    // GET /stream?id=<encodeId>  → { "streamUrl": "https://..." }
    std::string info_url = ws_url_ + "/stream?id=" + song_id;
    ESP_LOGI(TAG, "Getting stream URL: %s", info_url.c_str());

    std::string response = HttpGet(info_url);
    if (response.empty()) {
        ESP_LOGE(TAG, "Empty response from /stream");
        return false;
    }
    ESP_LOGD(TAG, "Stream info response: %.200s", response.c_str());

    cJSON* json = cJSON_Parse(response.c_str());
    if (!json) {
        ESP_LOGE(TAG, "JSON parse failed");
        return false;
    }

    // Try streamUrl, then proxyUrl
    const char* stream_url = nullptr;
    const cJSON* item = cJSON_GetObjectItem(json, "streamUrl");
    if (item && cJSON_IsString(item)) stream_url = item->valuestring;
    if (!stream_url) {
        item = cJSON_GetObjectItem(json, "proxyUrl");
        if (item && cJSON_IsString(item)) stream_url = item->valuestring;
    }
    if (!stream_url || stream_url[0] == '\0') {
        ESP_LOGE(TAG, "No stream URL in response");
        cJSON_Delete(json);
        is_playing_ = false;
        return false;
    }

    std::string actual_url = stream_url;
    cJSON_Delete(json);

    ESP_LOGI(TAG, "Stream URL: %.80s...", actual_url.c_str());
    return PlaySongDirect(actual_url);
}

// ──────────────────────────────────────────────
// PlaySongByTitle: search → pick first → PlaySong
// ──────────────────────────────────────────────
bool MusicService::PlaySongByTitle(const std::string& title) {
    ESP_LOGI(TAG, "Search & play: %s", title.c_str());
    auto results = SearchSongs(title, 1);
    if (results.empty()) {
        ESP_LOGW(TAG, "No result for: %s", title.c_str());
        if (status_callback_) status_callback_("not_found");
        return false;
    }

    // If search returned an encodeId, use PlaySong; otherwise PlaySongDirect
    if (!results[0].id.empty()) {
        return PlaySong(results[0].id);
    }
    if (!results[0].url.empty()) {
        return PlaySongDirect(results[0].url);
    }
    return false;
}

// ──────────────────────────────────────────────
// PlaySongDirect — THE REAL STREAMING ENGINE
//
//  Spawns a background task that:
//   1. Opens HTTP connection to the MP3 URL (streaming, not buffered)
//   2. Reads raw bytes into a ring buffer
//   3. Uses minimp3 to find MP3 sync-word and decode one frame at a time
//   4. Resamples decoded PCM to codec output_sample_rate (24 kHz)
//   5. Writes PCM directly to I2S via AudioCodec::Write()
// ──────────────────────────────────────────────
bool MusicService::PlaySongDirect(const std::string& stream_url) {
    if (stream_url.empty()) {
        ESP_LOGE(TAG, "Stream URL empty");
        return false;
    }

    // Stop previous playback if any
    if (is_playing_) {
        Stop();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    is_playing_      = true;
    current_song_id_ = "direct";

    // Heap-allocate context — task deletes it on exit
    auto* ctx = new StreamCtx();
    ctx->url  = stream_url;

    // Get output sample rate from the codec
    AudioCodec* codec = Board::GetInstance().GetAudioCodec();
    ctx->output_sample_rate = codec ? codec->output_sample_rate() : 24000;

    ESP_LOGI(TAG, "Starting MP3 stream task, output_rate=%d", ctx->output_sample_rate);

    xTaskCreate(
        [](void* arg) {
            StreamCtx* ctx = (StreamCtx*)arg;

            // ── 1. Open HTTP(S) connection ─────────────────────
            esp_http_client_config_t cfg = {};
            cfg.url                    = ctx->url.c_str();
            cfg.method                 = HTTP_METHOD_GET;
            cfg.timeout_ms             = 20000;
            cfg.buffer_size            = 4096;      // socket recv buffer
            cfg.crt_bundle_attach      = esp_crt_bundle_attach;  // HTTPS support
            cfg.keep_alive_enable      = true;
            cfg.skip_cert_common_name_check = false;

            esp_http_client_handle_t client = esp_http_client_init(&cfg);
            if (!client) {
                ESP_LOGE(TAG, "HTTP init failed");
                goto cleanup;
            }

            {
                esp_err_t err = esp_http_client_open(client, 0);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "HTTP open: %s", esp_err_to_name(err));
                    esp_http_client_cleanup(client);
                    goto cleanup;
                }

                int64_t content_len = esp_http_client_fetch_headers(client);
                int status = esp_http_client_get_status_code(client);
                ESP_LOGI(TAG, "HTTP %d, content_length=%lld", status, content_len);

                // Follow HTTP 302/301 redirects —
                // esp_http_client doesn't auto-follow by default when using open()
                // If status != 200 here, bail out (proxy server should handle redirects)
                if (status != 200) {
                    ESP_LOGE(TAG, "HTTP status %d — cannot stream MP3", status);
                    esp_http_client_close(client);
                    esp_http_client_cleanup(client);
                    goto cleanup;
                }

                // ── 2. Initialise minimp3 ──────────────────────
                mp3dec_t mp3d;
                mp3dec_init(&mp3d);

                mp3dec_frame_info_t frame_info;

                // Ring buffer: accumulate raw HTTP bytes, minimp3 searches for sync
                // Buffer size: must hold at least 2 full MP3 frames (~2×2881 bytes)
                const int kRingSize = 8 * 1024;
                std::vector<uint8_t> ring(kRingSize, 0);
                int ring_head = 0;  // bytes of valid data in ring[0..ring_head-1]

                // Temp read buffer
                const int kReadBuf = 2048;
                std::vector<uint8_t> read_buf(kReadBuf);

                AudioCodec* codec = Board::GetInstance().GetAudioCodec();
                if (!codec) {
                    ESP_LOGE(TAG, "No audio codec");
                    esp_http_client_close(client);
                    esp_http_client_cleanup(client);
                    goto cleanup;
                }

                // Ensure output is enabled
                if (!codec->output_enabled()) {
                    codec->EnableOutput(true);
                }

                int frames_decoded = 0;
                bool http_done = false;

                // ── 3. Main decode loop ────────────────────────
                while (!http_done || ring_head > 0) {
                    // Stop if someone called MusicService::Stop()
                    if (!MusicService::GetInstance().IsPlaying()) {
                        ESP_LOGI(TAG, "Playback stopped by user");
                        break;
                    }

                    // Fill ring buffer from HTTP
                    if (!http_done && ring_head < kRingSize) {
                        int space   = kRingSize - ring_head;
                        int to_read = (space < kReadBuf) ? space : kReadBuf;
                        int got = esp_http_client_read(client, (char*)read_buf.data(), to_read);
                        if (got > 0) {
                            memcpy(ring.data() + ring_head, read_buf.data(), got);
                            ring_head += got;
                        } else if (got == 0) {
                            // EOF
                            http_done = true;
                            ESP_LOGI(TAG, "HTTP stream EOF after %d frames", frames_decoded);
                        } else {
                            // Error
                            ESP_LOGW(TAG, "HTTP read error, stopping");
                            http_done = true;
                        }
                    }

                    // Try to decode one MP3 frame from ring buffer
                    // PCM output: up to MINIMP3_MAX_SAMPLES_PER_FRAME = 1152*2 samples
                    int16_t pcm_buf[MINIMP3_MAX_SAMPLES_PER_FRAME];
                    int samples = mp3dec_decode_frame(&mp3d,
                                                     ring.data(), ring_head,
                                                     pcm_buf, &frame_info);

                    if (frame_info.frame_bytes == 0 && samples == 0) {
                        // Not enough data yet (need more from HTTP) or invalid header
                        if (http_done) {
                            // Nothing left to decode
                            break;
                        }
                        // Give network a chance
                        vTaskDelay(pdMS_TO_TICKS(5));
                        continue;
                    }

                    // Consume frame_bytes from ring buffer (even if samples==0, skip garbage)
                    if (frame_info.frame_bytes > 0 && frame_info.frame_bytes <= ring_head) {
                        memmove(ring.data(), ring.data() + frame_info.frame_bytes,
                                ring_head - frame_info.frame_bytes);
                        ring_head -= frame_info.frame_bytes;
                    }

                    if (samples <= 0) {
                        // Decoder skipped a bad/non-audio frame
                        continue;
                    }

                    frames_decoded++;

                    // frame_info.channels = 1 or 2
                    // frame_info.hz       = sample rate of the MP3 (e.g. 44100)
                    // samples             = total PCM samples (channels * samples_per_channel)

                    int channels   = frame_info.channels;
                    int mp3_sr     = frame_info.hz;
                    int pcm_count  = samples;  // interleaved if stereo

                    // ── 4. Mix stereo→mono if needed ──────────
                    std::vector<int16_t> mono;
                    if (channels == 2) {
                        int half = pcm_count / 2;
                        mono.resize(half);
                        for (int i = 0; i < half; i++) {
                            int32_t avg = ((int32_t)pcm_buf[i * 2] + pcm_buf[i * 2 + 1]) / 2;
                            mono[i] = (int16_t)avg;
                        }
                    } else {
                        mono.assign(pcm_buf, pcm_buf + pcm_count);
                    }

                    // ── 5. Resample to codec output rate ──────
                    std::vector<int16_t> resampled;
                    ResampleMono(mono.data(), (int)mono.size(),
                                 mp3_sr, resampled, ctx->output_sample_rate);

                    // ── 6. Write to I2S ───────────────────────
                    codec->OutputData(resampled);

                    // Yield occasionally to maintain FreeRTOS fairness
                    if (frames_decoded % 16 == 0) {
                        taskYIELD();
                    }
                }

                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                ESP_LOGI(TAG, "Stream finished: %d frames decoded", frames_decoded);
            }

        cleanup:
            MusicService::GetInstance().Stop();
            delete ctx;
            vTaskDelete(NULL);
        },
        "mp3_stream",
        8192,               // stack: minimp3 needs ~6KB, leave margin
        ctx,
        tskIDLE_PRIORITY + 2,
        NULL
    );

    if (status_callback_) status_callback_("playing");
    return true;
}

void MusicService::Stop() {
    is_playing_      = false;
    current_song_id_.clear();
    ESP_LOGI(TAG, "Playback stopped");
    if (status_callback_) status_callback_("stopped");
}

// ──────────────────────────────────────────────
// ParseSongList: parse JSON array from /search or /charts
// Expected item shape: { encodeId, id, title, artist, duration, streamUrl? }
// ──────────────────────────────────────────────
std::vector<SongInfo> MusicService::ParseSongList(const cJSON* json) {
    std::vector<SongInfo> songs;
    if (!json) return songs;

    const cJSON* arr = nullptr;
    if (cJSON_IsArray(json)) {
        arr = json;
    } else {
        arr = cJSON_GetObjectItem(json, "data");
        if (!arr || !cJSON_IsArray(arr)) {
            arr = cJSON_GetObjectItem(json, "results");
        }
        if (!arr || !cJSON_IsArray(arr)) {
            arr = cJSON_GetObjectItem(json, "items");
        }
    }
    if (!arr || !cJSON_IsArray(arr)) {
        ESP_LOGW(TAG, "ParseSongList: no array found in JSON");
        return songs;
    }

    const cJSON* item = nullptr;
    cJSON_ArrayForEach(item, arr) {
        if (!cJSON_IsObject(item)) continue;

        // encodeId takes priority over id
        const cJSON* id_obj = cJSON_GetObjectItem(item, "encodeId");
        if (!id_obj || !cJSON_IsString(id_obj)) {
            id_obj = cJSON_GetObjectItem(item, "id");
        }

        const cJSON* title_obj    = cJSON_GetObjectItem(item, "title");
        const cJSON* artist_obj   = cJSON_GetObjectItem(item, "artist");
        const cJSON* duration_obj = cJSON_GetObjectItem(item, "duration");
        const cJSON* url_obj      = cJSON_GetObjectItem(item, "streamUrl");

        SongInfo song;
        song.id     = (id_obj     && cJSON_IsString(id_obj))     ? id_obj->valuestring     : "";
        song.title  = (title_obj  && cJSON_IsString(title_obj))  ? title_obj->valuestring  : "";
        song.artist = (artist_obj && cJSON_IsString(artist_obj)) ? artist_obj->valuestring : "";
        song.url    = (url_obj    && cJSON_IsString(url_obj))    ? url_obj->valuestring    : "";

        song.duration = 0;
        if (duration_obj) {
            if (cJSON_IsNumber(duration_obj)) {
                song.duration = duration_obj->valueint;
            } else if (cJSON_IsString(duration_obj)) {
                int m = 0, s = 0;
                sscanf(duration_obj->valuestring, "%d:%d", &m, &s);
                song.duration = m * 60 + s;
            }
        }

        if (!song.id.empty() && !song.title.empty()) {
            songs.push_back(song);
        }
    }
    return songs;
}

// Stub for legacy call sites
cJSON* MusicService::MakeRequest(const std::string&, cJSON*) {
    return nullptr;
}
