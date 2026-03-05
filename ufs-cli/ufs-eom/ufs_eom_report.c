// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include <stdio.h>
#include <string.h>
#include "common.h"
#include "query.h"
#include "uic.h"
#include "ufs_eom.h"

static int ufs_eom_write_report(const char *output_file,
				struct ufs_eom_context *ctx,
				const char *mname, const char *pname,
				const char *pver)
{
	const struct ufs_eom_data *data = &ctx->data;
	const struct ufs_eom_caps *caps = &ctx->caps;
	FILE *f;
	int i;

	f = fopen(output_file, "w");
	if (!f) {
		pr_err("Failed to create EOM result file %s\n", output_file);
		return ERROR;
	}

	fprintf(f, "UFS %s Side Eye Monitor Start\n",
		ctx->cfg.local_peer ? "Device" : "Host");
	fprintf(f, "- - - - UFS INQUIRY ID: %s %s %s\n", mname, pname, pver);
	fprintf(f, "- - - - UFS Gear Speed: HS-G%d Rate-%c\n",
		ctx->gear,
		ctx->rate == PA_HS_MODE_A ? 'A' : 'B');
	fprintf(f, "EOM Capabilities:\n");
	fprintf(f, "TimingMaxSteps %d TimingMaxOffset %d\n",
		caps->timing_max_steps, caps->timing_max_offset);
	fprintf(f, "VoltageMaxSteps %d VoltageMaxOffset %d\n\n",
		caps->voltage_max_steps, caps->voltage_max_offset);

	for (i = 0; i < data->data_cnt; i++)
		fprintf(f, "lane: %d timing: %d voltage: %d error count: %d\n",
			data->er[i].lane, data->er[i].timing,
			data->er[i].volt, data->er[i].error_cnt);

	if (ctx->cfg.slt_mode) {
		int l, n;

		for (l = ctx->cfg.start_lane, n = data->num_lanes; n > 0; n--, l++) {
			fprintf(f, "UFS EOM SLT (PassEyeWidth %.2fUI): "
				   "Lane %d Eye Width %.2fUI (%d steps) - %s, "
				   "Eye Center (timing step : %d)\n",
				data->slt_eye_width_threshold[l], l,
				data->slt_eye_width_ui[l],
				data->slt_eye_width_steps[l],
				(data->slt_eye_width_ui[l] <
				 data->slt_eye_width_threshold[l])
				? "Fail" : "Pass",
				data->slt_eye_center[l]);

			if (caps->unipro_ver < UFS_UNIPRO_VER_3) {
				fprintf(f, "UFS EOM SLT (PassEyeHeight %.2fmV): "
					   "Lane %d Eye Height %.2fmV (%d steps) - %s\n",
					data->slt_eye_height_threshold[l], l,
					data->slt_eye_height_mv[l],
					data->slt_eye_height_steps[l],
					(data->slt_eye_height_mv[l] <
					 data->slt_eye_height_threshold[l])
					? "Fail" : "Pass");
			}
		}
		fprintf(f, "\n");
	}

	fclose(f);
	printf("EOM results saved to %s\n", output_file);

	return SUCCESS;
}

/**
 * ufs_eom_generate_reports() - Generate output report files for a completed
 *                              EOM scan.
 * @ctx: EOM session context containing scan results.
 * @output_file: Full path of the output report file to create.
 *
 * Return: SUCCESS on success, ERROR on failure.
 */
int ufs_eom_generate_reports(struct ufs_eom_context *ctx,
			     const char *output_file)
{
	char mname[MANUFACTURER_NAME_STRING_DESC_SIZE];
	char pname[PRODUCT_NAME_STRING_DESC_SIZE];
	char pver[PRODUCT_REVISION_LEVEL_STRING_DESC_SIZE];
	int ret;

	ret = ufs_eom_get_device_info(ctx->bsg_fd, mname, pname, pver);
	if (ret) {
		pr_err("Failed to read device info for report\n");
		return ret;
	}

	ret = ufs_eom_write_report(output_file, ctx, mname, pname, pver);
	if (ret) {
		pr_err("Failed to write EOM report\n");
		return ret;
	}

	return SUCCESS;
}
