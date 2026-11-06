TARGET := iphone:clang:latest:15.0
ARCHS := arm64
THEOS_PACKAGE_SCHEME := rootless

include $(THEOS)/makefiles/common.mk

TWEAK_NAME := OldiOSLauncher
OldiOSLauncher_FILES := Tweak.x
OldiOSLauncher_CFLAGS := -fobjc-arc
OldiOSLauncher_FRAMEWORKS := Foundation

include $(THEOS_MAKE_PATH)/tweak.mk
