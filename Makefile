# Makefile fuer rle8 / rle16
#
# Ziele:
#   make            Bibliothek bauen (build/librle.a)
#   make test       Testprogramm bauen (build/test_rle)
#   make check      Testprogramm bauen und ausfuehren
#   make sanitize   Tests mit AddressSanitizer/UBSan (build/san/)
#   make clean      Build-Verzeichnis entfernen (nur build/, Quellen bleiben)
#   make help       diese Uebersicht
#
# Variablen (auf der Kommandozeile ueberschreibbar):
#   CC=arm-none-eabi-gcc   anderer Compiler
#   OPT=-Os                Optimierung (Standard: -O2)
#   WERROR=1               Warnungen als Fehler
#   BUILD=out              anderes Build-Verzeichnis
#   CFLAGS=... LDFLAGS=... zusaetzliche Flags (z. B. -mcpu=cortex-m4)

CC      ?= cc
AR      ?= ar
OPT     ?= -O2
BUILD   ?= build

# Interne Flags; CFLAGS/LDFLAGS bleiben fuer zusaetzliche Angaben des Nutzers frei.
# -ffunction-sections/-fdata-sections: ungenutzte Teile per --gc-sections entfernbar
# -MMD -MP: Abhaengigkeiten automatisch, inkl. rle_core.inc und test_core.inc
ALL_CFLAGS = -std=c99 -Wall -Wextra -pedantic $(OPT) \
             -ffunction-sections -fdata-sections -MMD -MP
ifeq ($(WERROR),1)
ALL_CFLAGS += -Werror
endif
ALL_CFLAGS += $(CFLAGS)

LIB_SRC  = rle_common.c rle8.c rle16.c
TEST_SRC = test_rle8.c test_rle16.c test_main.c

LIB_OBJ  = $(addprefix $(BUILD)/,$(LIB_SRC:.c=.o))
TEST_OBJ = $(addprefix $(BUILD)/,$(TEST_SRC:.c=.o))
LIB      = $(BUILD)/librle.a
TEST_BIN = $(BUILD)/test_rle

SAN_FLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer

.PHONY: all test check sanitize clean help

all: $(LIB)

test: $(TEST_BIN)

check: $(TEST_BIN)
	./$(TEST_BIN)

sanitize:
	$(MAKE) BUILD=$(BUILD)/san OPT="-O1 -g" \
	        CFLAGS="$(SAN_FLAGS)" LDFLAGS="$(SAN_FLAGS)" check

$(LIB): $(LIB_OBJ)
	$(AR) rcs $@ $^

$(TEST_BIN): $(TEST_OBJ) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $(TEST_OBJ) $(LIB)

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

help:
	@sed -n '3,/^$$/p' $(firstword $(MAKEFILE_LIST))

-include $(LIB_OBJ:.o=.d) $(TEST_OBJ:.o=.d)
