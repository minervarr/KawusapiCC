package io.nava.kobuzapi;

/**
 * Native Qobuz API client (C++ port of the kobuzapi Rust library).
 *
 * <p>Usage: obtain a service handle via {@link #create}, authenticate with
 * {@link #login} or {@link #loginWithToken}, then call API/download methods.
 * All methods returning {@code null} (or {@code 0} for {@code create}) failed;
 * {@link #getLastError()} returns the message for the current thread.
 *
 * <p>Search/browse results are returned as the raw Qobuz API JSON for the
 * caller to parse. Long-running download calls block; run them on a worker
 * thread and poll {@link #getProgress} from elsewhere.
 */
public final class Kobuzapi {

    static {
        System.loadLibrary("kobuzapi");
    }

    private Kobuzapi() {}

    /** Quality format IDs (see Qobuz API). */
    public static final int QUALITY_MP3_320 = 5;
    public static final int QUALITY_FLAC_16_44 = 6;
    public static final int QUALITY_FLAC_24_96 = 7;
    public static final int QUALITY_FLAC_24_192 = 27;

    /** Returns the native library version. */
    public static native String version();

    /** Returns the last error message for the current thread. */
    public static native String getLastError();

    /**
     * Creates a service handle. Pass empty appId/appSecret to load them from
     * the .env file at {@code envPath} or scrape them from the Qobuz web
     * player. {@code caBundlePath} must point to a PEM CA bundle (extract
     * cacert.pem from assets to app-private storage first). Returns 0 on
     * failure.
     */
    public static native long create(String appId, String appSecret, String caBundlePath,
                                     String envPath);

    /** Frees a service handle. */
    public static native void destroy(long handle);

    /** Email/password login. Returns the user auth token, or null. */
    public static native String login(long handle, String email, String password);

    /** Token login. Returns the account country code (may be empty), or null. */
    public static native String loginWithToken(long handle, String userId, String token);

    /** Sets a previously obtained auth token without a login round-trip. */
    public static native void setAuthToken(long handle, String token);

    /**
     * Generic signed GET against the Qobuz API, e.g.
     * {@code apiGet(h, "/album/search", new String[]{"query"}, new String[]{"Miles"})}.
     * Returns the raw response JSON, or null.
     */
    public static native String apiGet(long handle, String endpoint, String[] keys,
                                       String[] values);

    /** Returns the signed download URL response as JSON, or null. */
    public static native String getTrackFileUrl(long handle, long trackId, int formatId);

    /** Downloads one track; returns its file path, or null. */
    public static native String downloadTrack(long handle, int trackId, int formatId,
                                              String outputDir, boolean embedMetadata);

    /** Downloads an album; returns downloaded file paths, or null. */
    public static native String[] downloadAlbum(long handle, String albumId, int formatId,
                                                String outputDir, boolean embedMetadata,
                                                int concurrency);

    /** Downloads a playlist; returns downloaded file paths, or null. */
    public static native String[] downloadPlaylist(long handle, String playlistId,
                                                   int formatId, String outputDir,
                                                   boolean embedMetadata, int concurrency);

    /** Downloads an artist's releases; returns downloaded file paths, or null. */
    public static native String[] downloadArtist(long handle, int artistId, int formatId,
                                                 String outputDir, boolean embedMetadata,
                                                 int concurrency);

    /** Requests cancellation of in-flight downloads on this handle. */
    public static native void cancelDownloads(long handle);

    /** Clears the cancellation flag so the handle can download again. */
    public static native void resetCancel(long handle);

    /** Returns {trackId, downloadedBytes, totalBytes} of the latest progress. */
    public static native long[] getProgress(long handle);

    /** Sets a global API request throttle; 0 disables it. */
    public static native void setRequestsPerMinute(int rpm);
}
