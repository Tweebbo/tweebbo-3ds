#pragma once
#include <stdint.h>
enum { LOOKUP_CACHE_BUCKETS=512 };
typedef struct {uint32_t key;uint16_t slot;} LookupEntry;
typedef struct {LookupEntry entries[LOOKUP_CACHE_BUCKETS];} LookupCache;
static inline unsigned lookup_bucket(uint32_t key){key^=key>>16;key*=0x7feb352du;key^=key>>15;return key&(LOOKUP_CACHE_BUCKETS-1);}
/* Callers validate the cached slot against its current owner before using it. */
static inline int lookup_cache_get(const LookupCache *cache,uint32_t key){const LookupEntry *entry=&cache->entries[lookup_bucket(key)];return entry->slot&&entry->key==key?(int)entry->slot-1:-1;}
static inline void lookup_cache_put(LookupCache *cache,uint32_t key,unsigned slot){cache->entries[lookup_bucket(key)]=(LookupEntry){key,(uint16_t)(slot+1)};}
