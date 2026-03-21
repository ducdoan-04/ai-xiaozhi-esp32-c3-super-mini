#include "music_service.h"
#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "cJSON.h"

#include "audio_codec.h"
#include "board.h"

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "minimp3.h"

#define TAG "MusicService"

struct StreamCtx
{
    std::string url;
};

// Dùng để bắt Location header từ response trong event handler
struct RedirectCtx
{
    char location[512];
};

static esp_err_t http_redirect_event_handler(esp_http_client_event_t* evt)
{
    if (evt->event_id == HTTP_EVENT_ON_HEADER) {
        RedirectCtx* rctx = (RedirectCtx*)evt->user_data;
        if (rctx && strcasecmp(evt->header_key, "Location") == 0) {
            strncpy(rctx->location, evt->header_value, sizeof(rctx->location) - 1);
            rctx->location[sizeof(rctx->location) - 1] = '\0';
        }
    }
    return ESP_OK;
}



MusicService::MusicService()
{
}

MusicService::~MusicService()
{
    Stop();
}

bool MusicService::Initialize(const std::string& base_url)
{
    ws_url_ = base_url;

    ESP_LOGI(TAG, "Server: %s", ws_url_.c_str());

    return true;
}

std::string MusicService::HttpGet(const std::string& url, size_t max_bytes)
{
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 10000;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "HTTP init failed");
        return "";
    }

    if (esp_http_client_open(client, 0) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed");
        esp_http_client_cleanup(client);
        return "";
    }

    esp_http_client_fetch_headers(client);

    // Ki\u1ec3m tra Content-Length tr\u01b0\u1edbc \u0111\u1ec3 c\u00f3 th\u1ec3 reserve b\u1ed9 nh\u1edb
    int content_len = esp_http_client_get_content_length(client);
    std::string response;
    if (content_len > 0 && (size_t)content_len <= max_bytes) {
        response.reserve(content_len);
    }

    char buf[256];
    int len;
    size_t total = 0;

    while ((len = esp_http_client_read(client, buf, sizeof(buf))) > 0) {
        total += len;
        if (total > max_bytes) {
            ESP_LOGW(TAG, "Response qu\u00e1 l\u1edbn (>%u bytes), c\u1eaft b\u1edbt", (unsigned)max_bytes);
            break;
        }
        response.append(buf, len);
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "HttpGet: %u bytes", (unsigned)response.size());
    return response;
}

bool MusicService::PlaySongDirect(const std::string& url)
{
    if(url.empty())
        return false;

    if(is_playing_)
        Stop();

    is_playing_ = true;

    StreamCtx* ctx = new StreamCtx;
    ctx->url = url;

    xTaskCreate(

    [](void* arg)
    {

        StreamCtx* ctx = (StreamCtx*)arg;

        esp_http_client_handle_t client = nullptr;

        // Theo dõi redirect bằng event handler để bắt Location header từ response
        RedirectCtx rctx;
        std::string current_url = ctx->url;

        do
        {
            const int MAX_REDIRECTS = 5;
            int redirect_count = 0;
            int status = 0;

            // Vòng lặp xử lý redirect thủ công
            while (redirect_count <= MAX_REDIRECTS) {

                memset(&rctx, 0, sizeof(rctx));

                esp_http_client_config_t cfg = {};
                cfg.url        = current_url.c_str();
                cfg.method     = HTTP_METHOD_GET;
                cfg.timeout_ms = 30000;
                cfg.buffer_size    = 4096;
                cfg.buffer_size_tx = 1024;
                cfg.crt_bundle_attach     = esp_crt_bundle_attach;
                cfg.disable_auto_redirect = true;
                cfg.event_handler = http_redirect_event_handler;  // bắt response headers
                cfg.user_data     = &rctx;
                cfg.user_agent    = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

                client = esp_http_client_init(&cfg);
                if (!client) {
                    ESP_LOGE(TAG, "HTTP init fail");
                    break;
                }

                esp_http_client_set_header(client, "Accept",      "*/*");
                esp_http_client_set_header(client, "Connection",  "keep-alive");
                esp_http_client_set_header(client, "Icy-MetaData","1");
                esp_http_client_set_header(client, "Range",       "bytes=0-");

                if (esp_http_client_open(client, 0) != ESP_OK) {
                    ESP_LOGE(TAG, "HTTP open fail");
                    esp_http_client_cleanup(client);
                    client = nullptr;
                    break;
                }

                esp_http_client_fetch_headers(client);
                status = esp_http_client_get_status_code(client);
                ESP_LOGI(TAG, "HTTP status: %d (try %d, url: %s)", status, redirect_count, current_url.c_str());

                if (status >= 300 && status <= 399) {
                    // rctx.location được điền bởi event handler
                    if (rctx.location[0] != '\0') {
                        ESP_LOGI(TAG, "Redirect -> Location: %s", rctx.location);
                        current_url = rctx.location;
                        esp_http_client_close(client);
                        esp_http_client_cleanup(client);
                        client = nullptr;
                        redirect_count++;
                        continue;  // thử lại với URL mới
                    } else {
                        ESP_LOGE(TAG, "%d nhưng không có Location header", status);
                        break;
                    }
                }

                break;  // không phải redirect, thoát vòng
            }

            if (!client || status != 200) {
                ESP_LOGE(TAG, "Final HTTP failed: %d", status);
                break;
            }

            const int ring_size = 4096;   // giảm từ 8KB xuống 4KB
            const int rbuf_size = 1024;   // giảm từ 2KB xuống 1KB

            uint8_t* ring    = (uint8_t*)heap_caps_malloc(ring_size, MALLOC_CAP_8BIT);
            uint8_t* read_buf = (uint8_t*)heap_caps_malloc(rbuf_size, MALLOC_CAP_8BIT);

            if (!ring || !read_buf) {
                ESP_LOGE(TAG, "Buffer alloc fail (heap: %u)", (unsigned)esp_get_free_heap_size());
                heap_caps_free(ring);
                heap_caps_free(read_buf);
                break;
            }

            int ring_head = 0;

            mp3dec_t mp3;
            mp3dec_init(&mp3);

            mp3dec_frame_info_t info;

            AudioCodec* codec = Board::GetInstance().GetAudioCodec();

            if(!codec)
            {
                ESP_LOGE(TAG,"No codec");
                heap_caps_free(ring);
                heap_caps_free(read_buf);
                break;
            }

            codec->EnableOutput(true);

            int16_t* pcm = (int16_t*)heap_caps_malloc(
                MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(int16_t),
                MALLOC_CAP_8BIT);

            if(!pcm)
            {
                ESP_LOGE(TAG,"PCM alloc fail (heap: %u)", (unsigned)esp_get_free_heap_size());
                heap_caps_free(ring);
                heap_caps_free(read_buf);
                break;
            }

            ESP_LOGI(TAG, "Streaming start, free heap: %u", (unsigned)esp_get_free_heap_size());

            while(MusicService::GetInstance().IsPlaying())
            {

                int space = ring_size - ring_head;

                int r = esp_http_client_read(
                        client,
                        (char*)read_buf,
                        std::min(space, rbuf_size)
                );

                if(r <= 0)
                    break;

                memcpy(ring + ring_head, read_buf, r);
                ring_head += r;


                int samples = mp3dec_decode_frame(
                        &mp3,
                        ring,
                        ring_head,
                        pcm,
                        &info
                );

                if(info.frame_bytes > 0)
                {
                    memmove(
                        ring,
                        ring + info.frame_bytes,
                        ring_head - info.frame_bytes
                    );

                    ring_head -= info.frame_bytes;
                }

                if(samples <= 0)
                    continue;

                int ch = info.channels;

                // Dùng static buffer trên stack thay vì std::vector để tránh alloc
                // MINIMP3_MAX_SAMPLES_PER_FRAME = 1152 samples * 2 channels = 2304 max
                static int16_t mono_buf[MINIMP3_MAX_SAMPLES_PER_FRAME];
                int mono_count;

                if(ch == 2)
                {
                    mono_count = samples / 2;
                    for(int i = 0; i < mono_count; i++)
                    {
                        mono_buf[i] = (int16_t)(((int32_t)pcm[i*2] + pcm[i*2+1]) / 2);
                    }
                }
                else
                {
                    mono_count = samples;
                    memcpy(mono_buf, pcm, samples * sizeof(int16_t));
                }

                std::vector<int16_t> mono(mono_buf, mono_buf + mono_count);
                codec->OutputData(mono);

                taskYIELD();
            }

            heap_caps_free(pcm);
            heap_caps_free(ring);
            heap_caps_free(read_buf);

        }
        while(false);

        if(client)
        {
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
        }

        MusicService::GetInstance().Stop();

        delete ctx;

        vTaskDelete(NULL);

    },

    "mp3_stream",
    16384,
    ctx,
    2,
    NULL);

    return true;
}

void MusicService::Stop()
{
    is_playing_ = false;

    AudioCodec* codec = Board::GetInstance().GetAudioCodec();
    if(codec)
        codec->EnableOutput(false);
}

std::vector<SongInfo> MusicService::GetPopularSongs()
{
    ESP_LOGI(TAG, "GetPopularSongs not implemented");
    return {};
}

std::vector<SongInfo> MusicService::SearchSongs(const std::string& query, int limit)
{
    ESP_LOGI(TAG, "SearchSongs: %s (limit=%d)", query.c_str(), limit);

    // URL percent-encode query (xử lý cả tiếng Việt)
    std::string encoded;
    for (unsigned char c : query) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += (char)c;
        } else if (c == ' ') {
            encoded += '+';
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            encoded += buf;
        }
    }

    std::string url = ws_url_ + "/search?q=" + encoded + "&limit=" + std::to_string(limit);
    ESP_LOGI(TAG, "Search URL: %s", url.c_str());

    // Gi\u1edbi h\u1ea1n 12KB \u0111\u1ec3 tr\u00e1nh OOM tr\u00ean ESP32-C3
    std::string response = HttpGet(url, 12288);
    if (response.empty()) {
        ESP_LOGE(TAG, "Search request failed");
        return {};
    }

    ESP_LOGI(TAG, "Search response: %.200s", response.c_str());

    cJSON* json = cJSON_Parse(response.c_str());
    if (!json) {
        ESP_LOGE(TAG, "Search: invalid JSON");
        return {};
    }

    std::vector<SongInfo> results;

    // Server tr\u1ea3 v\u1ec1 {"message": "...", "songs": [...]}
    // H\u1ed7 tr\u1ee3 c\u1ea3 m\u1ea3ng tr\u1ef1c ti\u1ebfp l\u1eabn object v\u1edbi nhi\u1ec1u t\u00ean key kh\u00e1c nhau
    cJSON* arr = nullptr;
    if (cJSON_IsArray(json)) {
        arr = json;
    } else {
        // \u01afu ti\u00ean \"songs\" (format th\u1ef1c t\u1ebf c\u1ee7a server)
        arr = cJSON_GetObjectItem(json, "songs");
        if (!arr || !cJSON_IsArray(arr)) arr = cJSON_GetObjectItem(json, "data");
        if (!arr || !cJSON_IsArray(arr)) arr = cJSON_GetObjectItem(json, "items");
        if (!arr || !cJSON_IsArray(arr)) arr = cJSON_GetObjectItem(json, "results");
    }

    if (arr && cJSON_IsArray(arr)) {
        cJSON* item = nullptr;
        int count = 0;
        cJSON_ArrayForEach(item, arr) {
            if (count >= limit) break;
            SongInfo song;

            auto id     = cJSON_GetObjectItem(item, "encodeId");
            auto title  = cJSON_GetObjectItem(item, "title");
            auto artist = cJSON_GetObjectItem(item, "artistsNames");
            if (!artist) artist = cJSON_GetObjectItem(item, "artist");
            auto album  = cJSON_GetObjectItem(item, "album");
            auto dur    = cJSON_GetObjectItem(item, "duration");
            auto stream = cJSON_GetObjectItem(item, "streamUrl");

            if (id     && cJSON_IsString(id))    song.id     = id->valuestring;
            if (title  && cJSON_IsString(title))  song.title  = title->valuestring;
            if (artist && cJSON_IsString(artist)) song.artist = artist->valuestring;
            if (album  && cJSON_IsString(album))  song.album  = album->valuestring;
            if (stream && cJSON_IsString(stream)) song.url    = stream->valuestring;

            // duration c\u00f3 th\u1ec3 l\u00e0 string "M:SS" ho\u1eb7c s\u1ed1 gi\u00e2y
            if (dur) {
                if (cJSON_IsNumber(dur)) {
                    song.duration = dur->valueint;
                } else if (cJSON_IsString(dur) && dur->valuestring) {
                    // parse "M:SS" ho\u1eb7c "H:MM:SS"
                    int total = 0;
                    const char* s = dur->valuestring;
                    int part = 0;
                    while (*s) {
                        if (*s == ':') {
                            total = total * 60 + part;
                            part = 0;
                        } else if (*s >= '0' && *s <= '9') {
                            part = part * 10 + (*s - '0');
                        }
                        s++;
                    }
                    song.duration = total * 60 + part;
                }
            }

            if (!song.id.empty() && !song.title.empty()) {
                results.push_back(song);
                count++;
            }
        }
    }

    cJSON_Delete(json);
    ESP_LOGI(TAG, "Search found %d songs", (int)results.size());
    return results;
}

bool MusicService::PlaySongByTitle(const std::string& title)
{
    ESP_LOGI(TAG, "PlaySongByTitle: %s", title.c_str());
    auto results = SearchSongs(title, 1);
    if (results.empty()) {
        ESP_LOGE(TAG, "PlaySongByTitle: kh\u00f4ng t\u00ecm th\u1ea5y b\u00e0i: %s", title.c_str());
        return false;
    }
    const auto& song = results[0];
    ESP_LOGI(TAG, "PlaySongByTitle: ph\u00e1t b\u00e0i '%s' - id: %s", song.title.c_str(), song.id.c_str());
    return PlaySong(song.id);
}

bool MusicService::PlaySong(const std::string& id)
{
    ESP_LOGI(TAG, "PlaySong: %s", id.c_str());

    // B\u01b0\u1edbc 1: G\u1ecdi /song?id= \u0111\u1ec3 l\u1ea5y JSON ch\u1ee9a URL nh\u1ea1c
    std::string api_url = ws_url_ + "/song?id=" + id;
    ESP_LOGI(TAG, "Song API: %s", api_url.c_str());

    std::string response = HttpGet(api_url);
    if (response.empty()) {
        ESP_LOGE(TAG, "Song API failed");
        return false;
    }

    ESP_LOGI(TAG, "Song API response: %.300s", response.c_str());

    // B\u01b0\u1edbc 2: Parse JSON { "err":0, "data": { "128": "https://...", "320": "VIP" } }
    cJSON* json = cJSON_Parse(response.c_str());
    if (!json) {
        ESP_LOGE(TAG, "Invalid JSON from song API");
        return false;
    }

    cJSON* err_field = cJSON_GetObjectItem(json, "err");
    if (err_field && cJSON_IsNumber(err_field) && err_field->valueint != 0) {
        ESP_LOGE(TAG, "Song API error: %d", err_field->valueint);
        cJSON_Delete(json);
        return false;
    }

    cJSON* data = cJSON_GetObjectItem(json, "data");
    if (!data || !cJSON_IsObject(data)) {
        ESP_LOGE(TAG, "No 'data' field in song API response");
        cJSON_Delete(json);
        return false;
    }

    // \u01afu ti\u00ean 128kbps (mi\u1ec5n ph\u00ed), fallback l\u1ea5y value \u0111\u1ea7u ti\u00ean c\u00f3 URL h\u1ee3p l\u1ec7
    std::string stream_url;

    cJSON* q128 = cJSON_GetObjectItem(data, "128");
    if (q128 && cJSON_IsString(q128) &&
        q128->valuestring[0] != '\0' &&
        strcmp(q128->valuestring, "VIP") != 0) {
        stream_url = q128->valuestring;
        ESP_LOGI(TAG, "D\u00f9ng 128kbps");
    } else {
        // Th\u1eed l\u1ea5y b\u1ea5t k\u1ef3 key n\u00e0o c\u00f3 URL h\u1ee3p l\u1ec7
        for (cJSON* child = data->child; child; child = child->next) {
            if (cJSON_IsString(child) &&
                child->valuestring[0] != '\0' &&
                strcmp(child->valuestring, "VIP") != 0 &&
                strncmp(child->valuestring, "http", 4) == 0) {
                stream_url = child->valuestring;
                ESP_LOGI(TAG, "D\u00f9ng ch\u1ea5t l\u01b0\u1ee3ng '%s'", child->string);
                break;
            }
        }
    }

    cJSON_Delete(json);

    if (stream_url.empty()) {
        ESP_LOGE(TAG, "Kh\u00f4ng c\u00f3 URL h\u1ee3p l\u1ec7 (c\u00f3 th\u1ec3 c\u1ea7n VIP)");
        return false;
    }

    // B\u01b0\u1edbc 3: Stream MP3 t\u1eeb CDN URL (redirect \u0111\u01b0\u1ee3c x\u1eed l\u00fd b\u1edfi event handler)
    ESP_LOGI(TAG, "Stream URL: %s", stream_url.c_str());
    return PlaySongDirect(stream_url);
}