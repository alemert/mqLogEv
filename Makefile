################################################################################
# makefile for test util
################################################################################

MAKE_INCLUDE_PATH=mk.inc

# ------------------------------------------------------------------------------
# Compiler and BIT
# ------------------------------------------------------------------------------
CC=gcc
BIT = 64

# ------------------------------------------------------------------------------
# debugger
# ------------------------------------------------------------------------------
DBGOPT = -g

# ------------------------------------------------------------------------------
# sources
# ------------------------------------------------------------------------------
SOURCES = mqLogEv.c

#AROWN = libmsgcat.a
LSOWN = mqutil msgcat sogen

MQINST = /opt/mqm/75a
#XML2  = use

# ------------------------------------------------------------------------------
# main source
# ------------------------------------------------------------------------------
MAIN = main.c

# ------------------------------------------------------------------------------
# BINARY
# ------------------------------------------------------------------------------
BINARY = mqLogEv

# ------------------------------------------------------------------------------
# libraries dynamic & static
# ------------------------------------------------------------------------------
LIBRARY = 

ARCHIVE  = 

# ------------------------------------------------------------------------------
# rollout includes
# ------------------------------------------------------------------------------
ROLLOUT_INC = 

# ------------------------------------------------------------------------------
# general includes
# ------------------------------------------------------------------------------
include $(MAKE_INCLUDE_PATH)/general.modules.mk

# ------------------------------------------------------------------------------
# clean local
# ------------------------------------------------------------------------------
cleanlocal :
	$(RM) var/log/*.log
	$(RM) core.*

# ------------------------------------------------------------------------------
# tests
# ------------------------------------------------------------------------------
#TEST = t_file_000 t_string_000 t_fork_000
include $(MAKE_INCLUDE_PATH)/test.mk

