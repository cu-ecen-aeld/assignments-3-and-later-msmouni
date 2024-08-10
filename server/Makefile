# Variables
CC := gcc
CFLAGS := -Wall -Wextra -Werror
CROSS_COMPILE ?= 
TARGET := aesdsocket
SRCS := aesdsocket.c
OBJS := $(SRCS:.c=.o)

# Detect if cross-compiling
ifdef CROSS_COMPILE
CC := $(CROSS_COMPILE)gcc
endif

# Default target
all: $(TARGET)

# Build the target application
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Compile source files into object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean target to remove generated files
clean:
	rm -f $(TARGET) $(OBJS)

# Phony targets to avoid conflicts with file names
.PHONY: all clean
