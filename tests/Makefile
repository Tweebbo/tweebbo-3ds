SRC := ../source
CFLAGS := -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-format-truncation -I$(SRC)
OBJS := $(SRC)/proto.c $(SRC)/path.c $(SRC)/sim.c $(SRC)/world.c $(SRC)/catalog.c $(SRC)/gen/assets.c

all: host_test door_test anim_test protocol_test power lookup-cache

power: power_test http_idle_test

power_test: power.c $(SRC)/frame_pacing.h $(SRC)/world_activity.h $(SRC)/world.c
	cc $(CFLAGS) -ffunction-sections -fdata-sections -o $@ power.c $(SRC)/world.c -Wl,--gc-sections -lm
	./power_test

http_idle_test: http-idle.c $(SRC)/net.c $(SRC)/job_signal.h
	cc $(CFLAGS) -pthread -ffunction-sections -fdata-sections -o $@ http-idle.c -Wl,--gc-sections -lm
	./http_idle_test
.PHONY: power

lookup-cache: lookup-cache.c $(SRC)/world.c $(SRC)/lookup_cache.h
	cc $(CFLAGS) -O2 -ffunction-sections -fdata-sections -o lookup-cache-test lookup-cache.c $(SRC)/world.c -Wl,--gc-sections -lm
	./lookup-cache-test
.PHONY: lookup-cache

download_test: download_test.c $(SRC)/download.c $(SRC)/ws.c $(SRC)/conn.c
	cc $(CFLAGS) -DNO_TLS -pthread -o $@ $^
downloads: download_test
	node download-fixture.mjs
.PHONY: downloads

QJS := ../vendor/quickjs
snow_host: snow_host.c $(QJS)/quickjs.c $(QJS)/libregexp.c $(QJS)/libunicode.c $(QJS)/cutils.c $(QJS)/dtoa.c
	cc -std=gnu11 -O2 -D_GNU_SOURCE -DCONFIG_VERSION='"2026-06-04"' -I$(QJS) -o $@ $^ -lm -lpthread
snow: snow_host
	node snow-determinism.mjs
	./snow_host ../romfs/snow/game.js snow-bridge.js
.PHONY: snow
games: snow_host
	node fastfood-determinism.mjs
	node battle-render.mjs
.PHONY: games

door_test: door_test.c $(OBJS)
	cc $(CFLAGS) -o $@ $^ -lm
	./door_test

host_test: host_test.c $(OBJS)
	cc $(CFLAGS) -o $@ $^ -lm
	./host_test

# Online client against a local server (no TLS): make online SERVER=ws://127.0.0.1:8092
SERVER ?= ws://127.0.0.1:8092
WEB_BUILD ?= $(shell sed -n 's/^pub const WEB_CLIENT_BUILD: u8 = \([0-9][0-9]*\);.*/\1/p' ../../Tweebbo-Vercel/server/src/proto.rs)
NET_OBJS := $(SRC)/net.c $(SRC)/ws.c $(SRC)/conn.c $(SRC)/proto.c $(SRC)/world.c $(SRC)/catalog.c $(SRC)/gen/assets.c
online_test: online_test.c $(NET_OBJS)
	cc $(CFLAGS) -DNO_TLS -DTWEEBBO_DEBUG -pthread -o $@ $^ -lm

online: online_test
	TWEEBBO_WEB_BUILD=$(WEB_BUILD) ./online_test $(SERVER)
.PHONY: online

# Real TLS (mbedTLS 2.28 like devkitPro) inside ubuntu:24.04: make tls
tls:
	podman run --rm -v $(CURDIR)/..:/w:Z -w /w/tests docker.io/library/ubuntu:24.04 bash -c '\
	  apt-get -qq update >/dev/null && apt-get -qq install -y gcc libmbedtls-dev >/dev/null 2>&1 && \
	  cc -std=gnu11 -O1 -I../source -o /tmp/tls_test tls_test.c ../source/ws.c ../source/conn.c ../source/certs.c -lmbedtls -lmbedx509 -lmbedcrypto && \
	  /tmp/tls_test https://tweebbo.lol'
.PHONY: tls

# Avatar animations (real compositor, stubbed gfx)
anim_test: anim_test.c $(SRC)/avatar.c $(SRC)/gen/assets.c
	cc $(CFLAGS) -Istubs -include stdbool.h -o $@ anim_test.c $(SRC)/avatar.c $(SRC)/gen/assets.c -lm
	./anim_test

# The same online test over real TLS (wss://) inside ubuntu:24.04: make online-tls SERVER=wss://tweebbo.lol
online-tls:
	podman run --rm -v $(CURDIR)/..:/w:Z -w /w/tests docker.io/library/ubuntu:24.04 bash -c '\
	  apt-get -qq update >/dev/null && apt-get -qq install -y gcc libmbedtls-dev >/dev/null 2>&1 && \
	  cc -std=gnu11 -O1 -I../source -pthread -o /tmp/online_tls online_test.c ../source/net.c ../source/ws.c ../source/conn.c ../source/certs.c ../source/proto.c ../source/world.c ../source/catalog.c ../source/gen/assets.c -lmbedtls -lmbedx509 -lmbedcrypto -lm && \
	  TWEEBBO_WEB_BUILD=$(WEB_BUILD) /tmp/online_tls $(SERVER)'
.PHONY: online-tls

# In-game update path against the GitHub release (redirects, certificates, SHA-256): make update
update:
	podman run --rm -v $(CURDIR)/..:/w:Z -w /w/tests docker.io/library/ubuntu:24.04 bash -c '\
	  apt-get -qq update >/dev/null && apt-get -qq install -y gcc libmbedtls-dev >/dev/null 2>&1 && \
	  cc -std=gnu11 -O1 -I../source -o /tmp/update_test update_test.c ../source/download.c ../source/ws.c ../source/conn.c ../source/certs.c -lmbedtls -lmbedx509 -lmbedcrypto -lpthread && \
	  /tmp/update_test'
.PHONY: update

protocol_test: protocol_test.c $(SRC)/world.c $(SRC)/proto.c $(SRC)/catalog.c $(SRC)/gen/assets.c
	cc $(CFLAGS) -o $@ $^ -lm
	./protocol_test

transport_test: transport_test.c $(NET_OBJS)
	cc $(CFLAGS) -DNO_TLS -DTWEEBBO_DEBUG -pthread -o $@ $^ -lm

transport: transport_test
	node transport-fixture.mjs
.PHONY: transport

room-recovery: room-recovery.c transport_test.c $(NET_OBJS)
	cc $(CFLAGS) -DNO_TLS -DTWEEBBO_DEBUG -pthread -o $@ room-recovery.c $(NET_OBJS) -lm

recovery: room-recovery
	node room-recovery.mjs
.PHONY: recovery

input-latency: input-latency.c $(NET_OBJS)
	cc $(CFLAGS) -DNO_TLS -DTWEEBBO_DEBUG -pthread -o $@ $^ -lm
