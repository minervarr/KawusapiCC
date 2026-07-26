#include "errors.hh"

#include "ae/http.hh"

namespace kb {

Error from_engine(const ae::Error &e) {
    switch (e.kind) {
        case ae::ErrorKind::Io:
            return make_error(ErrorCode::Io, "I/O error: " + e.message, e.code);
        case ae::ErrorKind::Canceled:
            return canceled_error();
        case ae::ErrorKind::Tagging:
            return make_error(ErrorCode::Metadata, "Metadata error: " + e.message, e.code);
        case ae::ErrorKind::Parse:
            return make_error(ErrorCode::ApiResponseParse,
                              "Failed to parse API response: " + e.message, e.code);
        case ae::ErrorKind::Network:
        case ae::ErrorKind::Http:
        default: {
            Error out = make_error(ErrorCode::Http, "HTTP request failed: " + e.message, e.code);
            out.retryable_network = ae::is_retryable_network_error(e);
            return out;
        }
    }
}

} // namespace kb
