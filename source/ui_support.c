// Support Tweebbo: what donating and chips give, and where to get them. Payments and chips
// live only in the PC edition (tweebbo.lol); the console explains them and shows how to
// reach the same character from a browser with the character code.
#include "ui_int.h"
#include "gfx.h"
#include "net.h"
#include "world.h"
#include "proto.h"
#include <stdio.h>

static int tab;

// QR code to tweebbo.lol, top right of the body. The sprite is packed 87x87; drawn a little
// smaller so the copy keeps the full width of the panel.
static void qr_block(float x, float y) {
  fill_round(x - 3, y - 3, 80, 80, 4, sk_rgb(SK_WHITE));
  if (!sk_sprite("qr-site", x, y, 0.85f)) sk_text("tweebbo.lol", x + 37, y + 34, 12, SK_TEXT, 1, 74);
  sk_text("tweebbo.lol", x + 37, y + 82, 9.5f, SK_TEXT, 1, 78);
}

void support_draw(u64 now) {
  (void)now;
  if (sk_window(WIN_X, WIN_Y, WIN_W, WIN_H, "Support Tweebbo")) { ui_close(); return; }
  sk_content(IN_X, IN_Y, IN_W, IN_H);
  if (sk_tab(IN_X + 4, IN_Y + 2, 100, 16, "Donate", tab == 0)) tab = 0;
  if (sk_tab(IN_X + 108, IN_Y + 2, 100, 16, "Chips", tab == 1)) tab = 1;
  if (K.left) tab = 0;
  if (K.right) tab = 1;
  if (K.b) { ui_close(); return; }
  Ent *me = world_ent(W.me);
  // Body below the tabs, with the QR on the right and the copy in the column to its left.
  float tx = IN_X + 6, tw = IN_W - 96, qx = IN_X + IN_W - 80;
  float y = IN_Y + 24;
  const float foot_y = IN_Y + IN_H - 24;   // two short lines fit above the frame
  if (tab == 0) {
    fill_round(IN_X + 2, IN_Y + 22, IN_W - 4, IN_H - 22, 4, sk_rgb(0xfff8dc));
    sk_text("Keep the hotel open", tx, y, 11.5f, 0x6b4a00, 0, tw); y += 15;
    y += sk_paragraph("A one-time $5.00 donation helps pay for the servers. Every donor gets:", tx, y, tw, 9, SK_TEXT, 3);
    static const char *perks[] = {
      "- Tweebbo Club for life (gold badge)",
      "- VIP badge on your profile",
      "- 100 credits (not 10) for hanging out",
      "- 10,000 credits on your first donation",
    };
    y += 2;
    for (int i = 0; i < 4; i++) { sk_text(perks[i], tx, y, 9, SK_TEXT, 0, tw); y += 12; }
    if (me && (me->flags & FLAG_CLUB)) sk_text("You are a Club member. Thank you!", tx, y + 1, 8.5f, SK_GO, 0, tw);
    sk_paragraph("Donate at tweebbo.lol from a PC or phone with your character code.", tx, foot_y, tw, 8.5f, SK_DIM, 2);
    qr_block(qx, IN_Y + 28);
    if (sk_button(qx, IN_Y + IN_H - 34, 80, "Show code", true)) ui_open(P_IDENTITY);
  } else {
    fill_round(IN_X + 2, IN_Y + 22, IN_W - 4, IN_H - 22, 4, sk_rgb(0xf7ecec));
    sk_text("Chips: the PC Casino", tx, y, 11.5f, 0x70202a, 0, tw); y += 15;
    y += sk_paragraph("Chips are the Casino's real-money currency. They can only be bought and played on a PC at tweebbo.lol, including the VIP Dash.", tx, y, tw, 9, SK_TEXT, 5) + 6;
    sk_paragraph("On Nintendo 3DS every Casino game, the VIP Dash included, plays with free credits.", tx, y, tw, 9, SK_TEXT, 4);
    sk_paragraph("For chips, open tweebbo.lol with your character code (18+).", tx, foot_y, tw, 8.5f, SK_DIM, 2);
    qr_block(qx, IN_Y + 28);
    if (sk_button(qx, IN_Y + IN_H - 34, 80, "Show code", true)) ui_open(P_IDENTITY);
  }
}
