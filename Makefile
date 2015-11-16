##########################################################
CFLAGS = -DDEBUG -O0 -g
CXXFLAGS := $(CFLAGS)
LDFLAGS =

SOURCES = udprelayd.c utils.c config.c relay.c seen_lookup.c
BIN = udprelayd

udprelayd_CFLAGS = -Wall -Wno-unused-variable -std=c99 -D_XOPEN_SOURCE
udprelayd_CXXFLAGS := $(udprelayd_CFLAGS)

##########################################################

include common.mk
