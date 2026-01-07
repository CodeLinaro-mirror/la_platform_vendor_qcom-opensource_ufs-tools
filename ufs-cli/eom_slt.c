// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "common.h"
#include "ufs_eom.h"
#include "uic.h"

/* Eye width and height related macros */
#define EOM_WIDTH_INITIAL_LEFT_TIMING_STEP		-9
#define EOM_WIDTH_INITIAL_RIGHT_TIMING_STEP		6
#define EOM_HEIGHT_INITIAL_BOTTOM_VOLT_STEP		-57
#define EOM_HEIGHT_INITIAL_TOP_VOLT_STEP		57
#define EOM_HEIGHT_INITIAL_BOTTOM_VOLT_STEP_EXTENDED	-99
#define EOM_HEIGHT_INITIAL_TOP_VOLT_STEP_EXTENDED	99
#define UFS_EYEMON_HEIGHT_ADJUST(steps)		((steps) + ((((steps) - 1) / 8) * 4))

static int eom_slt_find_left(int is_peer, int lane, int target_count, int *left_timing)
{
	struct EOMData *data = &eom_data;
	int timing, error_count, ret;

	if (verbose)
		printf("Finding left eye boundary for lane %d...\n", lane);

	timing = EOM_WIDTH_INITIAL_LEFT_TIMING_STEP;

	/* Check if initial timing step exceeds timing_max_steps boundary */
	if (timing < -data->timing_max_steps) {
		timing = -data->timing_max_steps;
		if (verbose)
			printf("Left timing initial step adjusted to timing_max_steps boundary: %d\n", timing);
	} else if (timing > 0) {
		timing = 0;
		if (verbose)
			printf("Left timing initial step adjusted to 0: %d\n", timing);
	}

	ret = eom_scan(is_peer, lane, timing, /* voltage = */ 0, target_count);
	if (ret) {
		pr_err("Failed to scan (%d, %d) for left eye boundary\n", timing, 0);
		return ERROR;
	}

	error_count = data->er[data->data_cnt - 1].error_cnt;

	if (verbose)
		printf("Left initial scan: timing=%d, voltage=%d, error_count=%d\n", timing, 0, error_count);

	if (error_count) {
		/* Try timing steps to the right most till 0 */
		do {
			if (++timing > 0) {
				printf("Left eye boundary not found\n");
				return ERROR;
			}

			ret = eom_scan(is_peer, lane, timing, /* voltage = */ 0, target_count);
			if (ret) {
				pr_err("Failed to scan (%d, %d) for left eye boundary\n", timing, 0);
				return ERROR;
			}

			error_count = data->er[data->data_cnt - 1].error_cnt;

			if (verbose)
				printf("Left boundary scan: timing=%d, voltage=%d, error_count=%d\n",
				       timing, 0, error_count);

			if (error_count == 0) {
				/* Found first good point, this is the left boundary */
				*left_timing = timing;
				if (verbose)
					printf("Left eye boundary found at timing = %d\n", *left_timing);
				return SUCCESS;
			}
		} while (1);
	} else {
		/* Try timing steps to the left most till -max */
		do {
			if (--timing < -data->timing_max_steps) {
				*left_timing = -data->timing_max_steps;
				if (verbose)
					printf("Left eye boundary set to maximum timing = %d\n", *left_timing);
				return SUCCESS;
			}

			ret = eom_scan(is_peer, lane, timing, /* voltage = */ 0, target_count);
			if (ret) {
				pr_err("Failed to scan (%d, %d) for left eye boundary\n", timing, 0);
				return ERROR;
			}

			error_count = data->er[data->data_cnt - 1].error_cnt;

			if (verbose)
				printf("Left boundary scan: timing=%d, voltage=%d, error_count=%d\n",
				       timing, 0, error_count);

			if (error_count != 0) {
				/* Found first bad point, left boundary is timing + 1 */
				*left_timing = timing + 1;
				if (verbose)
					printf("Left eye boundary found at timing = %d\n", *left_timing);
				return SUCCESS;
			}
		} while (1);
	}
}

static int eom_slt_find_right(int is_peer, int lane, int target_count, int *right_timing)
{
	struct EOMData *data = &eom_data;
	int timing, error_count, ret;

	if (verbose)
		printf("Finding right eye boundary for lane %d...\n", lane);

	timing = EOM_WIDTH_INITIAL_RIGHT_TIMING_STEP;

	/* Check if initial timing step exceeds timing_max_steps boundary */
	if (timing > data->timing_max_steps) {
		timing = data->timing_max_steps;
		if (verbose)
			printf("Right timing initial step adjusted to timing_max_steps boundary: %d\n", timing);
	} else if (timing < 0) {
		timing = 0;
		if (verbose)
			printf("Right timing initial step adjusted to 0: %d\n", timing);
	}

	ret = eom_scan(is_peer, lane, timing, /* voltage = */ 0, target_count);
	if (ret) {
		pr_err("Failed to scan (%d, %d) for right eye boundary\n", timing, 0);
		return ERROR;
	}

	error_count = data->er[data->data_cnt - 1].error_cnt;

	if (verbose)
		printf("Right initial scan: timing=%d, voltage=%d, error_count=%d\n", timing, 0, error_count);

	if (error_count) {
		/* Try timing steps to the left most till 0 */
		do {
			if (--timing < 0) {
				printf("Right eye boundary not found\n");
				return ERROR;
			}

			ret = eom_scan(is_peer, lane, timing, /* voltage = */ 0, target_count);
			if (ret) {
				pr_err("Failed to scan (%d, %d) for right eye boundary\n", timing, 0);
				return ERROR;
			}

			error_count = data->er[data->data_cnt - 1].error_cnt;

			if (verbose)
				printf("Right boundary scan: timing=%d, voltage=%d, error_count=%d\n",
				       timing, 0, error_count);

			if (error_count == 0) {
				/* Found first good point, this is the right boundary */
				*right_timing = timing;
				if (verbose)
					printf("Right eye boundary found at timing = %d\n", *right_timing);
				return SUCCESS;
			}
		} while (1);
	} else {
		/* Try timing steps to the right most till max */
		do {
			if (++timing > data->timing_max_steps) {
				*right_timing = data->timing_max_steps;
				if (verbose)
					printf("Right eye boundary set to maximum right timing = %d\n", *right_timing);
				return SUCCESS;
			}

			ret = eom_scan(is_peer, lane, timing, /* voltage = */ 0, target_count);
			if (ret) {
				pr_err("Failed to scan (%d, %d) for right eye boundary\n", timing, 0);
				return ERROR;
			}

			error_count = data->er[data->data_cnt - 1].error_cnt;

			if (verbose)
				printf("Right boundary scan: timing=%d, voltage=%d, error_count=%d\n",
				       timing, 0, error_count);

			if (error_count != 0) {
				/* Found first bad point, right boundary is timing - 1 */
				*right_timing = timing - 1;
				if (verbose)
					printf("Right eye boundary found at timing = %d\n", *right_timing);
				return SUCCESS;
			}
		} while (1);
	}
}

static int eom_slt_find_bottom(int is_peer, int lane, int target_count, int timing_center, int *bottom_voltage)
{
	struct EOMData *data = &eom_data;
	int voltage, error_count, ret;

	if (verbose)
		printf("Finding bottom eye boundary for lane %d at timing %d...\n",
		       lane, timing_center);

	voltage = (data->use_extended_voltage) ?
		   EOM_HEIGHT_INITIAL_BOTTOM_VOLT_STEP_EXTENDED :
		   EOM_HEIGHT_INITIAL_BOTTOM_VOLT_STEP;

	/* Check if initial voltage step exceeds voltage_max_steps boundary */
	if (voltage < -data->voltage_max_steps) {
		voltage = -data->voltage_max_steps;
		if (verbose)
			printf("Bottom voltage initial step adjusted to voltage_max_steps boundary: %d\n", voltage);
	} else if (voltage > 0) {
		voltage = 0;
		if (verbose)
			printf("Bottom voltage initial step adjusted to 0: %d\n", voltage);
	}

	ret = eom_scan(is_peer, lane, timing_center, voltage, target_count);
	if (ret) {
		pr_err("Failed to scan (%d, %d) for bottom eye boundary\n", timing_center, voltage);
		return ERROR;
	}

	error_count = data->er[data->data_cnt - 1].error_cnt;

	if (verbose)
		printf("Bottom initial scan: timing=%d, voltage=%d, error_count=%d\n",
		       timing_center, voltage, error_count);

	if (error_count) {
		/* Try voltage steps to the top most till 0 */
		do {
			if (++voltage > 0) {
				printf("Bottom eye boundary not found\n");
				return ERROR;
			}

			ret = eom_scan(is_peer, lane, timing_center, voltage,
				       target_count);
			if (ret) {
				pr_err("Failed to scan (%d, %d) for bottom eye boundary\n",
				       timing_center, voltage);
				return ERROR;
			}

			error_count = data->er[data->data_cnt - 1].error_cnt;

			if (verbose)
				printf("Bottom boundary scan: timing=%d, voltage=%d, error_count=%d\n",
				       timing_center, voltage, error_count);

			if (error_count == 0) {
				/* Found first good point, this is the bottom boundary */
				*bottom_voltage = voltage;
				if (verbose)
					printf("Bottom eye boundary found at voltage = %d\n", *bottom_voltage);
				return SUCCESS;
			}
		} while (1);
	} else {
		/* Try voltage steps to the bottom most till -max */
		do {
			if (--voltage < -data->voltage_max_steps) {
				*bottom_voltage = -data->voltage_max_steps;
				if (verbose)
					printf("Bottom eye boundary set to maximum bottom voltage = %d\n",
					       *bottom_voltage);
				return SUCCESS;
			}

			ret = eom_scan(is_peer, lane, timing_center, voltage,
				       target_count);
			if (ret) {
				pr_err("Failed to scan (%d, %d) for bottom eye boundary\n",
				       timing_center, voltage);
				return ERROR;
			}

			error_count = data->er[data->data_cnt - 1].error_cnt;

			if (verbose)
				printf("Bottom boundary scan: timing=%d, voltage=%d, error_count=%d\n",
				       timing_center, voltage, error_count);

			if (error_count != 0) {
				/* Found first bad point, bottom boundary is voltage + 1 */
				*bottom_voltage = voltage + 1;
				if (verbose)
					printf("Bottom eye boundary found at voltage = %d\n", *bottom_voltage);
				return SUCCESS;
			}
		} while (1);
	}
}

static int eom_slt_find_top(int is_peer, int lane, int target_count, int timing_center, int *top_voltage)
{
	struct EOMData *data = &eom_data;
	int voltage, error_count, ret;

	if (verbose)
		printf("Finding top eye boundary for lane %d at timing %d...\n", lane, timing_center);

	voltage = (data->use_extended_voltage) ?
		   EOM_HEIGHT_INITIAL_TOP_VOLT_STEP_EXTENDED :
		   EOM_HEIGHT_INITIAL_TOP_VOLT_STEP;

	/* Check if initial voltage step exceeds voltage_max_steps boundary */
	if (voltage > data->voltage_max_steps) {
		voltage = data->voltage_max_steps;
		if (verbose)
			printf("Top voltage initial step adjusted to voltage_max_steps boundary: %d\n", voltage);
	} else if (voltage < 0) {
		voltage = 0;
		if (verbose)
			printf("Top voltage initial step adjusted to 0: %d\n", voltage);
	}

	ret = eom_scan(is_peer, lane, timing_center, voltage, target_count);
	if (ret) {
		pr_err("Failed to scan (%d, %d) for top eye boundary\n", timing_center, voltage);
		return ERROR;
	}

	error_count = data->er[data->data_cnt - 1].error_cnt;

	if (verbose)
		printf("Top initial scan: timing=%d, voltage=%d, error_count=%d\n",
		       timing_center, voltage, error_count);

	if (error_count) {
		/* Try voltage steps to the bottom most till 0 */
		do {
			if (--voltage < 0) {
				printf("Top eye boundary not found\n");
				return ERROR;
			}

			ret = eom_scan(is_peer, lane, timing_center, voltage,
				       target_count);
			if (ret) {
				pr_err("Failed to scan (%d, %d) for top eye boundary\n",
				       timing_center, voltage);
				return ERROR;
			}

			error_count = data->er[data->data_cnt - 1].error_cnt;

			if (verbose)
				printf("Top boundary scan: timing=%d, voltage=%d, error_count=%d\n",
				       timing_center, voltage, error_count);

			if (error_count == 0) {
				/* Found first good point, this is the top boundary */
				*top_voltage = voltage;
				if (verbose)
					printf("Top eye boundary found at voltage = %d\n", *top_voltage);
				return SUCCESS;
			}
		} while (1);
	} else {
		/* Try voltage steps to the top most till max */
		do {
			if (++voltage > data->voltage_max_steps) {
				*top_voltage = data->voltage_max_steps;
				if (verbose)
					printf("Top eye boundary set to maximum top voltage = %d\n", *top_voltage);
				return SUCCESS;
			}

			ret = eom_scan(is_peer, lane, timing_center, voltage,
				       target_count);
			if (ret) {
				pr_err("Failed to scan (%d, %d) for top eye boundary\n",
				       timing_center, voltage);
				return ERROR;
			}

			error_count = data->er[data->data_cnt - 1].error_cnt;

			if (verbose)
				printf("Top boundary scan: timing=%d, voltage=%d, error_count=%d\n",
				       timing_center, voltage, error_count);

			if (error_count != 0) {
				/* Found first bad point, top boundary is voltage - 1 */
				*top_voltage = voltage - 1;
				if (verbose)
					printf("Top eye boundary found at voltage = %d\n", *top_voltage);
				return SUCCESS;
			}
		} while (1);
	}
}

static int __eom_scan_slt(int is_peer, int lane, int target_count)
{
	struct EOMData *data = &eom_data;
	float timing_step_size, eye_width_threshold, eye_width;
	float voltage_step_size, eye_height_threshold, eye_height;
	bool eye_width_pass = false, eye_height_pass = false;
	int left_timing = 0, right_timing = 0;
	int top_voltage = 0, bottom_voltage = 0;
	int timing_center = 0;
	int eye_width_steps = 0, eye_height_steps = 0;
	int ret;

	if (verbose)
		printf("Starting SLT EOM scan for lane %d...\n", lane);

	/* Find left eye boundary */
	ret = eom_slt_find_left(is_peer, lane, target_count, &left_timing);
	if (ret) {
		pr_err("Failed to find left eye width boundary for lane %d\n", lane);
		return ERROR;
	}

	/* Find right eye boundary */
	ret = eom_slt_find_right(is_peer, lane, target_count, &right_timing);
	if (ret) {
		pr_err("Failed to find right eye width boundary for lane %d\n", lane);
		return ERROR;
	}

	/* Calculate eye width and center timing */
	eye_width_steps = right_timing - left_timing + 1;
	timing_center = left_timing + eye_width_steps / 2;

	/* Calculate UI size of per timing step */
	timing_step_size = (float)data->timing_max_offset * 0.01f / (float)data->timing_max_steps;
	if (verbose)
		printf("Lane %d Eye Width steps: %d (left=%d, right=%d, center=%d), timing_step_size:%f\n",
		       lane, eye_width_steps, left_timing, right_timing, timing_center, timing_step_size);

	/* Validate timing_step_size is non-zero */
	if (timing_step_size == 0.0f) {
		pr_err("Invalid timing_step_size (zero)\n");
		return ERROR;
	}

	/* Calculate eye width in UI units */
	eye_width = (float)eye_width_steps * timing_step_size;

	/* Set eye width threshold based on UFS gear */
	if (data->gear == UFS_HS_G4)
		eye_width_threshold = (data->unipro_ver >= UFS_UNIPRO_VER_3) ?
				      EOM_T_EYE_HS_G4_RX_V6 : EOM_T_EYE_HS_G4_RX_V5;
	else if (data->gear == UFS_HS_G5)
		eye_width_threshold = (data->unipro_ver >= UFS_UNIPRO_VER_3) ?
				      EOM_T_EYE_HS_G5_RX_V6 : EOM_T_EYE_HS_G5_RX_V5;

	/* Save eye width information to EOMData structure */
	data->slt_eye_width[lane] = eye_width;
	data->slt_eye_width_steps[lane] = eye_width_steps;
	data->slt_eye_width_threshold[lane] = eye_width_threshold;

	/* Check eye width against the calculated threshold */
	if (eye_width <= eye_width_threshold)
		eye_width_pass = false;
	else
		eye_width_pass = true;

	printf("UFS EOM SLT (PassEyeWidth %.2fUI): Lane %d Eye Width %.2fUI (%d steps) - %s\n",
	       eye_width_threshold, lane, eye_width, eye_width_steps, eye_width_pass ? "Pass" : "Fail");

	if (data->unipro_ver >= UFS_UNIPRO_VER_3)
		goto out;

	/* Find bottom eye boundary */
	ret = eom_slt_find_bottom(is_peer, lane, target_count, timing_center, &bottom_voltage);
	if (ret) {
		pr_err("Failed to find bottom eye height boundary for lane %d\n", lane);
		return ERROR;
	}

	/* Find top eye boundary */
	ret = eom_slt_find_top(is_peer, lane, target_count, timing_center, &top_voltage);
	if (ret) {
		pr_err("Failed to find top eye height boundary for lane %d\n", lane);
		return ERROR;
	}

	/* Calculate eye height */
	if (is_peer)
		eye_height_steps = top_voltage - bottom_voltage + 1;
	else
		eye_height_steps = UFS_EYEMON_HEIGHT_ADJUST(top_voltage) + UFS_EYEMON_HEIGHT_ADJUST(-bottom_voltage);

	/* Calculate voltage size(mv) of per voltage step */
	voltage_step_size = (float)data->voltage_max_offset * 10.0f / (float)data->voltage_max_steps;
	if (verbose)
		printf("Lane %d Eye Height steps: %d (top=%d, bottom=%d), voltage_step_size:%f\n",
			lane, eye_height_steps, top_voltage, bottom_voltage, voltage_step_size);

	/* Validate voltage_step_size is non-zero */
	if (voltage_step_size == 0.0f) {
		pr_err("Invalid voltage_step_size (zero)\n");
		return ERROR;
	}

	eye_height = (float)eye_height_steps * voltage_step_size;

	/* Set eye height threshold based on UFS gear */
	if (data->gear == UFS_HS_G4)
		eye_height_threshold = (data->unipro_ver >= UFS_UNIPRO_VER_3) ?
				       EOM_V_DIF_AC_HS_G4_RX_V6 : EOM_V_DIF_AC_HS_G4_RX_V5;
	else if (data->gear == UFS_HS_G5)
		eye_height_threshold = (data->unipro_ver >= UFS_UNIPRO_VER_3) ?
				       EOM_V_DIF_AC_HS_G5_RX_V6 : EOM_V_DIF_AC_HS_G5_RX_V5;

	/* Save eye height information to EOMData structure */
	data->slt_eye_height[lane] = eye_height;
	data->slt_eye_height_steps[lane] = eye_height_steps;
	data->slt_eye_height_threshold[lane] = eye_height_threshold;

	/* Check eye height against the calculated threshold */
	if (eye_height <= eye_height_threshold)
		eye_height_pass = false;
	else
		eye_height_pass = true;

	printf("UFS EOM SLT (PassEyeHeight %.2fmV): Lane %d Eye Height %.2fmV (%d steps) - %s\n",
	       eye_height_threshold, lane, eye_height, eye_height_steps, eye_height_pass ? "Pass" : "Fail");

out:
	return SUCCESS;
}

int eom_scan_slt(struct EOMData *data, int lane, int target_test_count)
{
	int l, n, scan_ret, ret = SUCCESS;

	if (verbose)
		printf("Running EOM Scan SLT...\n");

	for (l = lane, n = data->num_lanes; n > 0; n--, l++) {
		scan_ret = __eom_scan_slt(data->local_peer, l, target_test_count);
		if (scan_ret) {
			pr_err("Failed to run EOM Scan SLT for lane %d\n", l);
			ret |= scan_ret;
		}

		disable_eye_monitor(l, data->local_peer);
	}

	return ret;
}
