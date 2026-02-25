OUT_DIR := ./out

# Common objects shared by both executables
COMMON_OBJS := $(OUT_DIR)/uic.o $(OUT_DIR)/query.o $(OUT_DIR)/ufs_bsg.o $(OUT_DIR)/common.o $(OUT_DIR)/query_trans.o

# Unique objects for each executable
LSUFS_UNIQUE_OBJS := $(OUT_DIR)/shell.o $(OUT_DIR)/lsufs.o
EOM_UNIQUE_OBJS := $(OUT_DIR)/ufs_eom.o

# Combined object lists
LSUFS_OBJS := $(LSUFS_UNIQUE_OBJS) $(COMMON_OBJS)
EOM_OBJS := $(EOM_UNIQUE_OBJS) $(COMMON_OBJS)

CC := gcc
CFLAGS := -O0 -g -I. -I./ufs-eom -D_GNU_SOURCE
LDFLAGS :=
RM := rm -f

.PHONY: clean all

all: $(OUT_DIR)/lsufs $(OUT_DIR)/ufseom

$(OUT_DIR):
	mkdir -p $@

$(OUT_DIR)/lsufs: $(LSUFS_OBJS) | $(OUT_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OUT_DIR)/ufseom: $(EOM_OBJS) | $(OUT_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OUT_DIR)/%.o: %.c | $(OUT_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT_DIR)/%.o: ufs-eom/%.c | $(OUT_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	@echo -n Cleaning...
	@$(RM) -r $(OUT_DIR)
	@echo Done
