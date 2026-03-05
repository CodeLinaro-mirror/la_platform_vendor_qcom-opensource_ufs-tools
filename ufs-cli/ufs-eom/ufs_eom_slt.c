// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include <stdio.h>
#include <stdlib.h>
#include "ufs_eom_slt.h"
#include "common.h"
#include "uic.h"
#include "ufs_eom.h"

/**
 * ufs_eom_slt_find_horizontal_eye_boundary() - Probe the boundary of the eye
 * opening along the timing (horizontal) axis at a fixed voltage position.
 * @ctx: EOM session context.
 * @lane: Lane index to probe.
 * @initial_step: Starting timing step; its direction determines search direction
 *                (negative → left boundary, positive → right boundary).
 * @max_steps: Hardware timing step limit (magnitude).
 * @volt_step: Voltage step held constant during the horizontal search.
 * @result: Output parameter set to the boundary timing step on success.
 *
 * The search direction is inferred from the direction of @initial_step:
 *   - Negative initial_step → searching the left boundary.
 *   - Positive initial_step → searching the right boundary.
 *
 * Return: SUCCESS on success, ERROR if the boundary cannot be found.
 */
static int
ufs_eom_slt_find_horizontal_eye_boundary(struct ufs_eom_context *ctx,
					 int lane, int initial_step, int max_steps,
					 int volt_step, int *result)
{
	struct ufs_eom_data *data = &ctx->data;
	int initial_step_dir = (initial_step > 0) ? 1 : -1;
	int step_toward_center = -initial_step_dir;
	int step_away_center = initial_step_dir;
	int limit = initial_step_dir * max_steps;
	int step, error_count, ret;

	if (initial_step < -max_steps)
		step = -max_steps;
	else if (initial_step > max_steps)
		step = max_steps;
	else
		step = initial_step;

	if (ctx->cfg.verbose_logging)
		printf("Finding %s timing boundary for lane %d (initial=%d)...\n",
		       (initial_step_dir < 0) ? "left" : "right", lane, step);

	ret = ufs_eom_scan_point(ctx, lane, step, volt_step);
	if (ret) {
		pr_err("Failed to probe eye boundary at timing step %d, voltage step %d: %d\n",
		       step, volt_step, ret);
		return ERROR;
	}

	error_count = data->er[data->data_cnt - 1].error_cnt;

	if (ctx->cfg.verbose_logging)
		printf("  Initial probe: step=%d error_count=%d\n",
		       step, error_count);

	if (error_count) {
		/*
		 * Initial point is outside the eye.  Walk toward the centre
		 * until we find the first clean point — that is the boundary.
		 */
		while (1) {
			step += step_toward_center;

			/* Reached the centre without finding a clean point */
			if (initial_step_dir > 0 ? step < 0 : step > 0) {
				pr_err("Eye boundary not found for lane %d\n",
				       lane);
				return ERROR;
			}

			ret = ufs_eom_scan_point(ctx, lane, step, volt_step);
			if (ret) {
				pr_err("Failed to probe eye boundary at timing step %d,"
				       "voltage step %d: %d\n",
				       step, volt_step, ret);
				return ERROR;
			}

			error_count = data->er[data->data_cnt - 1].error_cnt;

			if (ctx->cfg.verbose_logging)
				printf("  Recovery scan: step=%d error_count=%d\n",
				       step, error_count);

			if (error_count == 0) {
				/* First clean point is the boundary */
				*result = step;
				if (ctx->cfg.verbose_logging)
					printf("  Boundary found at step=%d\n",
					       *result);
				return SUCCESS;
			}
		}
	} else {
		/*
		 * Initial point is inside the eye. Walk away from the centre
		 * until we find the first erroneous point; the boundary is one
		 * step back toward the centre.
		 */
		while (1) {
			step += step_away_center;

			/* Reached the hardware limit — boundary is at the limit */
			if (initial_step_dir > 0 ? step > limit : step < limit) {
				*result = limit;
				if (ctx->cfg.verbose_logging)
					printf("  Boundary set to hw limit %d\n",
					       *result);
				return SUCCESS;
			}

			ret = ufs_eom_scan_point(ctx, lane, step, volt_step);
			if (ret) {
				pr_err("Failed to probe eye boundary at timing step %d,"
				       "voltage step %d: %d\n",
				       step, volt_step, ret);
				return ERROR;
			}

			error_count = data->er[data->data_cnt - 1].error_cnt;

			if (ctx->cfg.verbose_logging)
				printf("  Expansion scan: step=%d error_count=%d\n",
				       step, error_count);

			if (error_count != 0) {
				/* First bad point — boundary is one step back */
				*result = step + step_toward_center;
				if (ctx->cfg.verbose_logging)
					printf("  Boundary found at step=%d\n",
					       *result);
				return SUCCESS;
			}
		}
	}
}

/**
 * ufs_eom_slt_find_vertical_eye_boundary() - Probe the boundary of the eye
 * opening along the voltage (vertical) axis at a fixed timing position.
 * @ctx: EOM session context.
 * @lane: Lane index to probe.
 * @initial_step: Starting voltage step; its direction determines search direction
 *                (negative → bottom boundary, positive → top boundary).
 * @max_steps: Hardware voltage step limit (magnitude).
 * @timing_step: Timing step held constant during the vertical search.
 * @result: Output parameter set to the boundary voltage step on success.
 *
 * The search direction is inferred from the direction of @initial_step:
 *   - Negative initial_step → searching the bottom boundary.
 *   - Positive initial_step → searching the top boundary.
 *
 * Return: SUCCESS on success, ERROR if the boundary cannot be found.
 */
static int
ufs_eom_slt_find_vertical_eye_boundary(struct ufs_eom_context *ctx,
				       int lane, int initial_step, int max_steps,
				       int timing_step, int *result)
{
	struct ufs_eom_data *data = &ctx->data;
	/*
	 * initial_step_dir > 0 → top boundary; initial_step_dir < 0 → bottom boundary.
	 * step_toward_center moves the step toward the eye centre (toward 0).
	 * step_away_center moves the step away from the eye centre.
	 */
	int initial_step_dir = (initial_step > 0) ? 1 : -1;
	int step_toward_center = -initial_step_dir;
	int step_away_center = initial_step_dir;
	int limit = initial_step_dir * max_steps;
	int step, error_count, ret;

	if (initial_step < -max_steps)
		step = -max_steps;
	else if (initial_step > max_steps)
		step = max_steps;
	else
		step = initial_step;

	if (ctx->cfg.verbose_logging)
		printf("Finding %s voltage boundary for lane %d (initial=%d)...\n",
		       (initial_step_dir < 0) ? "bottom" : "top", lane, step);

	ret = ufs_eom_scan_point(ctx, lane, timing_step, step);
	if (ret) {
		pr_err("Failed to probe eye boundary at timing step %d, voltage step %d: %d\n",
		       timing_step, step, ret);
		return ERROR;
	}

	error_count = data->er[data->data_cnt - 1].error_cnt;

	if (ctx->cfg.verbose_logging)
		printf("  Initial probe: step=%d error_count=%d\n",
		       step, error_count);

	if (error_count) {
		/*
		 * Initial point is outside the eye.  Walk toward the centre
		 * until we find the first clean point — that is the boundary.
		 */
		while (1) {
			step += step_toward_center;

			/* Reached the centre without finding a clean point */
			if (initial_step_dir > 0 ? step < 0 : step > 0) {
				pr_err("Eye boundary not found for lane %d\n",
				       lane);
				return ERROR;
			}

			ret = ufs_eom_scan_point(ctx, lane, timing_step, step);
			if (ret) {
				pr_err("Failed to probe eye boundary at timing step %d,"
				       "voltage step %d: %d\n",
				       timing_step, step, ret);
				return ERROR;
			}

			error_count = data->er[data->data_cnt - 1].error_cnt;

			if (ctx->cfg.verbose_logging)
				printf("  Recovery scan: step=%d error_count=%d\n",
				       step, error_count);

			if (error_count == 0) {
				/* First clean point is the boundary */
				*result = step;
				if (ctx->cfg.verbose_logging)
					printf("  Boundary found at step=%d\n",
					       *result);
				return SUCCESS;
			}
		}
	} else {
		/*
		 * Initial point is inside the eye.  Walk away from the centre
		 * until we find the first erroneous point; the boundary is one
		 * step back toward the centre.
		 */
		while (1) {
			step += step_away_center;

			/* Reached the hardware limit — boundary is at the limit */
			if (initial_step_dir > 0 ? step > limit : step < limit) {
				*result = limit;
				if (ctx->cfg.verbose_logging)
					printf("  Boundary set to hw limit %d\n",
					       *result);
				return SUCCESS;
			}

			ret = ufs_eom_scan_point(ctx, lane, timing_step, step);
			if (ret) {
				pr_err("Failed to probe eye boundary at timing step %d,"
				       "voltage step %d: %d\n",
				       timing_step, step, ret);
				return ERROR;
			}

			error_count = data->er[data->data_cnt - 1].error_cnt;

			if (ctx->cfg.verbose_logging)
				printf("  Expansion scan: step=%d error_count=%d\n",
				       step, error_count);

			if (error_count != 0) {
				/* First bad point — boundary is one step back */
				*result = step + step_toward_center;
				if (ctx->cfg.verbose_logging)
					printf("  Boundary found at step=%d\n",
					       *result);
				return SUCCESS;
			}
		}
	}
}

/**
 * ufs_eom_slt_measure_eye_width() - Measure the horizontal eye opening for a
 *                                   single lane and record the result.
 * @ctx: EOM session context.
 * @lane: Lane index to measure.
 * @timing_center: Output parameter set to the computed timing centre step.
 *
 * Finds the left and right timing boundaries at voltage offset 0, computes
 * the eye width in UI using the hardware timing step size, and compares the
 * result against the M-PHY specification threshold for the current gear and
 * UniPro version.  Results are stored in @ctx->data.
 *
 * Return: SUCCESS on success, ERROR on failure.
 */
static int ufs_eom_slt_measure_eye_width(struct ufs_eom_context *ctx, int lane,
					 int *timing_center)
{
	struct ufs_eom_data *data = &ctx->data;
	struct ufs_eom_caps *caps = &ctx->caps;
	int left_timing = 0, right_timing = 0, eye_width_steps;
	float timing_step_size, eye_width, eye_width_threshold;
	int ret;

	ret = ufs_eom_slt_find_horizontal_eye_boundary(ctx, lane,
						       EOM_SLT_INIT_LEFT_TIMING,
						       caps->timing_max_steps,
						       0, &left_timing);
	if (ret) {
		pr_err("Failed to find left eye boundary for lane %d\n", lane);
		return ERROR;
	}

	ret = ufs_eom_slt_find_horizontal_eye_boundary(ctx, lane,
						       EOM_SLT_INIT_RIGHT_TIMING,
						       caps->timing_max_steps,
						       0, &right_timing);
	if (ret) {
		pr_err("Failed to find right eye boundary for lane %d\n", lane);
		return ERROR;
	}

	eye_width_steps = right_timing - left_timing + 1;
	*timing_center = left_timing + eye_width_steps / 2;

	/* Convert steps to UI */
	timing_step_size = (float)caps->timing_max_offset * 0.01f /
			   (float)caps->timing_max_steps;
	if (timing_step_size == 0.0f) {
		pr_err("Invalid timing_step_size (zero) for lane %d\n", lane);
		return ERROR;
	}

	eye_width = (float)eye_width_steps * timing_step_size;

	/* Select threshold from M-PHY spec based on gear and UniPro version */
	if (ctx->gear == UFS_HS_G4)
		eye_width_threshold = (caps->unipro_ver >= UFS_UNIPRO_VER_3) ?
				      EOM_T_EYE_HS_G4_RX_V6 : EOM_T_EYE_HS_G4_RX_V5;
	else /* UFS_HS_G5 */
		eye_width_threshold = (caps->unipro_ver >= UFS_UNIPRO_VER_3) ?
				      EOM_T_EYE_HS_G5_RX_V6 : EOM_T_EYE_HS_G5_RX_V5;

	data->slt_eye_width_ui[lane] = eye_width;
	data->slt_eye_width_steps[lane] = eye_width_steps;
	data->slt_eye_width_threshold[lane] = eye_width_threshold;
	data->slt_eye_center[lane] = *timing_center;

	if (ctx->cfg.verbose_logging)
		printf("Lane %d Eye Width: %d steps (left=%d, right=%d, "
		       "center=%d), step_size=%.4f UI\n",
		       lane, eye_width_steps, left_timing, right_timing,
		       *timing_center, timing_step_size);

	printf("UFS EOM SLT (PassEyeWidth %.2fUI): Lane %d Eye Width %.2fUI "
	       "(%d steps) - %s, Eye Center (timing step : %d)\n",
	       eye_width_threshold, lane, eye_width, eye_width_steps,
	       (eye_width > eye_width_threshold) ? "Pass" : "Fail",
	       *timing_center);

	return SUCCESS;
}

/**
 * ufs_eom_slt_measure_eye_height() - Measure the vertical eye opening for a
 *                                    single lane and record the result.
 * @ctx: EOM session context.
 * @lane: Lane index to measure.
 * @timing_center: Timing step at the horizontal eye centre, used as the fixed
 *                 timing offset during the vertical search.
 *
 * Finds the bottom and top voltage boundaries at @timing_center, computes the
 * eye height in mV using the hardware voltage step size, and compares the
 * result against the M-PHY specification threshold for the current gear and
 * UniPro version.  Results are stored in @ctx->data.
 *
 * Return: SUCCESS on success, ERROR on failure.
 */
static int ufs_eom_slt_measure_eye_height(struct ufs_eom_context *ctx, int lane,
					  int timing_center)
{
	struct ufs_eom_data *data = &ctx->data;
	struct ufs_eom_caps *caps = &ctx->caps;
	int init_bottom = ctx->caps.use_extended_vrange ?
			  EOM_SLT_INIT_BOTTOM_VOLT_EXT :
			  EOM_SLT_INIT_BOTTOM_VOLT;
	int init_top = ctx->caps.use_extended_vrange ?
		       EOM_SLT_INIT_TOP_VOLT_EXT :
		       EOM_SLT_INIT_TOP_VOLT;
	int bottom_voltage = 0, top_voltage = 0;
	int eye_height_steps;
	float voltage_step_size, eye_height, eye_height_threshold;
	int ret;

	ret = ufs_eom_slt_find_vertical_eye_boundary(ctx, lane,
						     init_bottom,
						     caps->voltage_max_steps,
						     timing_center,
						     &bottom_voltage);
	if (ret) {
		pr_err("Failed to find bottom eye boundary for lane %d\n", lane);
		return ERROR;
	}

	ret = ufs_eom_slt_find_vertical_eye_boundary(ctx, lane,
						     init_top,
						     caps->voltage_max_steps,
						     timing_center,
						     &top_voltage);
	if (ret) {
		pr_err("Failed to find top eye boundary for lane %d\n", lane);
		return ERROR;
	}

	eye_height_steps = top_voltage - bottom_voltage + 1;

	/* Convert steps to mV */
	voltage_step_size = (float)caps->voltage_max_offset * 10.0f /
			    (float)caps->voltage_max_steps;
	if (voltage_step_size == 0.0f) {
		pr_err("Invalid voltage_step_size (zero) for lane %d\n", lane);
		return ERROR;
	}

	eye_height = (float)eye_height_steps * voltage_step_size;

	/* Select threshold from M-PHY spec based on gear and UniPro version */
	if (ctx->gear == UFS_HS_G4)
		eye_height_threshold = (caps->unipro_ver >= UFS_UNIPRO_VER_3) ?
				       EOM_V_DIF_AC_HS_G4_RX_V6 :
				       EOM_V_DIF_AC_HS_G4_RX_V5;
	else /* UFS_HS_G5 */
		eye_height_threshold = (caps->unipro_ver >= UFS_UNIPRO_VER_3) ?
				       EOM_V_DIF_AC_HS_G5_RX_V6 :
				       EOM_V_DIF_AC_HS_G5_RX_V5;

	data->slt_eye_height_mv[lane] = eye_height;
	data->slt_eye_height_steps[lane] = eye_height_steps;
	data->slt_eye_height_threshold[lane] = eye_height_threshold;

	if (ctx->cfg.verbose_logging)
		printf("Lane %d Eye Height: %d steps (top=%d, bottom=%d), "
		       "step_size=%.4f mV\n",
		       lane, eye_height_steps, top_voltage, bottom_voltage,
		       voltage_step_size);

	printf("UFS EOM SLT (PassEyeHeight %.2fmV): Lane %d Eye Height "
	       "%.2fmV (%d steps) - %s\n",
	       eye_height_threshold, lane, eye_height, eye_height_steps,
	       (eye_height > eye_height_threshold) ? "Pass" : "Fail");

	return SUCCESS;
}

/**
 * ufs_eom_slt_scan_per_lane() - Run the full SLT eye measurement sequence for
 *                               a single lane.
 * @ctx: EOM session context.
 * @lane: Lane index to scan.
 *
 * Measures the horizontal eye width and, for M-PHY versions prior to V6.0
 * (UniPro < 1.6), also measures the vertical eye height at the timing centre.
 * For M-PHY V6.0 and later only the width check is required by the spec.
 *
 * Return: SUCCESS on success, ERROR on failure.
 */
static int ufs_eom_slt_scan_per_lane(struct ufs_eom_context *ctx, int lane)
{
	int timing_center = 0;
	int ret;

	if (ctx->cfg.verbose_logging)
		printf("Starting SLT EOM scan for lane %d...\n", lane);

	ret = ufs_eom_slt_measure_eye_width(ctx, lane, &timing_center);
	if (ret)
		return ret;

	/*
	 * M-PHY V6.0 (UniPro >= 1.6) does not require a voltage-axis check
	 * for the SLT verdict.
	 */
	if (ctx->caps.unipro_ver >= UFS_UNIPRO_VER_3)
		return SUCCESS;

	return ufs_eom_slt_measure_eye_height(ctx, lane, timing_center);
}

/**
 * ufs_eom_slt_scan() - Run the SLT eye screening for all configured lanes.
 * @ctx: EOM session context.
 *
 * Iterates over all lanes starting from @ctx->cfg.start_lane and calls
 * ufs_eom_slt_scan_per_lane() for each.  The eye monitor is disabled after
 * each lane regardless of the per-lane result.  SLT mode is only supported
 * for HS-G4 and HS-G5; higher gears return an error immediately.
 *
 * Return: SUCCESS if all lanes pass, ERROR if any lane fails or if the
 *         current gear is not supported.
 */
int ufs_eom_slt_scan(struct ufs_eom_context *ctx)
{
	struct ufs_eom_config *cfg = &ctx->cfg;
	int l, n, err, ret = 0;

	if (cfg->slt_mode && ctx->gear > UFS_HS_G5) {
		pr_err("EOM SLT is not supported for HS-G%d\n", ctx->gear);
		return ERROR;
	}

	if (cfg->verbose_logging)
		printf("Running EOM Scan SLT...\n");

	for (l = cfg->start_lane, n = ctx->data.num_lanes; n > 0; n--, l++) {
		err = ufs_eom_slt_scan_per_lane(ctx, l);
		if (err) {
			pr_err("EOM SLT scan failed for lane %d\n", l);
			ret |= err;
		}

		ufs_eom_disable_eye_monitor(ctx, l);
	}

	return ret;
}
