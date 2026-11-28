CC      = gcc
CFLAGS  = -Wall -Wextra -g -Icommon
# 서버에 ncurses 붙이면 SERVER_LIBS에 -lncurses 추가

COMMON_SRC = $(wildcard common/*.c)
CLIENT_SRC = $(wildcard client/*.c) $(COMMON_SRC)
SERVER_SRC = $(wildcard server/*.c) $(wildcard server/analyzer/*.c) \
             $(wildcard server/store/*.c) $(COMMON_SRC)

CLIENT_LIBS = -lutil
SERVER_LIBS = -lpthread

# build_err.c는 의도적으로 컴파일에 실패하므로 실행 파일 목록에서 제외한다.
TEST_SRC = $(filter-out tests/build_err.c,$(wildcard tests/*.c))
TEST_BIN = $(TEST_SRC:.c=)

all: crashdoc crashdoc-server

crashdoc: $(CLIENT_SRC) $(wildcard common/*.h) $(wildcard client/*.h)
	$(CC) $(CFLAGS) -o $@ $(CLIENT_SRC) $(CLIENT_LIBS)

crashdoc-server: $(SERVER_SRC) $(wildcard common/*.h) $(wildcard server/*.h) \
                 $(wildcard server/analyzer/*.h) $(wildcard server/store/*.h)
	$(CC) $(CFLAGS) -Iserver/analyzer -Iserver/store -o $@ $(SERVER_SRC) $(SERVER_LIBS)

# 테스트 프로그램은 일부러 버그가 있으니 경고 옵션 없이 -g로만
tests: $(TEST_BIN)
# 분석기 테스트는 analyzer 소스와 전용 include 경로가 필요하다.
tests/test_analyzer: tests/test_analyzer.c server/analyzer/analyzer.c \
                     server/analyzer/llm.c server/analyzer/rule.c \
                     server/analyzer/analyzer.h server/analyzer/analyzer_internal.h
	$(CC) $(CFLAGS) -Iserver/analyzer -o $@ tests/test_analyzer.c \
	    server/analyzer/analyzer.c server/analyzer/llm.c server/analyzer/rule.c

tests/%: tests/%.c
	$(CC) -g -o $@ $<

clean:
	rm -f crashdoc crashdoc-server $(TEST_BIN)

.PHONY: all tests clean
