#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int32_t s32; typedef int64_t s64; typedef int32_t Result; typedef uint32_t Handle;
typedef struct TestThread *Thread;
#define U64_MAX UINT64_MAX
#define CUR_THREAD_HANDLE 0
#define MEDIATYPE_SD 1
#define R_SUCCEEDED(r) ((r)>=0)
#define R_FAILED(r) ((r)<0)
u64 osGetTime(void);
void svcSleepThread(s64 time);
Result svcGetThreadPriority(s32 *priority,Handle handle);
Thread threadCreate(void (*entry)(void *),void *arg,size_t stack,s32 priority,int core,bool detached);
Result threadJoin(Thread thread,u64 timeout);
void threadFree(Thread thread);
bool aptIsSleepAllowed(void);
void aptSetSleepAllowed(bool allowed);
void aptSetHomeAllowed(bool allowed);
Result amInit(void);
void amExit(void);
Result AM_StartCiaInstall(int media,Handle *handle);
Result FSFILE_Write(Handle handle,u32 *written,u64 offset,const void *data,u32 bytes,u32 flags);
Result AM_FinishCiaInstall(Handle handle);
Result AM_CancelCIAInstall(Handle handle);
Result APT_PrepareToDoApplicationJump(int flags,u64 title,int media);
void APT_DoApplicationJump(void *data,unsigned size,void *hmac);
