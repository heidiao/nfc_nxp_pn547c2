VOB_COMPONENTS := external/libnfc-nci/src
NFA := $(VOB_COMPONENTS)/nfa
NFC := $(VOB_COMPONENTS)/nfc

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
include $(call all-makefiles-under,$(LOCAL_PATH))

LOCAL_PRELINK_MODULE := false

ifneq ($(NCI_VERSION),)
LOCAL_CFLAGS += -DNCI_VERSION=$(NCI_VERSION) -O0 -g
endif

LOCAL_CFLAGS += -Wall -Wextra

#NXP PN547 Enable
LOCAL_CFLAGS += -DNFCC_PN547 -DNFC_NXP_NOT_OPEN_INCLUDED=TRUE

ifeq ($(NFC_NXP_P61),true)
LOCAL_CFLAGS +=-DNFC_NXP_P61
endif
#Gemalto SE Support
LOCAL_CFLAGS += -DGEMATO_SE_SUPPORT -DCHECK_FOR_NFCEE_CONFIGURATION
LOCAL_CFLAGS += -DNXP_UICC_ENABLE

define all-cpp-files-under
$(patsubst ./%,%, \
  $(shell cd $(LOCAL_PATH) ; \
          find $(1) -name "*.cpp" -and -not -name ".*") \
 )
endef

LOCAL_SRC_FILES += $(call all-cpp-files-under, .) $(call all-c-files-under, .)

LOCAL_C_INCLUDES += \
    bionic \
    bionic/libstdc++ \
    external/stlport/stlport \
    external/icu4c/common \
    frameworks/native/include \
    libcore/include \
    $(NFA)/include \
    $(NFA)/brcm \
    $(NFC)/include \
    $(NFC)/brcm \
    $(NFC)/int \
    $(VOB_COMPONENTS)/hal/include \
    $(VOB_COMPONENTS)/hal/int \
    $(VOB_COMPONENTS)/include \
    $(VOB_COMPONENTS)/gki/ulinux \
    $(VOB_COMPONENTS)/gki/common

ifeq ($(NFC_NXP_P61),true)
LOCAL_C_INCLUDES +=external/p61-jcop-kit/include

endif

LOCAL_SHARED_LIBRARIES := \
    libicuuc \
    libnativehelper \
    libcutils \
    libutils \
    liblog \
    libnfc-nci \
    libstlport

ifeq ($(NFC_NXP_P61),true)
LOCAL_SHARED_LIBRARIES += libp61-jcop-kit
endif

#LOCAL_STATIC_LIBRARIES := libxml2

LOCAL_MODULE := libnfc_nci_jni
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
