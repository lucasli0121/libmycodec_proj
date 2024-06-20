LOCAL_PATH := $(call my-dir)
JNI_PATH := $(LOCAL_PATH)


$(warning, $(TARGET_ARCH_ABI))

include $(CLEAR_VARS)
LOCAL_PATH := /

# Include all submodules declarations
include $(JNI_PATH)/libmycodec/Android.mk