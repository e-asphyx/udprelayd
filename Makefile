##########################################################
#CFLAGS = -DDEBUG -O0 -g
CFLAGS = -O2
CXXFLAGS := $(CFLAGS)
LDFLAGS =

SOURCES = udprelayd.c utils.c config.c relay.c seen_lookup.c
BIN = udprelayd

# SGLIB produces a lot of warnings about unused variables
udprelayd_CFLAGS = -Wall -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unknown-warning-option -std=c99 -D_XOPEN_SOURCE
udprelayd_CXXFLAGS := $(udprelayd_CFLAGS)

##########################################################

include common.mk
