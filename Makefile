CC      = gcc
CFLAGS  = -Wall -Wextra -g -Icommon
# 서버에 ncurses 붙이면 SERVER_LIBS에 -lncurses 추가

COMMON_SRC = $(wildcard common/*.c)
CLIENT_SRC = $(wildcard client/*.c) $(COMMON_SRC)
SERVER_SRC = $(wildcard server/*.c) $(wildcard server/analyzer/*.c) \
             $(wildcard server/store/*.c) $(COMMON_SRC)

CLIENT_LIBS = -lutil
SERVER_LIBS = -lpthread

TEST_SRC = $(wildcard tests/*.c)
TEST_BIN = $(TEST_SRC:.c=)

all: crashdoc crashdoc-server

crashdoc: $(CLIENT_SRC) $(wildcard common/*.h) $(wildcard client/*.h)
	$(CC) $(CFLAGS) -o $@ $(CLIENT_SRC) $(CLIENT_LIBS)

crashdoc-server: $(SERVER_SRC) $(wildcard common/*.h) $(wildcard server/*.h) \
                 $(wildcard server/analyzer/*.h) $(wildcard server/store/*.h)
	$(CC) $(CFLAGS) -Iserver/analyzer -Iserver/store -o $@ $(SERVER_SRC) $(SERVER_LIBS)

# 테스트 프로그램은 일부러 버그가 있으니 경고 옵션 없이 -g로만
tests: $(TEST_BIN)
tests/%: tests/%.c
	$(CC) -g -o $@ $<

clean:
	rm -f crashdoc crashdoc-server $(TEST_BIN)

.PHONY: all tests clean
