// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "ufs_eom.h"
#include "ufs_eom_qcom.h"
#include "common.h"
#include "query.h"
#include "uic.h"

#define EOM_VSTEP_WA_SYSFS_PATH \
	"/sys/devices/platform/soc/1d84000.ufshc/qcom/eom_vstep"
#define EOM_VSTEP_CLEAR_SYSFS_PATH \
	"/sys/devices/platform/soc/1d84000.ufshc/qcom/eom_vstep_clear"

static int vstep_wa_fd = -1;
static int vstep_clear_fd = -1;

/**
 * ufs_eom_qcom_init() - Initialize Qualcomm-specific EOM extensions.
 * @ctx: EOM session context whose capability fields will be updated.
 *
 * Return: SUCCESS on success, ERROR if the sysfs node cannot be opened.
 */
int ufs_eom_qcom_init(struct ufs_eom_context *ctx)
{
	struct ufs_eom_caps *caps = &ctx->caps;

	if (caps->unipro_ver >= UFS_UNIPRO_VER_3 &&
	    (!(caps->rx_eyemon_cap & EOM_CAP_EXTENDED_VOLTAGE))) {
		/* Override voltage_max_steps */
		caps->voltage_max_steps = 127;
		caps->use_extended_vrange = true;

		vstep_wa_fd = open(EOM_VSTEP_WA_SYSFS_PATH, O_WRONLY);
		if (vstep_wa_fd < 0) {
			pr_err("Failed to open file %s (%d).\n",
			       EOM_VSTEP_WA_SYSFS_PATH, vstep_wa_fd);
			return ERROR;
		}
	}

	return SUCCESS;
}

/**
 * ufs_eom_qcom_exit() - Release resources acquired by ufs_eom_qcom_init().
 * @ctx: EOM session context.
 *
 * Closes the sysfs voltage-step workaround file descriptor if it was opened.
 */
void ufs_eom_qcom_exit(struct ufs_eom_context *ctx)
{
	int start_lane = ctx->cfg.start_lane;
	int num_lanes = ctx->data.num_lanes;
	int lane;
	char vstep_clear_str[U32_TO_STR_SIZE_MAX];

	if (vstep_wa_fd >= 0) {
		close(vstep_wa_fd);

		vstep_clear_fd = open(EOM_VSTEP_CLEAR_SYSFS_PATH, O_WRONLY);
		if (vstep_clear_fd < 0) {
			pr_err("Failed to open file %s (%d).\n",
			       EOM_VSTEP_CLEAR_SYSFS_PATH, vstep_clear_fd);
			return;
		}

		for (lane = start_lane; lane < start_lane + num_lanes; lane++) {
			if (u32_to_str(lane, vstep_clear_str,
				       sizeof(vstep_clear_str))) {
				pr_err("Failed to convert lane %d to string\n", lane);
				return;
			}
			write(vstep_clear_fd, vstep_clear_str, strlen(vstep_clear_str));
		}

		close(vstep_clear_fd);
	}
}

/**
 * ufs_eom_qcom_set_vstep() - Set voltage steps via kernel sysfs.
 * @vstep: Encoded voltage step value to program.
 * @lane: Lane index to associate with the voltage step.
 *
 * Return: SUCCESS on success, INVAL if the workaround is inactive, ERROR on
 *         failure.
 */
int ufs_eom_qcom_set_vstep(__u32 vstep, int lane)
{
	char vstep_str[U32_TO_STR_SIZE_MAX];
	int ret;

	if (vstep_wa_fd >= 0) {
		ret = u32_to_str(RX_EYEMON_VSTEP_WA_ENCODE(vstep, lane),
				 vstep_str, sizeof(vstep_str));
		if (ret) {
			pr_err("Failed to convert vstep %d to string\n", vstep);
			return ERROR;
		}

		ret = write(vstep_wa_fd, vstep_str, strlen(vstep_str));
		if (ret < 0) {
			pr_err("Failed to write vstep workaround\n");
			return ERROR;
		}
		return SUCCESS;
	}

	return INVAL;
}

/**
 * ufs_eom_qcom_write_json_log() - Emit JSON eye-diagram data lines for one lane.
 * @f: Output file to write to.
 * @data: EOM scan results containing all lanes.
 * @lane_num: Lane index whose entries should be written.
 */
static void ufs_eom_qcom_write_json_log(FILE *f, const struct ufs_eom_data *data, int lane_num)
{
	bool first_line = true;
	int i;

	for (i = 0; i < data->data_cnt; i++) {
		if (data->er[i].lane != lane_num)
			continue;

		if (!first_line)
			fprintf(f, ",\n");
		fprintf(f, "[EOMREPORTLIB]                     [%d, %d, %d]",
			data->er[i].timing,
			data->er[i].volt,
			data->er[i].error_cnt);
		first_line = false;
	}

	if (!first_line)
		fprintf(f, "\n");
}

/**
 * ufs_eom_qcom_generate_json_report() - Generate a JSON report for a completed EOM scan.
 * @ctx: EOM session context containing scan results and capabilities.
 * @output_file: Base path for the output file; ".json" will be appended.
 * @mname: Manufacturer name string from the UFS device descriptor.
 * @pname: Product name string from the UFS device descriptor.
 * @pver: Product revision level string from the UFS device descriptor.
 *
 * Return: SUCCESS on success, ERROR on failure.
 */
int ufs_eom_qcom_generate_json_report(struct ufs_eom_context *ctx,
				      const char *output_file,
				      const char *mname, const char *pname,
				      const char *pver)
{
	const struct ufs_eom_data *data = &ctx->data;
	const struct ufs_eom_caps *caps = &ctx->caps;
	float timing_step_size, voltage_step_size;
	int l, n, start_lane;
	char json_file[1024];
	FILE *f;

	snprintf(json_file, sizeof(json_file), "%s.json", output_file);
	f = fopen(json_file, "w");
	if (!f) {
		pr_err("Failed to create JSON file %s\n", json_file);
		return ERROR;
	}

	timing_step_size  = (float)caps->timing_max_offset * 0.01f /
			    (float)caps->timing_max_steps;
	voltage_step_size = (float)caps->voltage_max_offset * 10.0f /
			    (float)caps->voltage_max_steps;

	/* Determine the first lane index */
	start_lane = (data->data_cnt > 0) ? data->er[0].lane : 0;

	/* Header text (non-JSON lines, ignored by downstream parsers) */
	fprintf(f, "EOM Capabilities:\n");
	fprintf(f, "TimingMaxSteps %d TimingMaxOffset %d\n",
		caps->timing_max_steps, caps->timing_max_offset);
	fprintf(f, "VoltageMaxSteps %d VoltageMaxOffset %d\n\n",
		caps->voltage_max_steps, caps->voltage_max_offset);
	fprintf(f, "UFS Eye Monitor - Data Collection In Progress\n");

	/* JSON content — every line prefixed with [EOMREPORTLIB] */
	fprintf(f, "[EOMREPORTLIB]{\n");
	fprintf(f, "[EOMREPORTLIB]   \"version\": \"1.0.0\",\n");
	fprintf(f, "[EOMREPORTLIB]   \"results\": [\n");
	fprintf(f, "[EOMREPORTLIB]         {\n");
	fprintf(f, "[EOMREPORTLIB]            \"interface\": \"%s UFS\",\n",
		ctx->cfg.local_peer ? "Device" : "Host");
	fprintf(f, "[EOMREPORTLIB]            \"instance\": 0,\n");
	fprintf(f, "[EOMREPORTLIB]            \"time_scale\": %f,\n", timing_step_size);
	fprintf(f, "[EOMREPORTLIB]            \"time_units\": \"UI\",\n");
	fprintf(f, "[EOMREPORTLIB]            \"voltage_scale\": %f,\n", voltage_step_size);
	fprintf(f, "[EOMREPORTLIB]            \"voltage_units\": \"mV\",\n");
	fprintf(f, "[EOMREPORTLIB]            \"note\": \"%s %s %s\",\n", mname, pname, pver);
	fprintf(f, "[EOMREPORTLIB]            \"lanes\": [\n");

	for (l = start_lane, n = data->num_lanes; n > 0; n--, l++) {
		fprintf(f, "[EOMREPORTLIB]                  {\n");
		fprintf(f, "[EOMREPORTLIB]                  \"lane_number\": %d,\n", l);
		fprintf(f, "[EOMREPORTLIB]                  \"note\": \"\",\n");
		fprintf(f, "[EOMREPORTLIB]                  \"eye\": [\n");
		ufs_eom_qcom_write_json_log(f, data, l);
		fprintf(f, "[EOMREPORTLIB]                     ]\n");
		fprintf(f, "[EOMREPORTLIB]                  }%s\n", n > 1 ? "," : "");
	}

	fprintf(f, "[EOMREPORTLIB]               ]\n");
	fprintf(f, "[EOMREPORTLIB]         }\n");
	fprintf(f, "[EOMREPORTLIB]      ]\n");
	fprintf(f, "[EOMREPORTLIB]}\n");

	fclose(f);
	printf("EOM results saved to %s\n", json_file);

	return SUCCESS;
}
