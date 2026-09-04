#include "errors.hh"

#include "arc/http.hh"

namespace kb {

Error from_engine(const arc::Error &e) {
    switch (e.kind) {
        case arc::ErrorKind::Io:
            return make_error(ErrorCode::Io, "I/O error: " + e.message, e.code);
        case arc::ErrorKind::Canceled:
            return canceled_error();
        case arc::ErrorKind::Tagging:
            return make_error(ErrorCode::Metadata, "Metadata error: " + e.message, e.code);
        case arc::ErrorKind::Parse:
            return make_error(ErrorCode::ApiResponseParse,
                              "Failed to parse API response: " + e.message, e.code);
        case arc::ErrorKind::Network:
        case arc::ErrorKind::Http:
        default: {
            Error out = make_error(ErrorCode::Http, "HTTP request failed: " + e.message, e.code);
            out.retryable_network = arc::is_retryable_network_error(e);
            return out;
        }
    }
}

} // namespace kb
