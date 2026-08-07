# Builds the real direction-finding engine for the host and checks every
# threshold, boundary and degenerate input. The engine's reading of the RSSI
# stream IS the product -- none of it is visible in a screenshot -- so it is
# verified on every push. See host_df_test.c.
#
#   make -C test

CC      ?= cc
CFLAGS  := -std=gnu2x -Wall -Wextra -Werror -O1 -g
CFLAGS  += -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS  += -I ..

SRC := host_df_test.c ../helpers/vul_df.c
BIN := host_df_test

.PHONY: all run clean
all: run

$(BIN): $(SRC) ../helpers/vul_df.h
	$(CC) $(CFLAGS) $(SRC) -o $@

run: $(BIN)
	./$(BIN)

clean:
	rm -rf $(BIN) $(BIN).dSYM
