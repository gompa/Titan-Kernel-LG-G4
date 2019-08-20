#Android makefile to build kernel as a part of Android Build
PERL		= $$(/usr/bin/env perl)

# Absolute path - needed for GCC/clang non-AOSP build-system make invocations
KERNEL_SRC_ABS := $(PWD)/$(call my-dir)

KERNEL_TARGET := $(strip $(INSTALLED_KERNEL_TARGET))
ifeq ($(KERNEL_TARGET),)
INSTALLED_KERNEL_TARGET := $(PRODUCT_OUT)/kernel
endif

TARGET_KERNEL_ARCH := $(strip $(TARGET_KERNEL_ARCH))
ifeq ($(TARGET_KERNEL_ARCH),)
KERNEL_ARCH := $(TARGET_ARCH)
else
KERNEL_ARCH := $(TARGET_KERNEL_ARCH)
endif

TARGET_KERNEL_HEADER_ARCH := $(strip $(TARGET_KERNEL_HEADER_ARCH))
ifeq ($(TARGET_KERNEL_HEADER_ARCH),)
KERNEL_HEADER_ARCH := $(KERNEL_ARCH)
else
$(warning Forcing kernel header generation only for '$(TARGET_KERNEL_HEADER_ARCH)')
KERNEL_HEADER_ARCH := $(TARGET_KERNEL_HEADER_ARCH)
endif

KERNEL_HEADER_DEFCONFIG := $(strip $(KERNEL_HEADER_DEFCONFIG))
ifeq ($(KERNEL_HEADER_DEFCONFIG),)
KERNEL_HEADER_DEFCONFIG := $(KERNEL_DEFCONFIG)
endif

# Force 32-bit binder IPC for 64bit kernel with 32bit userspace
ifeq ($(KERNEL_ARCH),arm64)
ifeq ($(TARGET_ARCH),arm)
KERNEL_CONFIG_OVERRIDE := CONFIG_ANDROID_BINDER_IPC_32BIT=y
endif
endif

# Target architecture cross compile
TARGET_KERNEL_CROSS_COMPILE_PREFIX := $(strip $(TARGET_KERNEL_CROSS_COMPILE_PREFIX))
KERNEL_TOOLCHAIN_PREFIX ?= $(TARGET_KERNEL_CROSS_COMPILE_PREFIX)

# Kernel toolchain - Use for binutils via $(CROSS_COMPILE)ar, $(CROSS_COMPILE)ld etc.
ifeq ($(KERNEL_TOOLCHAIN),)
    KERNEL_TOOLCHAIN_PATH := $(KERNEL_TOOLCHAIN_PREFIX)
else
    ifneq ($(KERNEL_TOOLCHAIN_PREFIX),)
        KERNEL_TOOLCHAIN_PATH := $(KERNEL_TOOLCHAIN)/$(KERNEL_TOOLCHAIN_PREFIX)
    endif
endif

ifneq ($(USE_CCACHE),)
  ifeq ($(KERNEL_ANDROID_Q_OR_HIGHER),true)
    # Prebuilt ccache is no longer shipped with Android since Q
    ccache := /usr/bin/ccache
  else
    ccache := $(PWD)/prebuilts/misc/$(HOST_PREBUILT_TAG)/ccache/ccache
  endif
  # Check that the executable is here.
  ccache := $(strip $(wildcard $(ccache)))
endif

# /usr/bin/perl is more reliable than /bin/perl
KERNEL_PERL := $$(/usr/bin/env perl)
# TODO: Check whether the installed perl works early?
#       PERL=$$(test -f /usr/bin/perl && echo /usr/bin/perl || (echo 'Perl not found, fix this!' && exit 1)) \

KERNEL_CROSS_COMPILE :=
ifeq ($(TARGET_KERNEL_USE_CLANG),true)
  KERNEL_CROSS_COMPILE += CC="$(CLANG_CC)"
  KERNEL_CROSS_COMPILE += CLANG_TRIPLE="aarch64-linux-gnu"
endif
KERNEL_CROSS_COMPILE += HOSTCC="$(KERNEL_HOSTCC)"
KERNEL_CROSS_COMPILE += HOSTAR="$(KERNEL_HOSTAR)"
KERNEL_CROSS_COMPILE += HOSTLD="$(KERNEL_HOSTLD)"
KERNEL_CROSS_COMPILE += HOSTCXX="$(KERNEL_HOSTCXX)"
KERNEL_CROSS_COMPILE += PERL=$(KERNEL_PERL)
ifneq ($(USE_CCACHE),)
  KERNEL_CROSS_COMPILE += CROSS_COMPILE="$(ccache) $(KERNEL_TOOLCHAIN_PATH)"
  KERNEL_CROSS_COMPILE += CROSS_COMPILE_ARM32="$(ccache) $(KERNEL_TOOLCHAIN_32BITS_PATH)"
else
  KERNEL_CROSS_COMPILE += CROSS_COMPILE="$(KERNEL_TOOLCHAIN_PATH)"
  KERNEL_CROSS_COMPILE += CROSS_COMPILE_ARM32="$(KERNEL_TOOLCHAIN_32BITS_PATH)"
endif

# Standard $(MAKE) evaluates to:
# prebuilts/build-tools/linux-x86/bin/ckati --color_warnings --kati_stats MAKECMDGOALS=
# which is forbidden by Android Q's new "path_interposer" tool
KERNEL_PREBUILT_MAKE := $(PWD)/prebuilts/build-tools/linux-x86/bin/make
# clang/GCC (glibc) host toolchain needs to be prepended to $PATH for certain
# host bootstrap tools to be built. Also, binutils such as `ld` and `ar` are
# needed for now.
KERNEL_MAKE_EXTRA_PATH := $(KERNEL_HOST_TOOLCHAIN)
ifneq ($(TARGET_KERNEL_USE_CLANG),true)
  KERNEL_MAKE_EXTRA_PATH := "$(KERNEL_HOST_TOOLCHAIN):$(KERNEL_HOST_TOOLCHAIN_LIBEXEC)"
endif
KERNEL_MAKE := \
    PATH="$(KERNEL_MAKE_EXTRA_PATH):$$PATH" \
    $(KERNEL_PREBUILT_MAKE)



ifeq ($(TARGET_PREBUILT_KERNEL),)

KERNEL_GCC_NOANDROID_CHK := $(shell (echo "int main() {return 0;}" | $(KERNEL_CROSS_COMPILE)gcc -E -mno-android - > /dev/null 2>&1 ; echo $$?))
ifeq ($(strip $(KERNEL_GCC_NOANDROID_CHK)),0)
KERNEL_CFLAGS := KCFLAGS=-mno-android
endif

# Absolute path - needed for GCC/clang non-AOSP build-system make invocations
KERNEL_OUT_ABS := $(PWD)/$(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ
KERNEL_OUT := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ
KERNEL_CONFIG := $(KERNEL_OUT)/.config

ifeq ($(call math_gt_or_eq,$(PLATFORM_SDK_VERSION),29),true)
  KERNEL_ANDROID_Q_OR_HIGHER := true
else
  # TODO: REMOVEME: Current(2019-08) master branch still reports PLATFORM_SDK_VERSION=28
  ifeq ($(PLATFORM_VERSION),R)
    KERNEL_ANDROID_Q_OR_HIGHER := true
  else
    KERNEL_ANDROID_Q_OR_HIGHER := false
  endif
endif

ifeq ($(KERNEL_DEFCONFIG)$(wildcard $(KERNEL_CONFIG)),)
$(error Kernel configuration not defined, cannot build kernel)
else

ifeq ($(TARGET_USES_UNCOMPRESSED_KERNEL),true)
$(info Using uncompressed kernel)
TARGET_PREBUILT_INT_KERNEL := $(KERNEL_OUT)/arch/$(KERNEL_ARCH)/boot/Image
else
ifeq ($(KERNEL_ARCH),arm64)
TARGET_PREBUILT_INT_KERNEL := $(KERNEL_OUT)/arch/$(KERNEL_ARCH)/boot/Image.gz
else
TARGET_PREBUILT_INT_KERNEL := $(KERNEL_OUT)/arch/$(KERNEL_ARCH)/boot/zImage
endif
endif

ifeq ($(TARGET_KERNEL_APPEND_DTB), true)
$(info Using appended DTB)
TARGET_PREBUILT_INT_KERNEL := $(TARGET_PREBUILT_INT_KERNEL)-dtb
endif

KERNEL_HEADERS_INSTALL := $(KERNEL_OUT)/usr
KERNEL_HEADERS_TIMESTAMP := $(KERNEL_HEADERS_INSTALL)/build-timestamp
KERNEL_MODULES_INSTALL := system
KERNEL_MODULES_OUT := $(TARGET_OUT)/lib/modules

# Set up cross compilers
ifeq ($(KERNEL_ANDROID_Q_OR_HIGHER),true)
  # clang-r365631 is clang 9.0.6
  CLANG_HOST_TOOLCHAIN := $(PWD)/prebuilts/clang/host/linux-x86/clang-r365631/bin
  GCC_HOST_TOOLCHAIN := $(PWD)/prebuilts/gcc/linux-x86/host/x86_64-linux-glibc2.17-4.8/x86_64-linux/bin
  GCC_HOST_TOOLCHAIN_LIBEXEC := $(PWD)/prebuilts/gcc/linux-x86/host/x86_64-linux-glibc2.17-4.8/libexec/gcc/x86_64-linux/4.8.3
else
  # TODO: clang-4691093 is clang ???
  CLANG_HOST_TOOLCHAIN := $(PWD)/prebuilts/clang/host/linux-x86/clang-4691093/bin
  GCC_HOST_TOOLCHAIN := $(PWD)/prebuilts/gcc/linux-x86/host/x86_64-linux-glibc2.15-4.8/x86_64-linux/bin
  GCC_HOST_TOOLCHAIN_LIBEXEC := $(PWD)/prebuilts/gcc/linux-x86/host/x86_64-linux-glibc2.15-4.8/libexec/gcc/x86_64-linux/4.8.3
endif

# TODO: Why no differentiation between host and cross arch? Is this done via CLANG_TRIPLE?
CLANG_CC := $(CLANG_HOST_TOOLCHAIN)/clang
CLANG_HOSTCC := $(CLANG_HOST_TOOLCHAIN)/clang
CLANG_HOSTCXX := $(CLANG_HOST_TOOLCHAIN)/clang++

GCC_CC := $(PWD)/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android-
GCC_HOSTCC := $(GCC_HOST_TOOLCHAIN)/gcc
GCC_HOSTCXX := $(GCC_HOST_TOOLCHAIN)/gcc++
GCC_HOSTAR := $(GCC_HOST_TOOLCHAIN)/ar
GCC_HOSTLD := $(GCC_HOST_TOOLCHAIN)/ld

ifeq ($(TARGET_KERNEL_USE_CLANG),true)
  # TODO: Also use clang binutils
  KERNEL_TOOLCHAIN := $(PWD)/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin
  # TODO: Also use clang 32bit binutils
  KERNEL_TOOLCHAIN_32BITS := $(PWD)/prebuilts/gcc/linux-x86/arm/arm-linux-androideabi-4.9/bin
  # TODO: Also use clang here for linker commands etc.
  KERNEL_HOST_TOOLCHAIN := $(GCC_HOST_TOOLCHAIN)
  KERNEL_HOST_TOOLCHAIN_LIBEXEC := $(GCC_HOST_TOOLCHAIN_LIBEXEC)
  KERNEL_HOSTCC := $(CLANG_HOSTCC)
  KERNEL_HOSTCXX := $(CLANG_HOSTCXX)
  KERNEL_HOSTAR := $(GCC_HOSTAR)
  KERNEL_HOSTLD := $(GCC_HOSTLD)
else
  KERNEL_TOOLCHAIN := $(PWD)/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin
  KERNEL_TOOLCHAIN_32BITS := $(PWD)/prebuilts/gcc/linux-x86/arm/arm-linux-androideabi-4.9/bin
  KERNEL_HOST_TOOLCHAIN := $(GCC_HOST_TOOLCHAIN)
  KERNEL_HOST_TOOLCHAIN_LIBEXEC := $(GCC_HOST_TOOLCHAIN_LIBEXEC)
  KERNEL_HOSTCC := $(GCC_HOSTCC)
  KERNEL_HOSTCXX := $(GCC_HOSTCXX)
  KERNEL_HOSTAR := $(GCC_HOSTAR)
  KERNEL_HOSTLD := $(GCC_HOSTLD)
endif

TARGET_PREBUILT_KERNEL := $(TARGET_PREBUILT_INT_KERNEL)

define mv-modules
mdpath=`find $(KERNEL_MODULES_OUT) -type f -name modules.dep`;\
if [ "$$mdpath" != "" ];then\
mpath=`dirname $$mdpath`;\
ko=`find $$mpath/kernel -type f -name *.ko`;\
for i in $$ko; do mv $$i $(KERNEL_MODULES_OUT)/; done;\
fi
endef

define clean-module-folder
mdpath=`find $(KERNEL_MODULES_OUT) -type f -name modules.dep`;\
if [ "$$mdpath" != "" ];then\
mpath=`dirname $$mdpath`; rm -rf $$mpath;\
fi
endef

TARGET_KERNEL_SOURCE := kernel
KERNEL_SOURCE_FILES := $(call find-files-in-subdirs, $(TARGET_KERNEL_SOURCE), "*", .)
KERNEL_SOURCE_FILES := $(addprefix $(TARGET_KERNEL_SOURCE)/, $(KERNEL_SOURCE_FILES))

ifeq ($(INIT_BOOTCHART2), true)
KERNEL_CONFIG_OVERRIDE_FILES := bootchart2_defconfig
endif

$(KERNEL_OUT):
	mkdir -p $(KERNEL_OUT)

$(KERNEL_CONFIG): $(KERNEL_SOURCE_FILES) | $(KERNEL_OUT)
        @echo "Building Kernel Config"
        $(KERNEL_MAKE) $(MAKE_FLAGS) -C $(KERNEL_SRC_ABS) O=$(KERNEL_OUT_ABS) ARCH=$(KERNEL_ARCH) $(KERNEL_CROSS_COMPILE) $(KERNEL_DEFCONFIG)

ifeq ($(PRODUCT_SUPPORT_EXFAT), y)
sinclude $(ANDROID_BUILD_TOP)/device/lge/common/tuxera.mk
endif

# TODO: Use non-PHONY target for qcom wifi modules
ifeq ($(TARGET_KERNEL_MODULES),)
    TARGET_KERNEL_MODULES := no-external-modules
endif

.PHONY: $(TARGET_KERNEL_MODULES)
$(TARGET_KERNEL_MODULES): \
    $(hide) if grep -q 'CONFIG_MODULES=y' $(KERNEL_CONFIG) ; \
            then \
                echo "Building Kernel Modules" ; \
                $(KERNEL_MAKE) $(MAKE_FLAGS) -C $(KERNEL_SRC_ABS) O=$(KERNEL_OUT_ABS) ARCH=$(KERNEL_ARCH) $(KERNEL_CROSS_COMPILE) modules && \
                $(KERNEL_MAKE) $(MAKE_FLAGS) -C $(KERNEL_SRC_ABS) O=$(KERNEL_OUT_ABS) INSTALL_MOD_PATH=../../$(KERNEL_MODULES_INSTALL) ARCH=$(KERNEL_ARCH) $(KERNEL_CROSS_COMPILE) modules_install && \
                $(mv-modules) && \
                $(clean-module-folder) ; \
            else \
                echo "Kernel Modules not enabled" ; \
            fi ;

$(TARGET_PREBUILT_INT_KERNEL): $(KERNEL_OUT_STAMP) $(KERNEL_CONFIG) $(KERNEL_HEADERS_INSTALL_STAMP)
    @echo "Building Kernel"
    $(KERNEL_MAKE) $(MAKE_FLAGS) -C $(KERNEL_SRC_ABS) O=$(KERNEL_OUT_ABS) ARCH=$(KERNEL_ARCH) $(KERNEL_CROSS_COMPILE) $(TARGET_PREBUILT_INT_KERNEL_TYPE)

$(KERNEL_DTB_STAMP): $(KERNEL_OUT_STAMP) $(KERNEL_CONFIG) | $(ACP)
    $(hide) if grep -q 'CONFIG_OF=y' $(KERNEL_CONFIG) ; \
            then \
                echo "Building DTBs" ; \
                $(KERNEL_MAKE) $(MAKE_FLAGS) -C $(KERNEL_SRC_ABS) O=$(KERNEL_OUT_ABS) ARCH=$(KERNEL_ARCH) $(KERNEL_CROSS_COMPILE) dtbs ; \
            else \
                echo "DTBs not enabled" ; \
            fi ;
    $(ACP) -fp $(KERNEL_DTB)/*.dtb $(KERNEL_DTB_OUT)/
    $(hide) touch $@

ifeq ($(PRODUCT_SUPPORT_EXFAT), y)
	@cp -f $(ANDROID_BUILD_TOP)/kernel/tuxera_update.sh $(ANDROID_BUILD_TOP)
	@sh tuxera_update.sh --target target/lg.d/mobile-msm8992-3.10.84 --use-cache --latest --max-cache-entries 2 --source-dir $(ANDROID_BUILD_TOP)/kernel --output-dir $(ANDROID_BUILD_TOP)/$(KERNEL_OUT) $(SUPPORT_EXFAT_TUXERA)
	@tar -xzf tuxera-exfat*.tgz
	@mkdir -p $(TARGET_OUT_EXECUTABLES)
	@cp $(ANDROID_BUILD_TOP)/tuxera-exfat*/exfat/kernel-module/texfat.ko $(ANDROID_BUILD_TOP)/$(TARGET_OUT_EXECUTABLES)/../lib/modules/
	@cp $(ANDROID_BUILD_TOP)/tuxera-exfat*/exfat/tools/* $(TARGET_OUT_EXECUTABLES)
	$(PERL) $(ANDROID_BUILD_TOP)/kernel/scripts/sign-file sha1 $(ANDROID_BUILD_TOP)/$(KERNEL_OUT)/signing_key.priv $(ANDROID_BUILD_TOP)/$(KERNEL_OUT)/signing_key.x509 $(ANDROID_BUILD_TOP)/$(KERNEL_MODULES_OUT)/texfat.ko
	@rm -f kheaders*.tar.bz2
	@rm -f tuxera-exfat*.tgz
	@rm -rf tuxera-exfat*
	@rm -f tuxera_update.sh
endif

ifeq ($(LGESP_ITSON), yes)
	$(PERL) $(ANDROID_BUILD_TOP)/kernel/scripts/sign-file sha1 \
	$(ANDROID_BUILD_TOP)/$(KERNEL_OUT)/signing_key.priv $(ANDROID_BUILD_TOP)/$(KERNEL_OUT)/signing_key.x509 \
	$(ANDROID_BUILD_TOP)/device/lge/$(MYDEVICE)/$(TARGET_PRODUCT)/products/itson/systemassets/system/lib/modules/itson_module1.ko \
	$(ANDROID_BUILD_TOP)/$(KERNEL_MODULES_OUT)/itson_module1.ko

	$(PERL) $(ANDROID_BUILD_TOP)/kernel/scripts/sign-file sha1 \
	$(ANDROID_BUILD_TOP)/$(KERNEL_OUT)/signing_key.priv $(ANDROID_BUILD_TOP)/$(KERNEL_OUT)/signing_key.x509 \
	$(ANDROID_BUILD_TOP)/device/lge/$(MYDEVICE)/$(TARGET_PRODUCT)/products/itson/systemassets/system/lib/modules/itson_module2.ko \
	$(ANDROID_BUILD_TOP)/$(KERNEL_MODULES_OUT)/itson_module2.ko
endif

ifeq ($(ITSON_ENABLED), true)
	@cp -r $(ANDROID_BUILD_TOP)/$(ITSON_MODULE2_PATH)/$(TARGET_ARCH)/ $(ANDROID_BUILD_TOP)/$(ITSON_MODULE2_PATH)/build/
	@cp $(ANDROID_BUILD_TOP)/kernel/drivers/misc/itson_module1/*.h $(ANDROID_BUILD_TOP)/$(ITSON_MODULE2_PATH)/build/itson/
	@sh $(ANDROID_BUILD_TOP)/$(ITSON_MODULE2_PATH)/build/build-kernel-module2.sh $(KERNEL_CROSS_COMPILE) $(ANDROID_BUILD_TOP)/$(KERNEL_OUT) $(ANDROID_BUILD_TOP)/$(ITSON_MODULE2_PATH)/build 1500 $(TARGET_BUILD_VARIANT) system $(PLATFORM_VERSION)
	$(PERL) $(ANDROID_BUILD_TOP)/kernel/scripts/sign-file sha1 \
	$(ANDROID_BUILD_TOP)/$(KERNEL_OUT)/signing_key.priv $(ANDROID_BUILD_TOP)/$(KERNEL_OUT)/signing_key.x509 \
	$(ANDROID_BUILD_TOP)/$(ITSON_MODULE2_PATH)/build/$(TARGET_BUILD_VARIANT)/itson_module2.ko \
	$(ANDROID_BUILD_TOP)/$(KERNEL_MODULES_OUT)/itson_module2.ko
	@rm -rf $(ANDROID_BUILD_TOP)/$(ITSON_MODULE2_PATH)/build
endif

	$(mv-modules)
	$(clean-module-folder)

$(KERNEL_HEADERS_TIMESTAMP): $(KERNEL_HEADERS_INSTALL)
$(KERNEL_HEADERS_INSTALL): | $(KERNEL_OUT)
	$(hide) rm -rf $@
	$(hide) if [ ! -z "$(KERNEL_HEADER_DEFCONFIG)" ]; then \
			rm -f ../$(KERNEL_CONFIG); \
                        $(KERNEL_MAKE) -C $(KERNEL_SRC_ABS) O=$(KERNEL_OUT_ABS) ARCH=$(KERNEL_HEADER_ARCH) $(KERNEL_CROSS_COMPILE) $(KERNEL_HEADER_DEFCONFIG); \
                        $(KERNEL_MAKE) -C $(KERNEL_SRC_ABS) O=$(KERNEL_OUT_ABS) ARCH=$(KERNEL_HEADER_ARCH) $(KERNEL_CROSS_COMPILE) headers_install; fi
	$(hide) if [ "$(KERNEL_HEADER_DEFCONFIG)" != "$(KERNEL_DEFCONFIG)" ]; then \
			echo "Used a different defconfig for header generation"; \
			rm -f ../$(KERNEL_CONFIG); \
                        $(KERNEL_MAKE) -C $(KERNEL_SRC_ABS) O=$(KERNEL_OUT_ABS) ARCH=$(KERNEL_ARCH) $(KERNEL_CROSS_COMPILE) $(KERNEL_DEFCONFIG); fi
	$(hide) if [ ! -z "$(KERNEL_CONFIG_OVERRIDE)" ]; then \
			echo "Overriding kernel config with '$(KERNEL_CONFIG_OVERRIDE)'"; \
                        echo $(KERNEL_CONFIG_OVERRIDE) >> $(KERNEL_OUT_ABS)/.config; \
                        $(KERNEL_MAKE) -C $(KERNEL_SRC_ABS) O=$(KERNEL_OUT_ABS) ARCH=$(KERNEL_ARCH) $(KERNEL_CROSS_COMPILE) oldconfig; fi
	$(hide) touch $@/build-timestamp
	$(hide) if [ ! -z "$(KERNEL_CONFIG_OVERRIDE_FILES)" ]; then \
			echo "Overriding kernel config with '$(KERNEL_CONFIG_OVERRIDE_FILES)'"; \
			for override_file in $(KERNEL_CONFIG_OVERRIDE_FILES); \
				do cat $(TARGET_KERNEL_SOURCE)/arch/$(KERNEL_ARCH)/configs/$$override_file >> $(KERNEL_OUT)/.config; done; \
                        $(KERNEL_MAKE) -C $(KERNEL_SRC_ABS) O=$(KERNEL_OUT_ABS) ARCH=$(KERNEL_ARCH) $(KERNEL_CROSS_COMPILE) oldconfig; fi

kerneltags: $(KERNEL_CONFIG) | $(KERNEL_OUT)
        $(KERNEL_MAKE) -C $(KERNEL_SRC_ABS) O=$(KERNEL_OUT_ABS) ARCH=$(KERNEL_ARCH) $(KERNEL_CROSS_COMPILE) tags

kernelconfig: $(KERNEL_CONFIG) | $(KERNEL_OUT)
	env KCONFIG_NOTIMESTAMP=true \
	     $(KERNEL_MAKE) -C $(KERNEL_SRC_ABS) O=$(KERNEL_OUT_ABS) ARCH=$(KERNEL_ARCH) $(KERNEL_CROSS_COMPILE) menuconfig
	env KCONFIG_NOTIMESTAMP=true \
	     $(KERNEL_MAKE) -C $(KERNEL_SRC_ABS) O=$(KERNEL_OUT_ABS) ARCH=$(KERNEL_ARCH) $(KERNEL_CROSS_COMPILE) savedefconfig
￼       $(ACP) $(KERNEL_OUT)/defconfig $(KERNEL_DEFCONFIG_SRC)

endif
endif
