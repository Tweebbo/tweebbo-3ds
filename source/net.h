// Transport between the client and the server.
// Online: WebSocket to <server>/ws/<room> on its own thread (ws.c), with
// reconnection and renewal like client/src/main.ts. Offline: the local
// simulation (sim.c) speaking the same protocol.
#pragma once
#include "common.h"
#include "ws.h"

/** Default server; for local testing: TWEEBBO_SERVER=ws://IP:8092 ./build.sh */
#ifndef DEFAULT_SERVER
#define DEFAULT_SERVER "wss://tweebbo.lol"
#endif
#define PROD_SERVER "wss://tweebbo.lol"
#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif
#define DEFAULT_ROOM "lobby"

/** NET_REPLACED: the same character connected elsewhere; waits for net_resume(). */
typedef enum { NET_OFFLINE, NET_CONNECTING, NET_ONLINE, NET_RETRYING, NET_REPLACED, NET_LOCKED, NET_FULL, NET_DENIED } NetState;

void net_start(u64 now);
void net_stop(void);
void net_send(const u8 *data, int len);
/** Hands received messages to the world and advances the offline simulation. */
void net_update(u64 now);
bool net_online(void);
NetState net_state(void);
/** Short status text ("Connecting…", last error). */
const char *net_status(void);
/** Configured server (URL) or "offline". */
const char *net_server(void);
/** Switches server ("offline" for the simulation) and saves it. Restarts the connection. */
void net_set_server(const char *url);
bool net_server_allowed(const char *url);
/** Enters another room. `arrival` is the TRANSFER ticket (or ""). */
void net_goto(const char *slug, const char *arrival);
const char *net_room(void);

// HTTP requests to the API (/api/rooms, /api/identity…), served by another thread.
/** Queues a request; returns its id (>0) or 0 without network. */
int net_http(const char *method, const char *path, const char *json, bool auth);
/** Checks the secure connection to tweebbo.lol (GET /healthz, creates no data). */
int net_check_prod(void);
/** true when the request finished; the caller owns `out->body` (free). */
bool net_http_result(int id, HttpResult *out);
/** Character token for Authorization (empty if there is none yet). */
const char *net_token(void);
/** Switches to another character ("Save / restore character" code) and reconnects. */
void net_use_token(const char *token);
/** Takes the character back after NET_REPLACED ("Play here"). */
void net_resume(void);
void net_unlock(const char *password);
int net_retry_seconds(void);
bool net_room_full(void);

// Storage implemented by the application (SD card on the console, files in the tests).
bool store_load(const char *key, char *out, int cap);
void store_save(const char *key, const char *value);

// Shortcuts for client messages.
void send_walk(int x, int y);
void send_chat(const char *text);
void send_gesture(int kind);
void send_use(u32 item);
void send_look(u64 look);
void send_name(const char *name);
void send_buy(u16 offer);
void send_place(u32 id, int x, int y, int rot, int z);
void send_move(u32 id, int x, int y, int rot, int z);
void send_pickup(u32 id);
void send_rotate(u32 id);
void send_redeem(u32 id);
void send_drink(u32 item, int drink);
void send_pet(const char *json);

void send_decorate(int kind, const char *id);

void send_game(int op, u32 a, int b, int c, int d);
void send_casino(int op, int a, int b, int c);
/** c2s TRADE: {"action":"invite|accept|decline|cancel|add|remove|confirm","target":eid,"id":item}. */
void send_trade(const char *json);
/** Room moderation: op (MOD_*), entity id (or guest id for unban/remove-of), ban length 1-3. */
void send_room_mod(int op, u32 target, int length);
