// Bottom screen (320×240, touch) with the classic look of the web edition.
#pragma once
#include "common.h"

#define BOT_W 320
#define BOT_H 240

void ui_room_changed(void);
void ui_chat(u32 eid, const char *text);
/** The player ignores the speaker (Friends Console). */
bool ui_chat_blocked(u32 eid);
void ui_toast(const char *text, u64 now);
/** Closes the open window (back to the room panel). */
void ui_close(void);
void ui_notice(const char *text);
void ui_item_changed(u32 id);
/** Hotel alert (HOTEL_ALERT, JSON {id,title,message}). */
void ui_alert(const char *json);
void ui_social_changed(void);
void ui_name_changed(u32 eid);
/** Handles buttons, pad and touch. Returns false when the player wants to quit. */
bool ui_input(u32 down, u32 held, u64 now);
void ui_draw(u64 now);
bool casino_top(u64 now);
void casino_receive(const char *json, u64 now);
void casino_notice(const char *text);
/** Furniture trade window (s2c TRADE JSON). */
void trade_receive(const char *json, u64 now);
void trade_invite(u32 eid);
/** Top-screen catalogue preview (drawn over the room while shopping). */
void shop_top(u64 now);
/** Top screen of the wardrobe: the large preview. */
void look_top(u64 now);
