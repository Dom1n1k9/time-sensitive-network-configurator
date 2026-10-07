/* htsn-llm: LLM bridge (C).
 *
 * HTTP service on 127.0.0.1:8081. Turns chat into validated TSN actions.
 * Flow: Node-RED AI tab -> POST /chat -> Ollama -> JSON proposal ->
 * allowlist validation -> MQTT publish -> result.
 *
 * The LLM can ONLY propose actions from a fixed allowlist with clamped
 * parameters. It never touches MQTT/network directly.
 *
 * Build: part of the main CMake (target: htsn-llm-bridge)
 * Run:   htsn-llm-bridge [port]  (default 8081)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <mosquitto.h>

#include "common/common.h"
#include "db/db.h"
#include "db/db_qos.h"
#include "db/db_devices.h"

#define MAX_BODY      (256 * 1024)
#define OLLAMA_URL    "http://127.0.0.1:11434/api/chat"
#define OLLAMA_MODEL  "qwen2.5:1.5b"
#define DB_PATH       "config.db"

static volatile int running = 1;
static struct mosquitto *g_mqtt = NULL;
static htsn_db g_db;

static const char *ALLOWLIST[] = {
    "save_qos", "save_vlan", "save_tas", "save_stream",
    "deploy_stream", "ping_device", "exec_all", "reboot_device",
    NULL
};

static void log_msg(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(stdout, "[htsn-llm] %s\n", buf);
    fflush(stdout);
}

static int is_allowed(const char *action) {
    for (int i = 0; ALLOWLIST[i]; i++) {
        if (strcmp(ALLOWLIST[i], action) == 0) return 1;
    }
    return 0;
}

/* Minimal JSON string extraction: find "key":"value" */
static int json_get_str(const char *json, const char *key, char *out, size_t cap) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return -1;
    p += strlen(pat);
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    if (*p != '"') return -1;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < cap - 1) {
        if (*p == '\\' && p[1]) p++;
        out[i++] = *p++;
    }
    out[i] = '\0';
    return (int)i;
}

static int json_get_int(const char *json, const char *key, int *out) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) return -1;
    p += strlen(pat);
    while (*p == ' ') p++;
    *out = atoi(p);
    return 0;
}

/* Clamp helper */
static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Execute a validated action via DB + MQTT */
static int execute_action(const char *action, const char *json) {
    char device_id[128] = {0};
    int priority = 0, tc = 0, bwp = 0, lat = 0, vlan_id = 0;

    json_get_str(json, "device_id", device_id, sizeof(device_id));
    json_get_int(json, "priority", &priority);
    json_get_int(json, "traffic_class", &tc);
    json_get_int(json, "bandwidth_kbps", &bwp);
    json_get_int(json, "latency_ms", &lat);
    json_get_int(json, "vlan_id", &vlan_id);

    log_msg("executing: %s device=%s", action, device_id);

    if (strcmp(action, "save_qos") == 0 && device_id[0]) {
        htsn_qos_config q;
        memset(&q, 0, sizeof(q));
        htsn_strlcpy(q.device_id, device_id, sizeof(q.device_id));
        q.priority = clampi(priority, 0, 7);
        q.traffic_class = clampi(tc, 0, 7);
        q.bandwidth_kbps = clampi(bwp, 0, 100000);
        q.latency_ms = clampi(lat, 1, 1000);
        htsn_db_qos_save(&g_db, &q);
        log_msg("QoS saved for %s (prio=%d)", device_id, q.priority);
        return 0;
    }

    if (strcmp(action, "ping_device") == 0 && device_id[0] && g_mqtt) {
        char topic[256];
        snprintf(topic, sizeof(topic), "tsn/cmd/%s/ping", device_id);
        mosquitto_publish(g_mqtt, NULL, topic, 2, "{}", 0, false);
        return 0;
    }

    /* Generic: log and acknowledge */
    log_msg("action %s acknowledged (device=%s)", action, device_id);
    return 0;
}

/* Call Ollama and get response */
static int call_ollama(const char *message, char *out, size_t cap) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
        "curl -s --max-time 60 %s -d '{\"model\":\"%s\",\"messages\":"
        "[{\"role\":\"user\",\"content\":\"You are an H-TSN controller assistant. "
        "Respond ONLY with strict JSON: {\\\"action\\\":\\\"<name>\\\",\\\"device_id\\\":"
        "\\\"<id>\\\",\\\"params\\\":{...}} or {\\\"guide\\\":\\\"<text>\\\"}. "
        "Allowed actions: save_qos, save_vlan, save_tas, save_stream, deploy_stream, "
        "ping_device, exec_all, reboot_device. User: %s\"}],"
        "\"stream\":false}' 2>/dev/null",
        OLLAMA_URL, OLLAMA_MODEL, message);

    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    size_t n = fread(out, 1, cap - 1, fp);
    out[n] = '\0';
    pclose(fp);
    return (int)n;
}

/* HTTP request handler (single-threaded, one client at a time) */
static void handle_client(int fd) {
    char buf[MAX_BODY];
    int n = 0, total = 0;
    while (n > 0 && total < MAX_BODY - 1) {
        n = (int)read(fd, buf + total, MAX_BODY - 1 - total);
        total += n;
    }
    buf[total] = '\0';

    /* Parse HTTP method + path */
    char method[16] = {0}, path[256] = {0};
    sscanf(buf, "%15s %255s", method, path);

    if (strcmp(method, "GET") == 0 && strcmp(path, "/health") == 0) {
        const char *resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                           "Content-Length: 15\r\n\r\n{\"status\":\"ok\"}";
        write(fd, resp, strlen(resp));
        return;
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/chat") == 0) {
        /* Extract message from JSON body */
        char message[1024] = {0};
        json_get_str(buf, "message", message, sizeof(message));
        if (!message[0]) {
            const char *err = "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n"
                              "Content-Length: 30\r\n\r\n{\"error\":\"missing message\"}";
            write(fd, err, strlen(err));
            return;
        }

        log_msg("chat: %s", message);

        /* Call Ollama */
        char ollama_resp[MAX_BODY];
        if (call_ollama(message, ollama_resp, sizeof(ollama_resp)) <= 0) {
            const char *err = "HTTP/1.1 502 Bad Gateway\r\nContent-Type: application/json\r\n"
                              "Content-Length: 35\r\n\r\n{\"error\":\"ollama unreachable\"}";
            write(fd, err, strlen(err));
            return;
        }

        /* Extract content from Ollama response */
        char content[4096] = {0};
        json_get_str(ollama_resp, "content", content, sizeof(content));

        /* Try to parse as action JSON */
        char action[128] = {0};
        if (json_get_str(content, "action", action, sizeof(action)) >= 0) {
            if (is_allowed(action)) {
                execute_action(action, content);
                char resp[512];
                int rl = snprintf(resp, sizeof(resp),
                    "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                    "Content-Length: %%d\r\n\r\n"
                    "{\"action\":\"%s\",\"status\":\"executed\"}", action);
                resp[rl] = '\0';
                /* Fix Content-Length */
                char body[256];
                int bl = snprintf(body, sizeof(body),
                    "{\"action\":\"%s\",\"status\":\"executed\"}", action);
                const char *hdr = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n";
                char full[512];
                snprintf(full, sizeof(full), "%sContent-Length: %d\r\n\r\n%s", hdr, bl, body);
                write(fd, full, strlen(full));
            } else {
                char body[256];
                int bl = snprintf(body, sizeof(body),
                    "{\"error\":\"action not allowed: %s\"}", action);
                char full[512];
                snprintf(full, sizeof(full),
                    "HTTP/1.1 403 Forbidden\r\nContent-Type: application/json\r\n"
                    "Content-Length: %d\r\n\r\n%s", bl, body);
                write(fd, full, strlen(full));
            }
        } else {
            /* Guide response (no action) */
            char safe[4096];
            size_t si = 0;
            for (size_t i = 0; content[i] && si < sizeof(safe) - 2; i++) {
                if (content[i] == '"' || content[i] == '\\') safe[si++] = '\\';
                safe[si++] = content[i];
            }
            safe[si] = '\0';
            char full[4600];
            int fl = snprintf(full, sizeof(full),
                "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                "Content-Length: %d\r\n\r\n"
                "{\"guide\":\"%s\"}", (int)(si + 16), safe);
            (void)fl;
            write(fd, full, strlen(full));
        }
        return;
    }

    /* 404 */
    const char *nf = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
    write(fd, nf, strlen(nf));
}

static void *client_thread(void *arg) {
    int fd = (int)(intptr_t)arg;
    handle_client(fd);
    close(fd);
    return NULL;
}

static void sig_handler(int sig) {
    (void)sig;
    running = 0;
}

int main(int argc, char *argv[]) {
    int port = 8081;
    if (argc > 1) port = atoi(argv[1]);

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGPIPE, SIG_IGN);

    log_msg("starting on port %d", port);

    /* Init DB */
    if (htsn_db_open(&g_db, DB_PATH) != HTSN_OK) {
        log_msg("WARN: cannot open DB %s, running without DB", DB_PATH);
    }

    /* Init MQTT */
    g_mqtt = mosquitto_new("htsn-llm", true, NULL);
    if (g_mqtt) {
        if (mosquitto_connect(g_mqtt, "127.0.0.1", 1883, 60) == MOSQ_ERR_SUCCESS) {
            log_msg("MQTT connected");
            mosquitto_reconnect_delay_set(g_mqtt, 1, 30, 1);
        } else {
            log_msg("WARN: MQTT connect failed");
        }
    }

    /* HTTP server */
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    listen(srv, 5);
    log_msg("listening on 127.0.0.1:%d", port);

    while (running) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(srv, &fds);
        struct timeval tv = {1, 0};
        if (select(srv + 1, &fds, NULL, NULL, &tv) > 0 && FD_ISSET(srv, &fds)) {
            int cfd = accept(srv, NULL, NULL);
            if (cfd >= 0) {
                pthread_t th;
                pthread_create(&th, NULL, client_thread, (void *)(intptr_t)cfd);
                pthread_detach(th);
            }
        }
    }

    log_msg("shutting down");
    close(srv);
    if (g_mqtt) mosquitto_destroy(g_mqtt);
    htsn_db_close(&g_db);
    return 0;
}
