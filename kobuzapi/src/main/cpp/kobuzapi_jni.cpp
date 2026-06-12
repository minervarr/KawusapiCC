// JNI bridge for io.nava.kobuzapi.Kobuzapi.
//
// Conventions: no exceptions cross the boundary; failures return null/false/0
// and store a message retrievable via getLastError() (thread-local). Search
// and browse calls return the raw API JSON for the Kotlin/Java side to parse.
// Download progress is exposed by polling getProgress().

#include <jni.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "api/service.hh"
#include "core/errors.hh"
#include "core/json.hh"
#include "download/download.hh"

namespace {

thread_local std::string g_last_error;

struct ServiceHandle {
    kb::QobuzApiService service;
    std::atomic<bool> cancel{false};

    std::mutex progress_mutex;
    int progress_track = 0;
    uint64_t progress_downloaded = 0;
    uint64_t progress_total = 0;

    explicit ServiceHandle(kb::QobuzApiService svc) : service(std::move(svc)) {}
};

ServiceHandle *handle_of(jlong h) { return reinterpret_cast<ServiceHandle *>(h); }

std::string jstr(JNIEnv *env, jstring js) {
    if (!js) return {};
    const char *cs = env->GetStringUTFChars(js, nullptr);
    std::string s(cs ? cs : "");
    if (cs) env->ReleaseStringUTFChars(js, cs);
    return s;
}

jstring jnew(JNIEnv *env, const std::string &s) { return env->NewStringUTF(s.c_str()); }

void set_error(const kb::Error &e) { g_last_error = e.message; }

kb::DownloadOptions make_options(ServiceHandle *h, jboolean embed_metadata,
                                 jint concurrency) {
    kb::DownloadOptions options;
    if (embed_metadata) options.metadata = kb::MetadataConfig();
    if (concurrency > 0) options.concurrency = concurrency;
    options.cancel = &h->cancel;
    options.progress = [h](int track_id, uint64_t downloaded, uint64_t total) {
        std::lock_guard<std::mutex> lock(h->progress_mutex);
        h->progress_track = track_id;
        h->progress_downloaded = downloaded;
        h->progress_total = total;
    };
    return options;
}

jobjectArray to_string_array(JNIEnv *env, const std::vector<std::string> &v) {
    jobjectArray arr =
        env->NewObjectArray(static_cast<jsize>(v.size()), env->FindClass("java/lang/String"),
                            nullptr);
    for (jsize i = 0; i < static_cast<jsize>(v.size()); ++i) {
        jstring s = jnew(env, v[i]);
        env->SetObjectArrayElement(arr, i, s);
        env->DeleteLocalRef(s);
    }
    return arr;
}

} // namespace

extern "C" {

JNIEXPORT jstring JNICALL
Java_io_nava_kobuzapi_Kobuzapi_getLastError(JNIEnv *env, jclass) {
    return jnew(env, g_last_error);
}

JNIEXPORT jstring JNICALL
Java_io_nava_kobuzapi_Kobuzapi_version(JNIEnv *env, jclass) {
    return jnew(env, "kobuzapi-android 1.0.0 (port of kobuzapi 1.0.1)");
}

// app_id/app_secret may be empty: then credentials come from envPath's .env
// or are scraped from the Qobuz web player.
JNIEXPORT jlong JNICALL
Java_io_nava_kobuzapi_Kobuzapi_create(JNIEnv *env, jclass, jstring jAppId,
                                      jstring jAppSecret, jstring jCaBundlePath,
                                      jstring jEnvPath) {
    kb::QobuzApiService::Config config;
    config.app_id = jstr(env, jAppId);
    config.app_secret = jstr(env, jAppSecret);
    config.ca_bundle_path = jstr(env, jCaBundlePath);
    config.env_path = jstr(env, jEnvPath);

    auto service = kb::QobuzApiService::create(std::move(config));
    if (!service.ok()) {
        set_error(service.error());
        return 0;
    }
    return reinterpret_cast<jlong>(new ServiceHandle(service.take()));
}

JNIEXPORT void JNICALL
Java_io_nava_kobuzapi_Kobuzapi_destroy(JNIEnv *, jclass, jlong handle) {
    delete handle_of(handle);
}

// Returns the user_auth_token, or null on failure.
JNIEXPORT jstring JNICALL
Java_io_nava_kobuzapi_Kobuzapi_login(JNIEnv *env, jclass, jlong handle, jstring jEmail,
                                     jstring jPassword) {
    auto *h = handle_of(handle);
    auto result = h->service.login(jstr(env, jEmail), jstr(env, jPassword));
    if (!result.ok()) {
        set_error(result.error());
        return nullptr;
    }
    return jnew(env, result.value().first);
}

// Returns the account country code (may be empty), or null on failure.
JNIEXPORT jstring JNICALL
Java_io_nava_kobuzapi_Kobuzapi_loginWithToken(JNIEnv *env, jclass, jlong handle,
                                              jstring jUserId, jstring jToken) {
    auto *h = handle_of(handle);
    auto result = h->service.login_with_token(jstr(env, jUserId), jstr(env, jToken));
    if (!result.ok()) {
        set_error(result.error());
        return nullptr;
    }
    return jnew(env, result.value());
}

JNIEXPORT void JNICALL
Java_io_nava_kobuzapi_Kobuzapi_setAuthToken(JNIEnv *env, jclass, jlong handle,
                                            jstring jToken) {
    handle_of(handle)->service.set_auth_token(jstr(env, jToken));
}

// Generic signed GET; returns the raw response JSON. Endpoint e.g.
// "/album/search", params as parallel key/value arrays.
JNIEXPORT jstring JNICALL
Java_io_nava_kobuzapi_Kobuzapi_apiGet(JNIEnv *env, jclass, jlong handle, jstring jEndpoint,
                                      jobjectArray jKeys, jobjectArray jValues) {
    auto *h = handle_of(handle);

    kb::api::Params params;
    jsize n_keys = jKeys ? env->GetArrayLength(jKeys) : 0;
    jsize n_values = jValues ? env->GetArrayLength(jValues) : 0;
    for (jsize i = 0; i < n_keys && i < n_values; ++i) {
        auto jk = (jstring)env->GetObjectArrayElement(jKeys, i);
        auto jv = (jstring)env->GetObjectArrayElement(jValues, i);
        params.emplace_back(jstr(env, jk), jstr(env, jv));
        env->DeleteLocalRef(jk);
        env->DeleteLocalRef(jv);
    }

    auto token = h->service.require_auth_token();
    if (!token.ok()) {
        set_error(token.error());
        return nullptr;
    }
    auto result = kb::api::signed_get_raw(h->service.http_client(), h->service.base_url(),
                                          jstr(env, jEndpoint), std::move(params),
                                          h->service.request_auth(token.value()));
    if (!result.ok()) {
        set_error(result.error());
        return nullptr;
    }
    return jnew(env, result.value());
}

// Returns the signed file URL response as JSON, or null on failure.
JNIEXPORT jstring JNICALL
Java_io_nava_kobuzapi_Kobuzapi_getTrackFileUrl(JNIEnv *env, jclass, jlong handle,
                                               jlong trackId, jint formatId) {
    auto *h = handle_of(handle);
    auto result = h->service.get_track_file_url(trackId, formatId);
    if (!result.ok()) {
        set_error(result.error());
        return nullptr;
    }
    const kb::FileUrl &f = result.value();
    nlohmann::json j;
    if (f.track_id) j["track_id"] = *f.track_id;
    if (f.url) j["url"] = *f.url;
    if (f.format_id) j["format_id"] = *f.format_id;
    if (f.mime_type) j["mime_type"] = *f.mime_type;
    if (f.sampling_rate) j["sampling_rate"] = *f.sampling_rate;
    if (f.bit_depth) j["bit_depth"] = *f.bit_depth;
    return jnew(env, j.dump());
}

JNIEXPORT jstring JNICALL
Java_io_nava_kobuzapi_Kobuzapi_downloadTrack(JNIEnv *env, jclass, jlong handle, jint trackId,
                                             jint formatId, jstring jOutputDir,
                                             jboolean embedMetadata) {
    auto *h = handle_of(handle);
    auto result = kb::download_track(h->service, trackId, formatId, jstr(env, jOutputDir),
                                     make_options(h, embedMetadata, 0));
    if (!result.ok()) {
        set_error(result.error());
        return nullptr;
    }
    return jnew(env, result.value());
}

JNIEXPORT jobjectArray JNICALL
Java_io_nava_kobuzapi_Kobuzapi_downloadAlbum(JNIEnv *env, jclass, jlong handle,
                                             jstring jAlbumId, jint formatId,
                                             jstring jOutputDir, jboolean embedMetadata,
                                             jint concurrency) {
    auto *h = handle_of(handle);
    auto result =
        kb::download_album(h->service, jstr(env, jAlbumId), formatId, jstr(env, jOutputDir),
                           make_options(h, embedMetadata, concurrency));
    if (!result.ok()) {
        set_error(result.error());
        return nullptr;
    }
    return to_string_array(env, result.value());
}

JNIEXPORT jobjectArray JNICALL
Java_io_nava_kobuzapi_Kobuzapi_downloadPlaylist(JNIEnv *env, jclass, jlong handle,
                                                jstring jPlaylistId, jint formatId,
                                                jstring jOutputDir, jboolean embedMetadata,
                                                jint concurrency) {
    auto *h = handle_of(handle);
    auto result = kb::download_playlist(h->service, jstr(env, jPlaylistId), formatId,
                                        jstr(env, jOutputDir),
                                        make_options(h, embedMetadata, concurrency));
    if (!result.ok()) {
        set_error(result.error());
        return nullptr;
    }
    return to_string_array(env, result.value());
}

JNIEXPORT jobjectArray JNICALL
Java_io_nava_kobuzapi_Kobuzapi_downloadArtist(JNIEnv *env, jclass, jlong handle,
                                              jint artistId, jint formatId,
                                              jstring jOutputDir, jboolean embedMetadata,
                                              jint concurrency) {
    auto *h = handle_of(handle);
    auto result =
        kb::download_artist(h->service, artistId, formatId, jstr(env, jOutputDir),
                            make_options(h, embedMetadata, concurrency));
    if (!result.ok()) {
        set_error(result.error());
        return nullptr;
    }
    return to_string_array(env, result.value());
}

// Requests cancellation of in-flight downloads on this handle.
JNIEXPORT void JNICALL
Java_io_nava_kobuzapi_Kobuzapi_cancelDownloads(JNIEnv *, jclass, jlong handle) {
    handle_of(handle)->cancel.store(true, std::memory_order_relaxed);
}

JNIEXPORT void JNICALL
Java_io_nava_kobuzapi_Kobuzapi_resetCancel(JNIEnv *, jclass, jlong handle) {
    handle_of(handle)->cancel.store(false, std::memory_order_relaxed);
}

// [trackId, downloadedBytes, totalBytes] of the most recent progress event.
JNIEXPORT jlongArray JNICALL
Java_io_nava_kobuzapi_Kobuzapi_getProgress(JNIEnv *env, jclass, jlong handle) {
    auto *h = handle_of(handle);
    jlong values[3];
    {
        std::lock_guard<std::mutex> lock(h->progress_mutex);
        values[0] = h->progress_track;
        values[1] = static_cast<jlong>(h->progress_downloaded);
        values[2] = static_cast<jlong>(h->progress_total);
    }
    jlongArray arr = env->NewLongArray(3);
    env->SetLongArrayRegion(arr, 0, 3, values);
    return arr;
}

// Global API throttle; 0 disables (relies on 429 backoff).
JNIEXPORT void JNICALL
Java_io_nava_kobuzapi_Kobuzapi_setRequestsPerMinute(JNIEnv *, jclass, jint rpm) {
    kb::api::set_requests_per_minute(rpm > 0 ? static_cast<uint32_t>(rpm) : 0);
}

} // extern "C"
