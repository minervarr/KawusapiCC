#pragma once

// Port of src/errors.rs (QobuzApiError).

#include <cassert>
#include <optional>
#include <string>
#include <utility>

#include "ae/result.hh"

namespace kb {

enum class ErrorCode {
    Authentication,
    Http,
    Io,
    ApiErrorResponse,
    ApiResponseParse,
    Initialization,
    Credentials,
    Download,
    Metadata,
    ResourceNotFound,
    RateLimit,
    InvalidParameter,
    UnexpectedApiResponse,
    Canceled,
};

struct Error {
    ErrorCode code = ErrorCode::UnexpectedApiResponse;
    // Pre-formatted, matching the Rust Display strings
    // (e.g. "Authentication failed: bad token").
    std::string message;
    // Raw API error code for ApiErrorResponse, HTTP status for Http.
    int api_code = 0;
    // True for transient transport failures (timeouts, resets) — the
    // is_retryable_network_error classification from download_io.rs.
    bool retryable_network = false;
};

inline Error make_error(ErrorCode code, std::string message, int api_code = 0) {
    return Error{code, std::move(message), api_code};
}

inline Error auth_error(std::string msg) {
    return make_error(ErrorCode::Authentication, "Authentication failed: " + std::move(msg));
}
inline Error io_error(std::string msg) {
    return make_error(ErrorCode::Io, "I/O error: " + std::move(msg));
}
inline Error api_error_response(int code, const std::string &msg, const std::string &status) {
    return make_error(ErrorCode::ApiErrorResponse,
                      "API error " + std::to_string(code) + ": " + msg + " (status " + status + ")",
                      code);
}
inline Error parse_error(const std::string &content, const std::string &source_info = {}) {
    std::string msg = "Failed to parse API response: " + content;
    if (!source_info.empty()) msg += " (" + source_info + ")";
    return make_error(ErrorCode::ApiResponseParse, std::move(msg));
}
inline Error initialization_error(std::string msg) {
    return make_error(ErrorCode::Initialization,
                      "Service initialization failed: " + std::move(msg));
}
inline Error credentials_error(std::string msg) {
    return make_error(ErrorCode::Credentials, "Invalid credentials: " + std::move(msg));
}
inline Error download_error(std::string msg) {
    return make_error(ErrorCode::Download, "Download failed: " + std::move(msg));
}
inline Error metadata_error(std::string msg) {
    return make_error(ErrorCode::Metadata, "Metadata error: " + std::move(msg));
}
inline Error not_found_error(const std::string &resource_type, const std::string &resource_id) {
    return make_error(ErrorCode::ResourceNotFound,
                      resource_type + " not found: " + resource_id);
}
inline Error rate_limit_error(std::string msg) {
    return make_error(ErrorCode::RateLimit, "Rate limited: " + std::move(msg));
}
inline Error invalid_parameter_error(std::string msg) {
    return make_error(ErrorCode::InvalidParameter, "Invalid parameter: " + std::move(msg));
}
inline Error unexpected_response_error(std::string msg) {
    return make_error(ErrorCode::UnexpectedApiResponse,
                      "Unexpected API response: " + std::move(msg));
}
inline Error canceled_error() {
    return make_error(ErrorCode::Canceled, "Download cancelled");
}

// Maps an engine transport/file error into the domain error space.
Error from_engine(const ae::Error &e);

inline bool is_retryable_network_error(const Error &e) { return e.retryable_network; }

// Same shape as ae::Result but carrying the domain Error.
template <typename T>
class Result {
public:
    Result(T value) : value_(std::move(value)) {}
    Result(Error error) : error_(std::move(error)) {}

    bool ok() const { return value_.has_value(); }
    explicit operator bool() const { return ok(); }

    T &value() {
        assert(ok());
        return *value_;
    }
    const T &value() const {
        assert(ok());
        return *value_;
    }
    T take() {
        assert(ok());
        return std::move(*value_);
    }

    const Error &error() const {
        assert(!ok());
        return *error_;
    }

private:
    std::optional<T> value_;
    std::optional<Error> error_;
};

template <>
class Result<void> {
public:
    Result() = default;
    Result(Error error) : error_(std::move(error)) {}

    bool ok() const { return !error_.has_value(); }
    explicit operator bool() const { return ok(); }

    const Error &error() const {
        assert(!ok());
        return *error_;
    }

private:
    std::optional<Error> error_;
};

} // namespace kb
