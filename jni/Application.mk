# The ARMv7 is significanly faster due to the use of the hardware FPU
#APP_ABI := armeabi-v7a
APP_ABI := arm64-v8a
API := 32
#NDK := E:/android-sdk-windows/ndk/26.3.11579264
NDK :=~/android-ndk-r26d
SYSTEM_ARCH := linux-x86_64
#SYSTEM_ARCH := windows-x86_64
APP_PLATFORM := android-$(API)
APP_STL := c++_shared
APP_CPPFLAGS += -frtti
APP_CPPFLAGS += -fexceptions
