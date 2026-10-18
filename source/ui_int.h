// Interface internals: bottom-screen panels (like the windows of the
// web edition) and build mode driven by the console buttons.
#pragma once
#include "common.h"
#include "skin.h"
#include "ui.h"

typedef enum {
  P_ROOM,     /* no window: touch minimap and quick actions */
  P_NAV,      /* room navigator and room creation */
  P_SHOP,     /* catalogue */
  P_HAND,     /* inventory */
  P_ME,       /* my character */
  P_FRIENDS,  /* Friends Console */
  P_CHAT,     /* chat history */
  P_SETTINGS, /* connection, server and help */
  P_LOOK,     /* wardrobe */
  P_ITEM,     /* selected furniture */
  P_ENT,      /* another character */
  P_PETS,     /* pets */
  P_MOVE,     /* one-time move of the character to another device */
  P_GAME, P_PROFILE, P_CASINO, P_SNOW, P_GAMES, P_RIGHTS,
  P_TRADE,    /* furniture trade with another player */
  P_SUPPORT,  /* donations, Tweebbo Club and chips: what the PC edition offers */
  P_BAR,      /* After Hours Bar: the menu and the held drink */
  P_MUSIC,    /* the room playlist (Jukebox / Trax) */
  P_COUNT
} Panel;

// Panel area (between the header and the icon bar).
#define HEAD_H 18
#define BAR_H 34
#define BAR_Y (BOT_H - BAR_H)
#define WIN_X 2
#define WIN_Y (HEAD_H + 1)
#define WIN_W (BOT_W - 4)
#define WIN_H (BAR_Y - HEAD_H - 2)
#define TITLE_H 17
/** Content area inside a window. */
#define IN_X (WIN_X + 5)
#define IN_Y (WIN_Y + TITLE_H + 2)
#define IN_W (WIN_W - 10)
#define IN_H (WIN_H - TITLE_H - 7)

/** Button input of the frame for panels (D-Pad and A/B while a window is open). */
typedef struct {
  u32 down, held;
  bool up, dn, left, right, a, b, x, y;
} Keys;
extern Keys K;

void ui_open(Panel p);
Panel ui_panel(void);
void ui_close(void);
void ui_select_item(u32 id);
void ui_select_ent(u32 eid);

/** System keyboard. Returns true when accepted. */
/** Modal confirmation dialog (drawn on top). */
void ui_confirm(const char *text, const char *yes, void (*fn)(void *), void *ctx);

// Panels (ui_panels.c and ui_social.c).
void nav_draw(u64 now);
void nav_opened(void);
void shop_draw(u64 now);
void hand_draw(u64 now);
void item_draw(u64 now);
void friends_draw(u64 now);
void friends_opened(void);
void friends_poll(u64 now);
bool friends_is_blocked(u32 guest);
int friends_badge(void);
void friends_act(const char *action, u32 peer, const char *text);
void pets_draw(u64 now);

// Build mode (build.c).
bool build_active(void);
/** Places an inventory item (`inv_id`) or moves a placed one (`moving`). */
void build_start(u16 def, u32 inv_id, u32 moving);
void build_cancel(void);
/** Buttons and pad in build mode; true if they were consumed. */
bool build_input(u32 down, u32 held, u64 now);
void build_draw_panel(u64 now);
void build_tap_tile(int x, int y);
/** The server confirmed (ITEM_ADD/UPDATE) or rejected (NOTICE) the placement. */
void build_result(u32 item_id);
void build_notice(void);

void spaces_reset(bool restore);

void games_draw(u64 now);
/** Games hub: Snow Storm and one-on-one challenges with people in the room. */
void games_hub_draw(u64 now);
void hand_select_def(u16 def);

void games_poll(void);
void games_challenge(u32 target);

void profile_open(u32 guest);
void profile_draw(u64 now);

void trade_draw(u64 now);
void support_draw(u64 now);

void casino_poll(u64 now);
void casino_draw(u64 now);
void casino_close(bool notify);
void casino_notice(const char *text);
