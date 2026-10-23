#pragma once
#include "common.h"
typedef struct {
  bool busy, waiting, enabled;
  int count;
  u32 guest;
  char name[164], status[200];
} DoorbellState;
extern DoorbellState doorbell;
void doorbell_poll(void);
void doorbell_ring(void);
void doorbell_answer(bool allow);
void doorbell_set(bool enabled);
