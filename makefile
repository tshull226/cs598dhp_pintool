##############################################################
#
#                   DO NOT EDIT THIS FILE!
#
##############################################################

# If the tool is built out of the kit, PIN_ROOT must be specified in the make invocation and point to the kit root.
PIN_ROOT =/Users/tshull7/UIUC/research/pin-2.13-65163-clang.5.0-mac
#PIN_ROOT = /home/tshull226/Documents/research/pin-2.14-71313-gcc.4.4.7-linux
CONFIG_ROOT := $(PIN_ROOT)/source/tools/Config
include $(CONFIG_ROOT)/makefile.config
include makefile.rules
include $(TOOLS_ROOT)/Config/makefile.default.rules

##############################################################
#
#                   DO NOT EDIT THIS FILE!
#
##############################################################
