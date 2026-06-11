#pragma once

// Port of src/api/requests.rs and src/api/response.rs — signed request
// primitives, throttling, 429 backoff and response parsing. The raw
// functions return the response body after status handling; the templated
// wrappers add JSON deserialization.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ae/http.hh"
#include "../core/errors.hh"
#include "../core/json.hh"

namespace kb::api {

using Params = std::vector<std::pair<std::string, std::string>>;

// Application credentials and user token for signed API requests.
struct RequestAuth {
    std::string app_id;
    std::string app_secret;
    std::string user_auth_token;
};

// Global API rate limit; 0 = unlimited (rely on 429 backoff).
void set_requests_per_minute(uint32_t rpm);

// Current Unix timestamp string for request signing.
std::string timestamp();

// Truncates with ellipsis (error-message contexts).
std::string truncate(const std::string &s, size_t max_len);

// Builds base_url + endpoint + ?query with the Rust port's urlencoding
// (space -> '+', unreserved kept, rest %XX).
std::string build_url_with_params(const std::string &base_url, const std::string &endpoint,
                                  const Params &params);

// Status-handling part of parse_response: success -> body; 404 ->
// ResourceNotFound; other errors -> ApiErrorResponse (decoding the API's
// {code, message} body when present).
Result<std::string> handle_response(const ae::HttpResponse &response,
                                    const std::string &endpoint);

// Signed GET (auth via X-User-Auth-Token header) with throttle + 429 retry.
Result<std::string> signed_get_raw(const ae::HttpClient &client, const std::string &base_url,
                                   const std::string &endpoint, Params params,
                                   const RequestAuth &auth);

// Unsigned POST form (login); user_auth_token omitted when empty.
Result<std::string> post_raw(const ae::HttpClient &client, const std::string &base_url,
                             const std::string &endpoint, Params params,
                             const std::string &app_id, const std::string &user_auth_token);

// Signed POST form (favorites mutation endpoints).
Result<std::string> signed_post_raw(const ae::HttpClient &client, const std::string &base_url,
                                    const std::string &endpoint, Params params,
                                    const RequestAuth &auth);

// Signed track file URL request (src/api/content/tracks.rs,
// get_track_file_url_raw): fixed-format signature, throttle + 429 retry.
Result<FileUrl> get_track_file_url_raw(const ae::HttpClient &client,
                                       const std::string &base_url, const RequestAuth &auth,
                                       int track_id, int format_id);

template <typename T>
Result<T> signed_get(const ae::HttpClient &client, const std::string &base_url,
                     const std::string &endpoint, Params params, const RequestAuth &auth) {
    auto raw = signed_get_raw(client, base_url, endpoint, std::move(params), auth);
    if (!raw.ok()) return raw.error();
    return parse_json<T>(raw.value());
}

template <typename T>
Result<T> post(const ae::HttpClient &client, const std::string &base_url,
               const std::string &endpoint, Params params, const std::string &app_id,
               const std::string &user_auth_token) {
    auto raw = post_raw(client, base_url, endpoint, std::move(params), app_id, user_auth_token);
    if (!raw.ok()) return raw.error();
    return parse_json<T>(raw.value());
}

template <typename T>
Result<T> signed_post(const ae::HttpClient &client, const std::string &base_url,
                      const std::string &endpoint, Params params, const RequestAuth &auth) {
    auto raw = signed_post_raw(client, base_url, endpoint, std::move(params), auth);
    if (!raw.ok()) return raw.error();
    return parse_json<T>(raw.value());
}

} // namespace kb::api
