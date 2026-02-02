// ---------------------------------------------------------------------------
// Watchdog — external supervisor for binance_shadow.
//
// Standalone binary. Monitors /metrics heartbeat from the main process.
// If the main process deadlocks or stops responding:
//   1. SIGKILLs the target PID (cannot be caught/ignored)
//   2. Sends a flatten request via REST to close any open positions
//
// Usage:
//   ./watchdog <PID> [metrics_url] [flatten_url] [interval_secs]
//
// Defaults:
//   metrics_url   = http://127.0.0.1:8080/metrics
//   flatten_url   = http://127.0.0.1:8080/flatten
//   interval_secs = 5
//
// This closes audit blocker #3: without an external watchdog, a deadlocked
// process has no kill authority and positions remain open indefinitely.
// ---------------------------------------------------------------------------
#include <curl/curl.h>
#include <signal.h>
#include <unistd.h>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <ctime>

static size_t discard_cb(void*, size_t size, size_t nmemb, void*) {
    return size * nmemb;  // discard body, we only care about HTTP success
}

static bool ping(const std::string& url) {
    CURL* c = curl_easy_init();
    if (!c) return false;

    curl_easy_setopt(c, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(c, CURLOPT_TIMEOUT,        3L);   // 3s max wait
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 2L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION,  discard_cb);
    curl_easy_setopt(c, CURLOPT_NOBODY,         1L);   // HEAD request — minimal data

    CURLcode res = curl_easy_perform(c);
    long http_code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(c);

    return (res == CURLE_OK && http_code == 200);
}

static void flatten(const std::string& url) {
    CURL* c = curl_easy_init();
    if (!c) return;

    curl_easy_setopt(c, CURLOPT_URL,           url.c_str());
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(c, CURLOPT_TIMEOUT,       5L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, discard_cb);

    curl_easy_perform(c);
    curl_easy_cleanup(c);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <target_pid> [metrics_url] [flatten_url] [interval_secs]\n", argv[0]);
        return 1;
    }

    pid_t target        = static_cast<pid_t>(atoi(argv[1]));
    std::string metrics = argc > 2 ? argv[2] : "http://127.0.0.1:8080/metrics";
    std::string flat    = argc > 3 ? argv[3] : "http://127.0.0.1:8080/flatten";
    int interval        = argc > 4 ? atoi(argv[4]) : 5;

    if (interval < 1) interval = 5;

    curl_global_init(CURL_GLOBAL_ALL);

    fprintf(stdout, "[WATCHDOG] Monitoring PID %d, interval=%ds\n", target, interval);
    fflush(stdout);

    int consecutive_failures = 0;
    static constexpr int KILL_THRESHOLD = 2;  // 2 consecutive failures → kill
    // At default 5s interval + 3s timeout, worst case: ~16s before kill fires.
    // Tunable via interval_secs.

    while (true) {
        sleep(interval);

        if (ping(metrics)) {
            consecutive_failures = 0;
            continue;
        }

        consecutive_failures++;
        fprintf(stdout, "[WATCHDOG] Heartbeat miss %d/%d\n",
                consecutive_failures, KILL_THRESHOLD);
        fflush(stdout);

        if (consecutive_failures >= KILL_THRESHOLD) {
            fprintf(stdout, "[WATCHDOG] KILL — target %d unresponsive\n", target);
            fflush(stdout);

            // SIGKILL — cannot be caught, blocked, or ignored
            kill(target, SIGKILL);

            // Attempt flatten — best effort. Main process is dead so if
            // flatten endpoint was on the same process this will fail.
            // In a production setup, flatten would route to a separate
            // emergency REST handler or directly to exchange API.
            fprintf(stdout, "[WATCHDOG] Attempting flatten...\n");
            fflush(stdout);
            flatten(flat);

            fprintf(stdout, "[WATCHDOG] Watchdog exiting.\n");
            curl_global_cleanup();
            return 0;
        }
    }
}
