# Project Name
TARGET = MiniSynth

# Sources
CPP_SOURCES = MiniSynth.cpp

# Library Locations
LIBDAISY_DIR = ../../libDaisy/
DAISYSP_DIR = ../../DaisySP/

# OLED + Dev tools
INCLUDES = -I$(LIBDAISY_DIR)/src -I$(DAISYSP_DIR)/Source

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
