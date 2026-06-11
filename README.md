# Kobuzapi++

Android native port (C++17) of the [kobuzapi](https://github.com/minervarr/kobuzapi) Rust
library — a Qobuz music streaming API client with search, browse, authenticated downloads
(resume + retry + cancellation) and metadata tagging.

## Layout

```
kobuzapi/                  Android library module (com.android.library)
  src/main/cpp/
    core/                  errors, models, JSON, signing, credentials
    api/                   signed requests, throttling, QobuzApiService
    metadata/              extractor, performers parsing, TagLib embedder
    download/              track/album/playlist/artist download orchestration
    kobuzapi_jni.cpp       JNI bridge (io.nava.kobuzapi.Kobuzapi)
    third_party/nlohmann/  vendored nlohmann/json 3.11.3
  src/main/assets/cacert.pem   Mozilla CA bundle for libcurl TLS verification
engine/archive_engine/     reusable native engine (git submodule)
    util/  ae_util         md5, base64, filename sanitization, Result<T>
    net/   ae_net          libcurl + mbedTLS HTTP client and downloader
    tag/   ae_tag          TagLib 2 tagging facade (FLAC / MP3, cover art)
    archive/ ae_archive    libarchive extract/compress (off in this project)
```

The engine modules are option-gated (`AE_BUILD_NET`, `AE_BUILD_TAG`, `AE_BUILD_ARCHIVE`,
`AE_BUILD_JNI`) so other projects can consume only what they need via
`add_subdirectory`.

## Building

Clone with submodules:

```sh
git clone --recurse-submodules <repo>
```

Then build the AAR with Gradle (Android SDK + NDK required):

```sh
./gradlew :kobuzapi:assembleRelease
```

The first native build downloads and cross-compiles mbedTLS, curl, TagLib and utfcpp via
CMake `ExternalProject`; later builds are incremental.

## Usage (Java/Kotlin)

```java
// 1. Extract assets/cacert.pem to app-private storage once.
// 2. Create a service (empty appId/appSecret => scraped from the web player
//    and cached in the .env file).
long h = Kobuzapi.create("", "", caCertPath, envPath);
if (h == 0) throw new IllegalStateException(Kobuzapi.getLastError());

String token = Kobuzapi.login(h, email, password);

// Search/browse return raw Qobuz API JSON.
String json = Kobuzapi.apiGet(h, "/album/search",
        new String[]{"query", "limit"}, new String[]{"Miles Davis", "20"});

// Blocking download (run on a worker thread); poll getProgress() elsewhere.
String[] files = Kobuzapi.downloadAlbum(h, albumId, Kobuzapi.QUALITY_FLAC_16_44,
        downloadDir, /*embedMetadata=*/true, /*concurrency=*/4);

Kobuzapi.cancelDownloads(h);  // from any thread
Kobuzapi.destroy(h);
```

Downloads land in app-private storage; hand finished files to MediaStore for shared
storage on Android 10+.

## Parity with the Rust library

Ported faithfully: MD5 request signing, web-player app-credential extraction, login
(email/password and user-id/token), all search/browse/favorites endpoints, 429
backoff + optional requests-per-minute throttle, Range-resume downloads with
3×-exponential-backoff retry and 416 already-complete handling, album folder naming
with quality tag, and the full FLAC Vorbis / MP3 ID3v2 metadata embedding logic
(artists from performers, composer dedup, conductor album-artist rule, FLAC custom
keys). Async tokio tasks are replaced by bounded worker threads.
