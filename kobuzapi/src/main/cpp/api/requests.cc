#include "requests.hh"

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <thread>

#include "../core/signing.hh"

namespace kb::api {

namespace {

constexpr uint32_t MAX_RETRIES = 3;
constexpr uint64_t BASE_BACKOFF_MS = 500;

std::atomic<uint32_t> g_requests_per_minute{0};
std::mutex g_last_call_mutex;
std::optional<std::chrono::steady_clock::time_point> g_last_call;

// Token-bucket style throttle: each caller reserves a slot spaced 60/rpm
// seconds apart, then sleeps until its slot arrives.
void throttle() {
    uint32_t rpm = g_requests_per_minute.load(std::memory_order_relaxed);
    if (rpm == 0) return;

    auto interval = std::chrono::microseconds(60'000'000 / rpm);
    std::chrono::steady_clock::duration sleep_dur{};
    {
        std::lock_guard<std::mutex> lock(g_last_call_mutex);
        auto now = std::chrono::steady_clock::now();
        auto next_slot = now;
        if (g_last_call) {
            auto next = *g_last_call + interval;
            if (next > now) next_slot = next;
        }
        g_last_call = next_slot;
        sleep_dur = next_slot - now;
    }
    if (sleep_dur > std::chrono::steady_clock::duration::zero()) {
        std::this_thread::sleep_for(sleep_dur);
    }
}

std::string urlencoding(const std::string &s) {
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char byte : s) {
        if (byte == ' ') {
            out += '+';
        } else if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
                   (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' ||
                   byte == '.' || byte == '~') {
            out += static_cast<char>(byte);
        } else {
            out += '%';
            out += hex[byte >> 4];
            out += hex[byte & 0x0F];
        }
    }
    return out;
}

void append_signature(Params &params, const std::string &method, const std::string &endpoint,
                      const RequestAuth &auth) {
    params.emplace_back("app_id", auth.app_id);
    params.emplace_back("request_ts", timestamp());
    std::string sig = sign_request(method, endpoint, params, auth.app_secret);
    params.emplace_back("request_sig", sig);
}

// GET with X-User-Auth-Token, throttled, retrying on 429 with exponential
// backoff. Transport errors are returned immediately.
Result<ae::HttpResponse> retry_with_backoff(const ae::HttpClient &client,
                                            const std::string &url,
                                            const std::string &user_auth_token) {
    throttle();

    std::optional<Error> last_error;
    std::vector<std::string> headers{"X-User-Auth-Token: " + user_auth_token};

    for (uint32_t attempt = 0; attempt <= MAX_RETRIES; ++attempt) {
        auto response = client.get(url, {}, headers);
        if (!response.ok()) return from_engine(response.error());

        if (response.value().status == 429) {
            uint64_t delay = BASE_BACKOFF_MS << attempt;
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            last_error = rate_limit_error("Rate limited, retry " + std::to_string(attempt) +
                                          "/" + std::to_string(MAX_RETRIES));
            continue;
        }
        return response.take();
    }

    return last_error ? *last_error : rate_limit_error("retries exhausted");
}

Result<std::string> execute_post(const ae::HttpClient &client, const std::string &base_url,
                                 const std::string &endpoint, const Params &params) {
    auto response = client.post_form(base_url + endpoint, params);
    if (!response.ok()) return from_engine(response.error());
    return handle_response(response.value(), endpoint);
}

} // namespace

void set_requests_per_minute(uint32_t rpm) {
    g_requests_per_minute.store(rpm, std::memory_order_relaxed);
}

std::string timestamp() {
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
    return std::to_string(secs);
}

std::string truncate(const std::string &s, size_t max_len) {
    if (s.size() <= max_len) return s;
    return s.substr(0, max_len) + "...";
}

std::string build_url_with_params(const std::string &base_url, const std::string &endpoint,
                                  const Params &params) {
    std::string url = base_url + endpoint;
    if (!params.empty()) {
        url += '?';
        bool first = true;
        for (const auto &[key, value] : params) {
            if (!first) url += '&';
            first = false;
            url += urlencoding(key);
            url += '=';
            url += urlencoding(value);
        }
    }
    return url;
}

Result<std::string> handle_response(const ae::HttpResponse &response,
                                    const std::string &endpoint) {
    long status = response.status;
    const std::string &body = response.body;

    if (status >= 200 && status < 300) return body;

    nlohmann::json err = nlohmann::json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (!err.is_discarded() && err.is_object()) {
        int code = err.value("code", 0);
        std::string message = err.value("message", "Unknown error");

        if (status == 404) {
            std::string resource_type = endpoint;
            while (!resource_type.empty() && resource_type.front() == '/') {
                resource_type.erase(resource_type.begin());
            }
            return not_found_error(resource_type, message);
        }
        return api_error_response(code, message, std::to_string(status));
    }

    return api_error_response(static_cast<int>(status), truncate(body, 200),
                              std::to_string(status));
}

Result<std::string> signed_get_raw(const ae::HttpClient &client, const std::string &base_url,
                                   const std::string &endpoint, Params params,
                                   const RequestAuth &auth) {
    append_signature(params, "GET", endpoint, auth);

    std::string url = build_url_with_params(base_url, endpoint, params);
    auto response = retry_with_backoff(client, url, auth.user_auth_token);
    if (!response.ok()) return response.error();

    return handle_response(response.value(), endpoint);
}

Result<std::string> post_raw(const ae::HttpClient &client, const std::string &base_url,
                             const std::string &endpoint, Params params,
                             const std::string &app_id, const std::string &user_auth_token) {
    params.emplace_back("app_id", app_id);
    if (!user_auth_token.empty()) {
        params.emplace_back("user_auth_token", user_auth_token);
    }
    return execute_post(client, base_url, endpoint, params);
}

Result<std::string> signed_post_raw(const ae::HttpClient &client, const std::string &base_url,
                                    const std::string &endpoint, Params params,
                                    const RequestAuth &auth) {
    params.emplace_back("user_auth_token", auth.user_auth_token);
    append_signature(params, "POST", endpoint, auth);
    return execute_post(client, base_url, endpoint, params);
}

Result<FileUrl> get_track_file_url_raw(const ae::HttpClient &client,
                                       const std::string &base_url, const RequestAuth &auth,
                                       int track_id, int format_id) {
    std::string ts = timestamp();
    std::string sig = sign_track_file_url(format_id, track_id, ts, auth.app_secret);

    Params params{
        {"track_id", std::to_string(track_id)},
        {"format_id", std::to_string(format_id)},
        {"intent", "stream"},
        {"request_ts", ts},
        {"request_sig", sig},
        {"app_id", auth.app_id},
    };

    std::string url = build_url_with_params(base_url, "/track/getFileUrl", params);
    auto response = retry_with_backoff(client, url, auth.user_auth_token);
    if (!response.ok()) return response.error();

    auto body = handle_response(response.value(), "/track/getFileUrl");
    if (!body.ok()) return body.error();
    return parse_json<FileUrl>(body.value());
}

} // namespace kb::api
