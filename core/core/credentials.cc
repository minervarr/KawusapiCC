#include "credentials.hh"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

#include "ae/base64.hh"

namespace kb {

namespace {

std::string trim(const std::string &s) {
    size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

std::string unquote(const std::string &s) {
    if (s.size() < 2) return s;
    char first = s.front(), last = s.back();
    if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

std::optional<std::pair<std::string, std::string>> parse_env_line(const std::string &line) {
    std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed.front() == '#') return std::nullopt;
    size_t eq = trimmed.find('=');
    if (eq == std::string::npos) return std::nullopt;
    std::string key = trim(trimmed.substr(0, eq));
    std::string value = unquote(trim(trimmed.substr(eq + 1)));
    return std::make_pair(std::move(key), std::move(value));
}

bool starts_with(const std::string &s, const char *prefix) {
    return s.rfind(prefix, 0) == 0;
}

} // namespace

Result<std::vector<std::pair<std::string, std::string>>> parse_env_file(
    const std::string &env_path) {
    std::ifstream in(env_path, std::ios::binary);
    if (!in) return credentials_error("Failed to read .env file: " + env_path);

    std::vector<std::pair<std::string, std::string>> pairs;
    std::string line;
    while (std::getline(in, line)) {
        if (auto kv = parse_env_line(line)) pairs.push_back(std::move(*kv));
    }
    return pairs;
}

Result<std::optional<std::pair<std::string, std::string>>> load_app_credentials(
    const std::string &env_path) {
    std::error_code ec;
    if (!std::filesystem::exists(env_path, ec)) {
        return std::optional<std::pair<std::string, std::string>>{};
    }

    auto pairs = parse_env_file(env_path);
    if (!pairs.ok()) return pairs.error();

    std::optional<std::string> app_id, app_secret;
    for (auto &[key, value] : pairs.value()) {
        if (key == "QOBUZ_APP_ID") app_id = value;
        else if (key == "QOBUZ_APP_SECRET") app_secret = value;
    }

    if (app_id && app_secret) {
        return std::optional<std::pair<std::string, std::string>>{
            std::make_pair(std::move(*app_id), std::move(*app_secret))};
    }
    return std::optional<std::pair<std::string, std::string>>{};
}

Result<void> save_app_credentials(const std::string &env_path, const std::string &app_id,
                                  const std::string &app_secret) {
    std::string existing;
    {
        std::ifstream in(env_path, std::ios::binary);
        if (in) {
            std::ostringstream ss;
            ss << in.rdbuf();
            existing = ss.str();
        }
    }

    std::vector<std::string> lines;
    {
        std::istringstream ss(existing);
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
    }

    bool id_written = false, secret_written = false;
    for (auto &line : lines) {
        if (starts_with(line, "QOBUZ_APP_ID=") || line == "# QOBUZ_APP_ID=") {
            line = "QOBUZ_APP_ID=" + app_id;
            id_written = true;
        } else if (starts_with(line, "QOBUZ_APP_SECRET=") || line == "# QOBUZ_APP_SECRET=") {
            line = "QOBUZ_APP_SECRET=" + app_secret;
            secret_written = true;
        }
    }
    if (!id_written) lines.push_back("QOBUZ_APP_ID=" + app_id);
    if (!secret_written) lines.push_back("QOBUZ_APP_SECRET=" + app_secret);

    std::ofstream out(env_path, std::ios::binary | std::ios::trunc);
    if (!out) return io_error("cannot write " + env_path);
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) out << '\n';
        out << lines[i];
    }
    if (!out) return io_error("failed writing " + env_path);

    // 0600 like the Rust version; ignore failure on filesystems without
    // POSIX permissions.
    std::error_code ec;
    std::filesystem::permissions(env_path,
                                 std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace, ec);
    return {};
}

namespace detail {

std::string capitalize_first_letter(const std::string &s) {
    if (s.empty()) return s;
    std::string out = s;
    out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
}

Result<std::string> extract_bundle_url(const std::string &html) {
    static const std::regex re(R"re(src="(/[^"]*bundle[^"]*\.js)")re");
    std::smatch m;
    if (!std::regex_search(html, m, re)) {
        return credentials_error("Could not find bundle.js URL in login page");
    }
    return "https://play.qobuz.com" + m[1].str();
}

Result<std::string> extract_app_id_from_bundle(const std::string &js) {
    static const std::regex re(R"re(production:\{api:\{appId:"(\d+)")re");
    std::smatch m;
    if (!std::regex_search(js, m, re)) {
        return credentials_error("Could not find production appId in bundle JavaScript");
    }
    return m[1].str();
}

Result<std::string> extract_app_secret_from_bundle(const std::string &js) {
    // \):[a-z]\.initialSeed\("(seed)",window\.utimezone\.(timezone)\)
    static const std::regex seed_tz_re(
        R"re(\):[a-z]\.initialSeed\("(.*?)",window\.utimezone\.([a-z]+)\))re");
    std::smatch m;
    if (!std::regex_search(js, m, seed_tz_re)) {
        return credentials_error("Could not find seed and timezone in bundle JavaScript");
    }
    std::string seed = m[1].str();
    std::string timezone = m[2].str();

    std::string title_case = capitalize_first_letter(timezone);
    std::regex info_extras_re("name:\"[^\"]*/" + title_case + "\"[^}]*");
    std::smatch tz_match;
    if (!std::regex_search(js, tz_match, info_extras_re)) {
        return credentials_error("Could not find timezone object with info and extras");
    }
    std::string tz_object = tz_match[0].str();

    std::string info, extras;
    static const std::regex info_re(R"re(info:"([^"]*)")re");
    static const std::regex extras_re(R"re(extras:"([^"]*)")re");
    std::smatch im, em;
    if (std::regex_search(tz_object, im, info_re)) info = im[1].str();
    if (std::regex_search(tz_object, em, extras_re)) extras = em[1].str();

    std::string encoded = seed + info + extras;
    if (encoded.size() <= 44) {
        return credentials_error("Concatenated seed+info+extras too short to decode");
    }
    encoded.resize(encoded.size() - 44);

    auto decoded = ae::base64_decode(encoded);
    if (!decoded) {
        return credentials_error("Failed to base64-decode app secret");
    }
    return *decoded;
}

} // namespace detail

Result<std::pair<std::string, std::string>> extract_from_web_player(const ae::HttpClient &http) {
    auto login_page = http.get("https://play.qobuz.com/login");
    if (!login_page.ok()) return from_engine(login_page.error());

    auto bundle_url = detail::extract_bundle_url(login_page.value().body);
    if (!bundle_url.ok()) return bundle_url.error();

    auto bundle_js = http.get(bundle_url.value());
    if (!bundle_js.ok()) return from_engine(bundle_js.error());

    auto app_id = detail::extract_app_id_from_bundle(bundle_js.value().body);
    if (!app_id.ok()) return app_id.error();

    auto app_secret = detail::extract_app_secret_from_bundle(bundle_js.value().body);
    if (!app_secret.ok()) return app_secret.error();

    return std::make_pair(app_id.take(), app_secret.take());
}

} // namespace kb
