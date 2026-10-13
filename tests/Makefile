SRC := ../source
CFLAGS := -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-format-truncation -I$(SRC)
OBJS := $(SRC)/proto.c $(SRC)/path.c $(SRC)/sim.c $(SRC)/world.c $(SRC)/catalog.c $(SRC)/gen/assets.c

all: host_test door_test anim_test

door_test: door_test.c $(OBJS)
	cc $(CFLAGS) -o $@ $^ -lm
	./door_test

host_test: host_test.c $(OBJS)
	cc $(CFLAGS) -o $@ $^ -lm
	./host_test

# Online client against a local server (no TLS): make online SERVER=ws://127.0.0.1:8092
SERVER ?= ws://127.0.0.1:8092
NET_OBJS := $(SRC)/net.c $(SRC)/ws.c $(SRC)/conn.c $(SRC)/proto.c $(SRC)/world.c $(SRC)/catalog.c $(SRC)/gen/assets.c
online_test: online_test.c $(NET_OBJS)
	cc $(CFLAGS) -DNO_TLS -pthread -o $@ $^ -lm

online: online_test
	./online_test $(SERVER)
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
	  /tmp/online_tls $(SERVER)'
.PHONY: online-tls

# In-game update path against the GitHub release (redirects, certificates, SHA-256): make update
update:
	podman run --rm -v $(CURDIR)/..:/w:Z -w /w/tests docker.io/library/ubuntu:24.04 bash -c '\
	  apt-get -qq update >/dev/null && apt-get -qq install -y gcc libmbedtls-dev >/dev/null 2>&1 && \
	  cc -std=gnu11 -O1 -I../source -o /tmp/update_test update_test.c ../source/ws.c ../source/conn.c ../source/certs.c -lmbedtls -lmbedx509 -lmbedcrypto && \
	  for i in 1 2 3 4 5 6; do /tmp/update_test | head -2; done'
.PHONY: update
