#
# FreeModbus Linux PORT - Makefile
#
# Copyright (c) 2006 Christian Walter, ? s::can, Vienna 2006.
#
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# platform dependencies
# ---------------------------------------------------------------------------
# 平台检测用 make 内建函数，不依赖 grep（Windows cmd 环境无 grep）
ifneq ($(findstring cygwin,$(shell gcc -v 2>&1)),)
	CYGWIN_BUILD = YES
endif
CC	    = gcc
CXX	    = g++
OBJCOPY	    = objcopy
INSIGHT     = /opt/insight-x86/bin/insight

# ---------------------------------------------------------------------------
# project specifics
# ---------------------------------------------------------------------------
BUILD_DIR   = out
# -fexec-charset=GBK: 源码已转UTF-8，但Windows控制台默认GBK代码页，
# 字符串字面量需按GBK字节输出中文才不乱码
CFLAGS	    = -MD -g3 -Wall -fexec-charset=GBK
LDFLAGS     =
ifeq ($(CYGWIN_BUILD),YES)
else
#LDFLAGS     += -lpthread
endif

TGT         = dlt645_cli.exe
OTHER_CSRC  = 
OTHER_ASRC  = 
CSRC        = src/dlt645_api_07.c src/main.c
ASRC        = 
OBJS        = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(CSRC)) $(ASRC:.S=.o)
NOLINK_OBJS = $(OTHER_CSRC:.c=.o) $(OTHER_ASRC:.S=.o)
DEPS        = $(OBJS:.o=.d) $(NOLINK_OBJS:.o=.d)
BIN         = $(BUILD_DIR)/$(TGT)

.PHONY: clean all

all: $(BIN)

debug:
	$(INSIGHT) --se=$(TGT)

$(BIN): $(OBJS) $(NOLINK_OBJS) | $(BUILD_DIR)
	$(CC) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $@

$(BUILD_DIR):
	-mkdir $(BUILD_DIR)

clean:
	-rm -f $(DEPS) $(OBJS) $(NOLINK_OBJS) $(BIN)
	-del /q $(BUILD_DIR)\*.o $(BUILD_DIR)\*.d $(BUILD_DIR)\$(TGT)

# ---------------------------------------------------------------------------
# rules for code generation
# ---------------------------------------------------------------------------
$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ -c $<

%.o:    %.S
	$(CC) $(ASFLAGS) -o $@ -c $<

# ---------------------------------------------------------------------------
#  # compiler generated dependencies
# ---------------------------------------------------------------------------
-include $(DEPS)

