CC = gcc
CXX = g++
CFLAGS = -Wall -Wextra -Iinclude
CXXFLAGS = -Wall -Wextra -Iinclude -std=c++20

TERMOX_DIR = libs/termox
TERMOX_DEPS = \
	$(TERMOX_DIR)/build/_deps/escape-src/include \
	$(TERMOX_DIR)/build/_deps/signals-light-src/include \
	$(TERMOX_DIR)/build/_deps/zzz-src/include

# Include & link TermOx
CFLAGS   += -I$(TERMOX_DIR)/include $(addprefix -I,$(TERMOX_DEPS))
CXXFLAGS += -I$(TERMOX_DIR)/include $(addprefix -I,$(TERMOX_DEPS))

LDLIBS += -L$(TERMOX_DIR)/build -ltermox -L$(TERMOX_DIR)/build/_deps/escape-build -lescape -L/opt/homebrew/opt/icu4c/lib -licui18n -licuuc -licudata -lncurses -lpthread

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

# List source files
C_SRCS = $(SRC_DIR)/main.c \
	$(SRC_DIR)/networking/server.c \
	$(SRC_DIR)/networking/client.c \
	$(SRC_DIR)/networking/network.c \
	$(SRC_DIR)/client/main.c \
	$(SRC_DIR)/server/main.c

CPP_SRCS = $(SRC_DIR)/ui/launcher.cpp

SRCS = $(C_SRCS) $(CPP_SRCS)

# Generate object file names
C_OBJS = $(C_SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
CPP_OBJS = $(CPP_SRCS:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)
OBJS = $(C_OBJS) $(CPP_OBJS)

# Output binary
TARGET = $(BIN_DIR)/armada

# Main target
all: termox $(TARGET)

# Build TermOx first
termox:
	@cd $(TERMOX_DIR) && cmake -B build > /dev/null 2>&1 || true
	@cd $(TERMOX_DIR) && cmake --build build > /dev/null 2>&1
	@echo "TermOx ready."

# Link
$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(OBJS) -o $@ $(LDLIBS)

# Compile C files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile C++ files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

.PHONY: all clean termox