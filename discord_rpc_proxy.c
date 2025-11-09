// discord_rpc_proxy.c
// Minimal proxy for Discord RPC that forwards Initialize/UpdatePresence/ShutDown to a local TCP server.
//
// Build (MinGW):
// gcc -shared -o discord-rpc.dll discord_rpc_proxy.c -lws2_32 -Wl,--kill-at

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdint.h>   // added
#include <string.h>   // added

#pragma comment(lib, "Ws2_32.lib")

#define HOST "127.0.0.1"
#define PORT 50050

static SOCKET create_socket()
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) return INVALID_SOCKET;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { WSACleanup(); return INVALID_SOCKET; } // ensure cleanup on socket() failure
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr)); // zero before use
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = inet_addr(HOST);
    if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        WSACleanup();
        return INVALID_SOCKET;
    }
    return s;
}

static void send_json(const char *json)
{
    SOCKET s = create_socket();
    if (s == INVALID_SOCKET) return;
    int len = (int)strlen(json);
    // send len (4 bytes little-endian) then payload (optional, helps parse)
    int32_t l = len;
    if (send(s, (char*)&l, 4, 0) == SOCKET_ERROR) {
        closesocket(s);
        WSACleanup();
        return;
    }
    if (len > 0) {
        int sent = 0;
        while (sent < len) {
            int n = send(s, json + sent, len - sent, 0);
            if (n == SOCKET_ERROR) {
                closesocket(s);
                WSACleanup();
                return;
            }
            sent += n;
        }
    }
    closesocket(s);
    WSACleanup();
}

// Minimal struct definitions matching Discord RPC SDK (only what we need)
typedef struct {
    const char *state;
    const char *details;
    int64_t startTimestamp;
    int64_t endTimestamp;
    const char *largeImageKey;
    const char *largeImageText;
    const char *smallImageKey;
    const char *smallImageText;
    const char *partyId;
    int partySize;
    int partyMax;
    const char *matchSecret;
    const char *joinSecret;
    const char *spectateSecret;
    int8_t instance;
} DiscordRichPresence;

#ifdef __cplusplus
extern "C" {
#endif

__declspec(dllexport) void __cdecl Discord_Initialize(const char *applicationId,
    void *eventHandlers, int autoRegister, const char *optionalSteamId)
{
    // send an initialize event with the client id
    char buf[1024];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"initialize\",\"client_id\":\"%s\",\"autoRegister\":%d}",
        applicationId ? applicationId : "", autoRegister);
    send_json(buf);
}

__declspec(dllexport) void __cdecl Discord_UpdatePresence(const DiscordRichPresence *presence)
{
    // build a small JSON payload with common fields; presence pointer may be NULL
    const char *state = presence && presence->state ? presence->state : "";
    const char *details = presence && presence->details ? presence->details : "";
    const char *large = presence && presence->largeImageKey ? presence->largeImageKey : "";
    const char *small = presence && presence->smallImageKey ? presence->smallImageKey : "";
    long long start = presence ? presence->startTimestamp : 0;
    long long end = presence ? presence->endTimestamp : 0;

    char buf[2048];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"update\",\"state\":\"%s\",\"details\":\"%s\",\"start\":%lld,\"end\":%lld,\"large\":\"%s\",\"small\":\"%s\"}",
        state, details, start, end, large, small);
    send_json(buf);
}

__declspec(dllexport) void __cdecl Discord_RunCallbacks(void)
{
    // no-op proxy; send a heartbeat if desired
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"type\":\"run_callbacks\"}");
    send_json(buf);
}

__declspec(dllexport) void __cdecl Discord_Shutdown(void)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"type\":\"shutdown\"}");
    send_json(buf);
}

#ifdef __cplusplus
}
#endif