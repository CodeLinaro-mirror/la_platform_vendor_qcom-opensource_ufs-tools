// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef UFS_EOM_H
#define UFS_EOM_H

#include <stdint.h>
#include <stdbool.h>
#include "common.h"

/* EOM Version */
#define EOM_VERSION		"2.0"

/* EOM Configuration Constants */
#define EOM_TARGET_TEST_COUNT_DEFAULT		0x5D
#define EOM_TARGET_TEST_COUNT_MAX		0x7F
#define EOM_PHY_ERROR_COUNT_THRESHOLD		0x3C
#define EOM_DIRECTION_SHIFT			0x6
#define EOM_DIRECTION_SHIFT_EXT			0x7
#define EOM_STEP_MASK				0x3F
#define EOM_STEP_MASK_EXT			0x7F
#define EOM_TEMP_DATA_SIZE			(4 * 1024 * 1024)	/* 4MB */
#define EOM_TEMP_DATA_MEM_ALIGN_SIZE		4096
#define EOM_SUPPORTED_MIN_GEAR			5
#define EOM_TIMING_VOLTAGE_INIT			0xFF

/* M-PHY SPEC V5.0 eye thresholds */
#define EOM_T_EYE_HS_G4_RX_V5		0.48f	/* min eye width (UI) for Gear-4 */
#define EOM_T_EYE_HS_G5_RX_V5		0.3f	/* min eye width (UI) for Gear-5 */
#define EOM_V_DIF_AC_HS_G4_RX_V5	80	/* min eye height (mV) for Gear-4 */
#define EOM_V_DIF_AC_HS_G5_RX_V5	60	/* min eye height (mV) for Gear-5 */

/* M-PHY SPEC V6.0 eye thresholds */
#define EOM_T_EYE_HS_G4_RX_V6		0.48f	/* min eye width (UI) for Gear-4 */
#define EOM_T_EYE_HS_G5_RX_V6		0.45f	/* min eye width (UI) for Gear-5 */
#define EOM_V_DIF_AC_HS_G4_RX_V6	80	/* min eye height (mV) for Gear-4 */
#define EOM_V_DIF_AC_HS_G5_RX_V6	120	/* min eye height (mV) for Gear-5 */

#define EOM_CAP_EXTENDED_VOLTAGE		(1 << 4)

#define STRING_BUFFER_SIZE			0x24

/* Single EOM point result */
struct ufs_eom_result {
	int lane;
	int timing;
	int volt;
	int error_cnt;
};

/* EOM software scan data collected during a full eye scan */
struct ufs_eom_data {
	int num_lanes;

	/* Collected scan results */
	int data_cnt;
	struct ufs_eom_result *er;
};

/* EOM hardware capabilities read from M-PHY registers at session start */
struct ufs_eom_caps {
	int timing_max_steps;
	int timing_max_offset;
	int voltage_max_steps;
	int voltage_max_offset;
	int unipro_ver;
	bool use_extended_vrange;
	__u32 rx_eyemon_cap;
};

/* EOM scan configuration parameters derived from command-line arguments */
struct ufs_eom_config {
	int local_peer;
	int start_lane;
	int voltage_low;
	int voltage_high;
	int timing_left;
	int timing_right;
	int target_test_count;
	bool generate_io;
	bool verbose_logging;
	char output_path[DEVICE_PATH_NAME_SIZE_MAX];
	char device_path[DEVICE_PATH_NAME_SIZE_MAX];
};

/* Runtime context holding all state for a UFS EOM scan session */
struct ufs_eom_context {
	int bsg_fd;
	int data_fd;
	int gear;
	int rate;
	int eom_result_count;
	struct ufs_eom_config cfg;
	struct ufs_eom_data data;
	struct ufs_eom_caps caps;
};

/**
 * ufs_eom_get_device_info() - Read manufacturer name, product name and
 *                             product revision from UFS device descriptors.
 * @bsg_fd: File descriptor for the UFS BSG device.
 * @mname: Output buffer for the manufacturer name string.
 * @pname: Output buffer for the product name string.
 * @pversion: Output buffer for the product revision level string.
 *
 * Return: SUCCESS on success, ERROR on failure.
 */
int ufs_eom_get_device_info(int bsg_fd, char *mname, char *pname,
			    char *pversion);

/**
 * ufs_eom_scan_point() - Perform a single EOM measurement at a given lane, timing
 *                  and voltage offset.
 * @ctx: EOM session context.
 * @lane: Lane index to measure.
 * @timing: Signed timing offset to apply.
 * @volt: Signed voltage offset to apply.
 *
 * Return: SUCCESS on success, ERROR on failure, INVAL if the measurement
 * has not yet completed (caller should retry).
 */
int ufs_eom_scan_point(struct ufs_eom_context *ctx, int lane, int timing, int volt);

/**
 * ufs_eom_disable_eye_monitor() - Disable the M-PHY eye monitor for a lane.
 * @ctx: EOM session context.
 * @lane: Lane index to disable.
 *
 * Return: SUCCESS on success, ERROR on failure.
 */
int ufs_eom_disable_eye_monitor(struct ufs_eom_context *ctx, int lane);

/**
 * ufs_eom_generate_reports() - Generate output report files for a completed
 *                              EOM scan.
 * @ctx: EOM session context containing scan results.
 * @output_file: Full path of the output report file to create.
 *
 * Return: SUCCESS on success, ERROR on failure.
 */
int ufs_eom_generate_reports(struct ufs_eom_context *ctx,
			     const char *output_file);

#endif /* UFS_EOM_H */
