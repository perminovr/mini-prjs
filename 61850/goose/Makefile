SHELL = /bin/bash

TM := $(shell date +'%Y-%m-%d-%H-%M-%S')
PWD != pwd
BUILD_DIR := $(PWD)/Build
BUILD_WD := $(BUILD_DIR)/usr
BUILD_LOG := $(BUILD_DIR)/logs
BUILD_DWLD := $(BUILD_DIR)/download
BUILD_LIB := $(BUILD_WD)/lib
BUILD_INC := $(BUILD_WD)/include
BUILD_BIN := $(BUILD_WD)/bin
SYSROOT_WD := $(PWD)/usr
SYSROOT_LOG := $(PWD)/logs

include Targets.mk

ifdef CSO # YYY
CSO_NAMES := $(addprefix $(BUILD_LIB)/, $(patsubst %,lib%.so,$(CSO)))			# PWD/usr/lib/libYYY.so
PRE_CSO_MkFiles := $(addprefix $(BUILD_DIR)/,$(addsuffix /MakefileCSO.mk, $(CSO)))	# PWD/YYY/MakefileCSO.mk
CSO_MkFiles := $(wildcard $(BUILD_DIR)/*/MakefileCSO.mk)							# PWD/YYY/MakefileCSO.mk
CSO_CLEAN := $(addprefix soclean-,$(CSO))										# soclean-YYY
endif

ifdef CBIN # YYY
CBIN_NAMES := $(addprefix $(BUILD_BIN)/, $(CBIN))								# PWD/usr/bin/YYY
PRE_CBIN_MkFiles := $(addprefix $(BUILD_DIR)/,$(addsuffix /MakefileCBIN.mk, $(CBIN)))	# PWD/YYY/MakefileCBIN.mk
CBIN_MkFiles := $(wildcard $(BUILD_DIR)/*/MakefileCBIN.mk)								# PWD/YYY/MakefileCBIN.mk
CBIN_CLEAN := $(addprefix cclean-,$(CBIN))										# cclean-YYY
endif

ifdef CPPSO # YYY
CPPSO_NAMES := $(addprefix $(BUILD_LIB)/, $(patsubst %,lib%.so,$(CPPSO)))			# PWD/usr/lib/libYYY.so
PRE_CPPSO_MkFiles := $(addprefix $(BUILD_DIR)/,$(addsuffix /MakefileCPPSO.mk, $(CPPSO)))	# PWD/YYY/MakefileCPPSO.mk
CPPSO_MkFiles := $(wildcard $(BUILD_DIR)/*/MakefileCPPSO.mk)								# PWD/YYY/MakefileCPPSO.mk
CPPSO_CLEAN := $(addprefix soclean-,$(CPPSO))										# soclean-YYY
endif

ifdef CPPBIN # YYY
CPPBIN_NAMES := $(addprefix $(BUILD_BIN)/, $(CPPBIN))									# PWD/usr/bin/YYY
PRE_CPPBIN_MkFiles := $(addprefix $(BUILD_DIR)/,$(addsuffix /MakefileCPPBIN.mk, $(CPPBIN))) # PWD/YYY/MakefileCPPBIN.mk
CPPBIN_MkFiles := $(wildcard $(BUILD_DIR)/*/MakefileCPPBIN.mk)								# PWD/YYY/MakefileCPPBIN.mk
CPPBIN_CLEAN := $(addprefix cclean-,$(CPPBIN))											# cclean-YYY
endif


include $(BUILD_DIR)/MakeFile-cso.mk
include $(BUILD_DIR)/MakeFile-cbin.mk
include $(BUILD_DIR)/MakeFile-cppso.mk
include $(BUILD_DIR)/MakeFile-cppbin.mk

x86:
	PLATFORM=TRG_X86 $(MAKE) prebuild
	. $(BUILD_DIR)/environment-setup-x86_64.sh ; PLATFORM=TRG_X86 $(MAKE) all

all: $(CSO_NAMES) $(CBIN_NAMES) $(CPPSO_NAMES) $(CPPBIN_NAMES)
	@echo "All ok"

.PHONY: clean cleanall prebuild

prebuild: $(SYSROOT_LOG) $(SYSROOT_WD) $(BUILD_LOG) $(BUILD_INC) $(BUILD_LIB) $(PRE_CSO_MkFiles) $(PRE_CBIN_MkFiles) $(PRE_CPPSO_MkFiles) $(PRE_CPPBIN_MkFiles)
	@echo "Prebuild ok"

$(BUILD_INC):
	@mkdir -p $(BUILD_INC)

$(BUILD_LOG):
	@mkdir -p $(BUILD_LOG)

$(BUILD_LIB):
	@mkdir -p $(BUILD_LIB)

$(BUILD_BIN):
	@mkdir -p $(BUILD_BIN)

$(SYSROOT_LOG):
	@mkdir -p $(SYSROOT_LOG)

$(SYSROOT_WD):
	@ln -sf $(BUILD_WD) $(SYSROOT_WD)

clean: $(CSO_CLEAN) $(CBIN_CLEAN) $(CPPBIN_CLEAN) $(CPPSO_CLEAN)
	rm -rf $(BUILD_WD) $(BUILD_LOG)
	rm -rf $(SYSROOT_WD) $(SYSROOT_LOG)

cleanall: clean
	rm -rf $(PRE_CSO_MkFiles) $(PRE_CBIN_MkFiles) $(PRE_CPPSO_MkFiles) $(PRE_CPPBIN_MkFiles) $(BUILD_DWLD)

include $(CSO_MkFiles)
include $(CBIN_MkFiles)
include $(CPPSO_MkFiles)
include $(CPPBIN_MkFiles)
