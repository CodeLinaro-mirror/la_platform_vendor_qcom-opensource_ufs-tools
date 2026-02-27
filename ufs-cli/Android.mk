LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := lsufs
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES:= shell.c uic.c query.c ufs_bsg.c common.c lsufs.c query_trans.c
LOCAL_SHARED_LIBRARIES := libcutils libc
LOCAL_C_INCLUDES+= $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
LOCAL_MODULE_PATH := $(TARGET_OUT_DATA)/ufs-tools
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := ufseom
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := uic.c query.c ufs_bsg.c common.c query_trans.c
LOCAL_SRC_FILES += ufs-eom/ufs_eom.c ufs-eom/ufs_eom_report.c
LOCAL_SRC_FILES += ufs-eom/ufs_eom_slt.c ufs-eom/ufs_eom_qcom.c
LOCAL_SHARED_LIBRARIES := libcutils libc
LOCAL_C_INCLUDES+= $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/ufs-eom
LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
LOCAL_MODULE_PATH := $(TARGET_OUT_DATA)/ufs-tools
include $(BUILD_EXECUTABLE)
