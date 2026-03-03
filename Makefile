RACK_DIR ?= ../..
#RACK_DIR=~/Rack-SDK-2.0.0

FLAGS += 
CFLAGS +=
CXXFLAGS +=

SOURCES += src/FMtool.cpp

DISTRIBUTABLES += res

include $(RACK_DIR)/plugin.mk
