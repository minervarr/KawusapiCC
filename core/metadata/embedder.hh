#pragma once

// Port of src/metadata/embedder/* — builds the tag payload from
// ComprehensiveMetadata (format-specific artist/composer/date handling)
// and writes it via the engine's TagLib facade.

#include <string>

#include "../core/errors.hh"
#include "config.hh"
#include "extractor.hh"

namespace kb {

// FLAC -> Vorbis Comments, MP3 -> ID3v2 (decided by the file extension,
// like the Rust file-type probe). Existing tags are replaced.
Result<void> embed_metadata_in_file(const std::string &path,
                                    const ComprehensiveMetadata &meta,
                                    const MetadataConfig &config);

} // namespace kb
