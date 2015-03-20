EXTN_PN547_PATH:= $(call my-dir)

LOCAL_C_INCLUDES += \
    $(EXTN_PN547_PATH)/inc \
    $(EXTN_PN547_PATH)/src/common \
    $(EXTN_PN547_PATH)/src/log \
    $(EXTN_PN547_PATH)/src/mifare \
    $(EXTN_PN547_PATH)/src/utils

LOCAL_CFLAGS += -DNXP_UICC_ENABLE
