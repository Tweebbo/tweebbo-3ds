// Support Tweebbo: what donating and chips give, and how to pay for them. The console shows
// a pay link as a QR code (paylink.c): a phone or PC opens it and pays for this character,
// and the hotel credits it; no QR is shown before a payment code is ready.
#include "ui_int.h"
#include "gfx.h"
#include "net.h"
#include "world.h"
#include "proto.h"
#include "paylink.h"
#include <stdio.h>

static int tab;

static const char *const kinds[] = {"club", "chips"};

// Only a live checkout code belongs in this panel.
static void qr_block(float x, float y, u64 now) {
  fill_round(x - 3, y - 3, 80, 80, 4, sk_rgb(SK_WHITE));
  int n = paylink_qr_size();
  if (n && paylink_state() == PAY_READY) {
    float m = (float)(74 / n), o = (74 - m * n) / 2;
    for (int qy = 0; qy < n; qy++)
      for (int qx = 0; qx < n; qx++)
        if (paylink_qr_dark(qx, qy)) fill_rect(x + o + qx * m, y + o + qy * m, m, m, sk_rgb(0x111920));
    char left[32];
    int s = paylink_seconds_left(now);
    snprintf(left, sizeof left, "Scan to pay %d:%02d", s / 60, s % 60);
    sk_text(left, x + 37, y + 82, 9, SK_TEXT, 1, 86);
    return;
  }
  sk_text("Create a code", x + 37, y + 32, 10, SK_DIM, 1, 74);
  sk_text("Use phone", x + 37, y + 82, 9, SK_TEXT, 1, 78);
}

// The button under the QR: make a pay code, or show where the last one stands.
static void pay_button(float x, float y, float w, u64 now) {
  PayState st = paylink_state();
  const char *label = st == PAY_LOADING ? "Wait..." : st == PAY_READY ? "New code" : "Pay / QR";
  if ((sk_button(x, y, w, label, st != PAY_LOADING) || K.a) && st != PAY_LOADING) paylink_open(kinds[tab], now);
}

static void pay_status(float tx, float y, float tw) {
  PayState st = paylink_state();
  if (st == PAY_PAID) sk_paragraph(paylink_message(), tx, y, tw, 8.5f, SK_GO, 2);
  else if (st == PAY_ERROR) sk_paragraph(paylink_message(), tx, y, tw, 8.5f, 0xa32020, 2);
  else if (st == PAY_EXPIRED) sk_paragraph("The code expired. Press A for a new one.", tx, y, tw, 8.5f, SK_DIM, 2);
  else if (st == PAY_READY) sk_paragraph("Scan the code with your phone and pay there. This screen updates by itself.", tx, y, tw, 8.5f, SK_DIM, 2);
}

void support_draw(u64 now) {
  paylink_poll(now);
  if (sk_window(WIN_X, WIN_Y, WIN_W, WIN_H, "Donate")) { paylink_close(); ui_close(); return; }
  sk_content(IN_X, IN_Y, IN_W, IN_H);
  if (sk_tab(IN_X + 4, IN_Y + 2, 100, 16, "Donate", tab == 0)) tab = 0;
  if (sk_tab(IN_X + 108, IN_Y + 2, 100, 16, "Chips", tab == 1)) tab = 1;
  int was = tab;
  if (K.left) tab = 0;
  if (K.right) tab = 1;
  if (tab != was) paylink_close();
  if (K.b) { paylink_close(); ui_close(); return; }
  Ent *me = world_ent(W.me);
  // Body below the tabs, with the QR on the right and the copy in the column to its left.
  float tx = IN_X + 6, tw = IN_W - 96, qx = IN_X + IN_W - 80;
  float y = IN_Y + 24;
  const float foot_y = IN_Y + IN_H - 24;   // two short lines fit above the frame
  if (tab == 0) {
    fill_round(IN_X + 2, IN_Y + 22, IN_W - 4, IN_H - 22, 4, sk_rgb(0xfff8dc));
    sk_text("Keep the hotel open", tx, y, 11.5f, 0x6b4a00, 0, tw); y += 15;
    y += sk_paragraph("Donate $5+ for these benefits:", tx, y, tw, 9, SK_TEXT, 2);
    static const char *perks[] = {
      "- Tweebbo Club for life",
      "- VIP badge on your profile",
      "- 100 credits for hanging out",
      "- 10,000 credits the first time",
    };
    y += 2;
    for (int i = 0; i < 4; i++) { sk_text(perks[i], tx, y, 9, SK_TEXT, 0, tw); y += 12; }
    if (me && (me->flags & FLAG_CLUB)) sk_text("You are a Club member. Thank you!", tx, y + 1, 8.5f, SK_GO, 0, tw);
    if (paylink_state() == PAY_IDLE) sk_paragraph("A: Pay by phone. Crypto and custom amounts available.", tx, foot_y, tw, 8.5f, SK_DIM, 2);
    else pay_status(tx, foot_y, tw);
    qr_block(qx, IN_Y + 28, now);
    pay_button(qx - 4, IN_Y + IN_H - 34, 88, now);
  } else {
    fill_round(IN_X + 2, IN_Y + 22, IN_W - 4, IN_H - 22, 4, sk_rgb(0xf7ecec));
    sk_text("Chips: the PC Casino", tx, y, 11.5f, 0x70202a, 0, tw); y += 15;
    y += sk_paragraph("Chips are the Casino's real-money currency (18+). Buy them with your phone for this character; they play at tweebbo.lol, including the VIP Dash.", tx, y, tw, 9, SK_TEXT, 5) + 6;
    sk_paragraph("On Nintendo 3DS every Casino game plays with free credits.", tx, y, tw, 9, SK_TEXT, 3);
    if (paylink_state() == PAY_IDLE) sk_paragraph("Press A for a code to buy chips (18+).", tx, foot_y, tw, 8.5f, SK_DIM, 2);
    else pay_status(tx, foot_y, tw);
    qr_block(qx, IN_Y + 28, now);
    pay_button(qx - 4, IN_Y + IN_H - 34, 88, now);
  }
}
