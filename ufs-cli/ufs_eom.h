// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef UFS_EOM_H
#define UFS_EOM_H

#include <stdint.h>
#include <stdbool.h>

/* EOM Version */
#define EOM_VERSION  "1.3"

/* EOM Configuration Constants */
#define EOM_TARGET_TEST_COUNT_DEFAULT	0x5D
#define EOM_TARGET_TEST_COUNT_MAX	0x7F
#define EOM_PHY_ERROR_COUNT_THRESHOLD	0x3C
#define EOM_DIRECTION_SHIFT		0x6
#define EOM_DIRECTION_SHIFT_EXT		0x7
#define EOM_STEP_MASK			0x3F
#define EOM_STEP_MASK_EXT		0x7F
#define EOM_TEMP_DATA_SIZE		4 * 1024 * 1024	/* 4MB file */
#define EOM_TEMP_DATA_MEM_ALIGN_SIZE	4096
#define EOM_SUPPORTED_MIN_GEAR		5
#define EOM_TIMING_VOLTAGE_INIT		0xFF
#define EOM_T_EYE_HS_G5_RX_V5		0.3		/* Per M-PHY SPEC V5.0, minimum eye width is 0.3UI for Gear-5 */
#define EOM_T_EYE_HS_G4_RX_V5		0.48		/* Per M-PHY SPEC V5.0, minimum eye width is 0.48UI for Gear-4 */
#define EOM_V_DIF_AC_HS_G5_RX_V5	60		/* Per M-PHY SPEC V5.0, minimum eye height is 60mV for Gear-5 */
#define EOM_V_DIF_AC_HS_G4_RX_V5	80		/* Per M-PHY SPEC V5.0, minimum eye height is 80mV for Gear-4 */

#define EOM_CAP_EXTENDED_VOLTAGE	(1 << 4)

#define RX_EYEMON_VSTEP_WA_LANE_SHIFT	16
#define RX_EYEMON_VSTEP_WA_ENCODE(v, l)	(v | (l << RX_EYEMON_VSTEP_WA_LANE_SHIFT))

#define STRING_BUFFER_SIZE		0x24

/* EOM Result Structure */
struct eom_result {
	int lane;
	int timing;
	int volt;
	int error_cnt;
};

/* EOM Data Structure */
struct EOMData {
	int timing_max_steps;
	int timing_max_offset;
	int voltage_max_steps;
	int voltage_max_offset;
	int data_cnt;
	int num_lanes;
	int local_peer;
	int gear;
	int rate;
	/* SLT mode eye width and height information */
	int slt_eye_width_steps[2];		/* Eye Width steps for each lane */
	int slt_eye_height_steps[2];		/* Eye Height steps for each lane */
	float slt_eye_width[2];			/* Eye Width (UI) for each lane */
	float slt_eye_height[2];		/* Eye Height (mV) for each lane */
	float slt_eye_width_threshold[2];	/* Eye Width (UI) threshold for each lane */
	float slt_eye_height_threshold[2];	/* Eye Height (mV) threshold for each lane */

	struct eom_result *er;
};

/* Global Variables */
extern bool verbose;
extern struct EOMData eom_data;

/* Function Declarations */
int get_device_info(char *mname, char *pname, char *pversion);
int eom_scan(int peer, int lane, int timing, int volt, int target_count);
int disable_eye_monitor(int lane, int peer);

#endif /* UFS_EOM_H */
