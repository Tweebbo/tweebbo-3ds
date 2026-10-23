#pragma once
#include "common.h"

typedef struct {
  char subject[401], message[8001], status[160];
  int category;
  bool busy;
} FeedbackDraft;
extern FeedbackDraft feedback;
void feedback_open(void);
void feedback_poll(void);
void feedback_send(void);
bool feedback_receptionist(u32 guest, const char *model);
