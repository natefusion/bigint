# https://github.com/clemedon/makefile_tutor
# https://makefiletutorial.com

DEBUG := ./build/debug
RELEASE := ./build/release

mode ?= debug
OBJ_DIR ?= $(DEBUG)
SRC_DIR := src

CC := gcc
SRCS := src/main.c src/randq.c

CFLAGS := -Wall -Werror -Wextra -pedantic -Werror=format-security -Wshadow -Wpointer-arith -Wcast-qual -Wcast-align -Wconversion -fno-omit-frame-pointer -pipe
CPPFLAGS := -MMD -MP

ifeq ($(mode), debug)
	CFLAGS += -g -O0
else
	ifeq ($(mode), release)
		CFLAGS += -O2
		OBJ_DIR = $(RELEASE)
	endif
endif

OBJS := $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

NAME := $(notdir $(CURDIR))
EXECUTABLE := $(OBJ_DIR)/$(NAME)

DIR_DUP = mkdir -p $(@D)

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJS)
	$(CC) $^ -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(DIR_DUP)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

-include $(DEPS)

.PHONY: clean install uninstall run test

clean:
	rm -f $(EXECUTABLE) $(OBJS) $(DEPS)

install:
	cp $(RELEASE)/$(NAME) ~/.local/bin/

uninstall:
	rm -f ~/.local/bin/$(NAME)

run:
	-$(EXECUTABLE)
