LOCAL_PATH := $(call my-dir)
 
include $(CLEAR_VARS)
 
LOCAL_MODULE := libmycodec
 
include $(LOCAL_PATH)/config-android.mak
 
SRC = src/codec.cpp \
	src/log.cpp \
	src/vcodec.cpp \
	src/audio_codec.cpp \
	src/demuxing_decode.cpp

CFLAGS += -I$(LOCAL_PATH)/inc 

LOCAL_CPP_EXTENSION := .cpp
LOCAL_SRC_FILES := $(SRC)

#NDK_DEBUG=1

LOCAL_CC := $(CC)
LOCAL_CXX := $(CXX)
TARGET_CC := $(CC)
TARGET_CXX := $(CXX)
LOCAL_CFLAGS := $(CFLAGS)
LOCAL_CPPFLAGS := $(CXXFLAGS)
LOCAL_LDFLAGS := $(LDFLAGS)
LOCAL_LDLIBS := $(LDLIBS)
#LOCAL_SHARED_LIBRARIES := $(SHARED_LIBS)
#LOCAL_STATIC_LIBRARIES := $(STATIC_LIBS)

include $(BUILD_SHARED_LIBRARY)

