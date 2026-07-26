#pragma once

// Port of src/credentials.rs and src/credentials/web.rs — .env file I/O and
// web player app-credential extraction.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ae/http.hh"
#include "errors.hh"

namespace kb {

// Reads QOBUZ_APP_ID / QOBUZ_APP_SECRET from a .env file. Missing file or
// missing keys yield an empty optional (not an error).
Result<std::optional<std::pair<std::string, std::string>>> load_app_credentials(
    const std::string &env_path);

// Writes/updates QOBUZ_APP_ID and QOBUZ_APP_SECRET in a .env file,
// preserving unrelated lines and replacing commented placeholders.
Result<void> save_app_credentials(const std::string &env_path, const std::string &app_id,
                                  const std::string &app_secret);

// Parses a .env file into key-value pairs (comments/blank lines skipped,
// surrounding quotes stripped).
Result<std::vector<std::pair<std::string, std::string>>> parse_env_file(
    const std::string &env_path);

// Extracts (app_id, app_secret) from the Qobuz web player JS bundle.
// `http` must be a plain client ("Mozilla/5.0" user agent, no API headers).
Result<std::pair<std::string, std::string>> extract_from_web_player(
    const ae::HttpClient &http);

namespace detail {
// Exposed for testing; mirror the Rust helpers.
Result<std::string> extract_bundle_url(const std::string &html);
Result<std::string> extract_app_id_from_bundle(const std::string &js);
Result<std::string> extract_app_secret_from_bundle(const std::string &js);
std::string capitalize_first_letter(const std::string &s);
} // namespace detail

} // namespace kb
