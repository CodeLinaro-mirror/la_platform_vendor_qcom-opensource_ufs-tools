// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <malloc.h>
#include <sys/stat.h>
#include "ufs_eom_slt.h"
#include "ufs_eom_qcom.h"
#include "common.h"
#include "query.h"
#include "uic.h"
#include "ufs_eom.h"

/* Temporary file written during I/O stress runs */
static const char *EOM_TMP_FILENAME = "ufseom_tmp_data";

/* Global aligned I/O buffer used during EOM stress runs */
static char *ufs_eom_tmp_buf;

static const char *ufseom_help =
	"\nufseom cli:\n\n"
	"ufseom [-p|--peer | -l|--local] [-D|--data] [--slt]\n"
	"       [-L|--lane <lane>] [--voltage-low <v>] [--voltage-high <v>]\n"
	"       [--timing-left <t>] [--timing-right <t>]\n"
	"       [-t|--target <count>] [-o|--output <path>] [-d|--device <dev>]\n"
	"       [-V|--verbose]\n\n"
	"Options:\n"
	"  -h               Show this help message\n"
	"  --version        Print ufseom version\n"
	"  -p, --peer       Measure peer (UFS device) Rx\n"
	"  -l, --local      Measure local (UFS host) Rx\n"
	"  -D, --data       Stress the link with random I/O while scanning\n"
	"  -L, --lane       Lane number (0 or 1); omit to scan all lanes\n"
	"  --voltage-low    Lower voltage bound (default: -voltage_max_steps)\n"
	"  --voltage-high   Upper voltage bound (default: +voltage_max_steps)\n"
	"  --timing-left    Left timing bound (default: -timing_max_steps)\n"
	"  --timing-right   Right timing bound (default: +timing_max_steps)\n"
	"  --slt            Run EOM STL screening\n"
	"  -t, --target     Target test count per point (default: 0x5D)\n"
	"  -o, --output     Directory for output files (must end with '/')\n"
	"  -d, --device     Path to UFS BSG device (e.g. /dev/ufs-bsg0)\n"
	"  -V, --verbose    Print detailed scan progress\n\n"
	"Examples:\n"
	"  # Scan local Rx with I/O stress:\n"
	"  ufseom -l -D -o /data/ -d /dev/ufs-bsg0\n\n"
	"  # Scan peer Rx with I/O stress::\n"
	"  ufseom -p -D -o /data/ -d /dev/ufs-bsg0\n\n"
	"  # Run EOM STL screening:\n"
	"  ufseom -l -D --slt -o /data/ -d /dev/ufs-bsg0\n\n"
	"Note: disable UFS low-power features before running EOM:\n"
	"  echo 0 > /sys/devices/<path>/*.ufshc/clkscale_enable\n"
	"  echo 0 > /sys/devices/<path>/*.ufshc/clkgate_enable\n"
	"  echo 0 > /sys/devices/<path>/*.ufshc/auto_hibern8\n"
	"  echo on > /sys/bus/scsi/devices/*/power/control\n"
	"Reboot the system after use.\n";

static const char *short_opts = "plDL:t:o:d:V";

enum long_opt_ids {
	OPT_VOLTAGE_LOW = 1,
	OPT_VOLTAGE_HIGH = 2,
	OPT_TIMING_LEFT = 3,
	OPT_TIMING_RIGHT = 4,
	OPT_SLT = 5,
};

static struct option long_opts[] = {
	{"peer", no_argument, NULL, 'p'},
	{"local", no_argument, NULL, 'l'},
	{"data", no_argument, NULL, 'D'},
	{"lane", required_argument, NULL, 'L'},
	{"target", required_argument, NULL, 't'},
	{"output", required_argument, NULL, 'o'},
	{"device", required_argument, NULL, 'd'},
	{"verbose", no_argument, NULL, 'V'},
	{"voltage-low", required_argument, NULL, OPT_VOLTAGE_LOW},
	{"voltage-high", required_argument, NULL, OPT_VOLTAGE_HIGH},
	{"timing-left", required_argument, NULL, OPT_TIMING_LEFT},
	{"timing-right", required_argument, NULL, OPT_TIMING_RIGHT},
	{"slt", no_argument, NULL, OPT_SLT},
	{NULL, 0, NULL, 0}
};

/**
 * ufs_eom_parse_int_arg() - Parse the current optarg as a signed integer.
 * @out: Pointer to store the parsed integer value.
 *
 * Return: SUCCESS on success, ERROR if the argument is not a valid integer.
 */
static int ufs_eom_parse_int_arg(int *out)
{
	char *end;

	*out = (int)strtol(optarg, &end, 0);
	if (*end != '\0') {
		pr_err("Invalid numeric argument: '%s'\n", optarg);
		return ERROR;
	}

	if (*out == 0 && optarg[0] != '0' && optarg[0] != '-') {
		pr_err("Invalid numeric argument: '%s'\n", optarg);
		return ERROR;
	}

	return SUCCESS;
}

/**
 * ufs_eom_parse_lane_arg() - Parse and validate the lane number argument.
 * @cfg: EOM configuration to update with the parsed lane number.
 *
 * Return: SUCCESS on success, ERROR if the lane number is invalid.
 */
static int ufs_eom_parse_lane_arg(struct ufs_eom_config *cfg)
{
	int lane, ret;

	ret = get_value_from_cli(&lane);
	if (ret || lane < 0 || lane > 1) {
		pr_err("Invalid lane number (must be 0 or 1)\n");
		return ERROR;
	}
	cfg->start_lane = lane;

	return SUCCESS;
}

/**
 * ufs_eom_parse_target_count_arg() - Parse and validate the target test count
 *                                    argument.
 * @cfg: EOM configuration to update with the parsed target test count.
 *
 * Return: SUCCESS on success, ERROR if the value is out of range.
 */
static int ufs_eom_parse_target_count_arg(struct ufs_eom_config *cfg)
{
	int t, ret;

	ret = get_value_from_cli(&t);
	if (ret || t <= 0 || t > EOM_TARGET_TEST_COUNT_MAX) {
		pr_err("Invalid target test count (must be 1..%d)\n",
		       EOM_TARGET_TEST_COUNT_MAX);
		return ERROR;
	}

	cfg->target_test_count = t;

	return SUCCESS;
}

/**
 * ufs_eom_check_output_path() - Validate the output directory path.
 * @path: Output path string to validate.
 *
 * Return: SUCCESS on success, ERROR if the path is invalid.
 */
static int ufs_eom_check_output_path(const char *path)
{
	size_t len = strlen(path);
	size_t i;

	for (i = 0; i < len; i++) {
		if (path[i] == ' ') {
			pr_err("Output path must not contain spaces\n");
			return ERROR;
		}
	}

	if (path[len - 1] != '/') {
		pr_err("Output path must end with '/'\n");
		return ERROR;
	}

	return SUCCESS;
}

/**
 * ufs_eom_parse_args() - Parse and validate all command-line arguments.
 * @argc: Argument count passed to main().
 * @argv: Argument vector passed to main().
 * @cfg: EOM configuration structure to populate.
 *
 * Return: SUCCESS on success, ERROR on invalid or missing arguments.
 */
static int ufs_eom_parse_args(int argc, char *argv[], struct ufs_eom_config *cfg)
{
	int c, idx, ret = ERROR;

	if (argc < 2) {
		pr_err("Too few arguments. Try 'ufseom -h'.\n");
		return ERROR;
	}

	if (!strcmp(argv[1], "--version")) {
		printf("ufseom version %s\n", EOM_VERSION);
		return ERROR;
	}

	if (!strcmp(argv[1], "-h")) {
		printf("%s\n", ufseom_help);
		return ERROR;
	}

	while ((c = getopt_long(argc, argv, short_opts, long_opts, &idx)) != -1) {
		switch (c) {
		case 'p':
			cfg->local_peer = PEER;
			ret = SUCCESS;
			break;
		case 'l':
			cfg->local_peer = LOCAL;
			ret = SUCCESS;
			break;
		case 'D':
			cfg->generate_io = true;
			ret = SUCCESS;
			break;
		case 'V':
			cfg->verbose_logging = true;
			ret = SUCCESS;
			break;
		case 'L':
			ret = ufs_eom_parse_lane_arg(cfg);
			break;
		case 't':
			ret = ufs_eom_parse_target_count_arg(cfg);
			break;
		case 'o':
			ret = init_device_path(cfg->output_path);
			break;
		case 'd':
			ret = init_device_path(cfg->device_path);
			break;
		case OPT_VOLTAGE_LOW:
			ret = ufs_eom_parse_int_arg(&cfg->voltage_low);
			break;
		case OPT_VOLTAGE_HIGH:
			ret = ufs_eom_parse_int_arg(&cfg->voltage_high);
			break;
		case OPT_TIMING_LEFT:
			ret = ufs_eom_parse_int_arg(&cfg->timing_left);
			break;
		case OPT_TIMING_RIGHT:
			ret = ufs_eom_parse_int_arg(&cfg->timing_right);
			break;
		case OPT_SLT:
			cfg->slt_mode = true;
			ret = SUCCESS;
			break;
		default:
			pr_err("Unknown option. Try 'ufseom -h'.\n");
			return ERROR;
		}

		if (ret)
			return ret;
	}

	/* Mandatory checks */
	if (cfg->local_peer == INIT) {
		pr_err("Must specify -l (local) or -p (peer)\n");
		return ERROR;
	}

	if (cfg->device_path[0] == '\0') {
		pr_err("Must specify -d <device>\n");
		return ERROR;
	}

	if (cfg->output_path[0] == '\0') {
		pr_err("Must specify -o <output path>\n");
		return ERROR;
	}

	if (ufs_eom_check_output_path(cfg->output_path))
		return ERROR;

	if (cfg->target_test_count == INIT) {
		cfg->target_test_count = EOM_TARGET_TEST_COUNT_DEFAULT;
		pr_err("Target test count not specified; using default %d\n",
		       cfg->target_test_count);
	}

	return SUCCESS;
}

/**
 * ufs_eom_parse_string_desc() - Extract an ASCII string from a UFS string
 *                               descriptor buffer.
 * @buf: Descriptor buffer.
 * @out: Output buffer to store the extracted string.
 *
 * Return: SUCCESS on success, ERROR if @buf or @out is NULL.
 */
static int ufs_eom_parse_string_desc(const __u8 *buf, char *out)
{
	int len, i, j;

	if (!buf || !out)
		return ERROR;

	len = buf[0]; /* bLength field */
	for (i = 2, j = 0; i < len; i++) {
		if (buf[i])
			out[j++] = (char)buf[i];
	}

	out[j] = '\0';

	return SUCCESS;
}

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
			    char *pversion)
{
	__u8 desc_buf[DESCRIPTOR_BUFFER_SIZE] = {0};
	char str_buf[STRING_BUFFER_SIZE];
	int mname_idx, pname_idx, pver_idx, ret;
	size_t len;

	ret = query_read_descriptor(bsg_fd, DEVICE_DESCRIPTOR_IDN, 0, 0,
				    desc_buf, DESCRIPTOR_BUFFER_SIZE);
	if (ret) {
		pr_err("Failed to read Device Descriptor\n");
		return ret;
	}

	mname_idx = desc_buf[MANUFACTURER_NAME_OFFSET];
	pname_idx = desc_buf[PRODUCT_NAME_OFFSET];
	pver_idx = desc_buf[PRODUCT_REVISION_LEVEL_OFFSET];

	ret = query_read_descriptor(bsg_fd, STRING_DESCRIPTOR_IDN, mname_idx,
				    0, desc_buf, DESCRIPTOR_BUFFER_SIZE);
	if (ret) {
		pr_err("Failed to read Manufacturer Name String Descriptor\n");
		return ret;
	}

	memset(str_buf, 0, STRING_BUFFER_SIZE);
	ufs_eom_parse_string_desc(desc_buf, str_buf);
	len = strlcpy(mname, str_buf, MANUFACTURER_NAME_STRING_DESC_SIZE);
	if (len >= MANUFACTURER_NAME_STRING_DESC_SIZE) {
		pr_err("Truncation occurred. Need %zu bytes but mname is %zu bytes.\n",
		       len, MANUFACTURER_NAME_STRING_DESC_SIZE);
		return ERROR;
	}

	ret = query_read_descriptor(bsg_fd, STRING_DESCRIPTOR_IDN, pname_idx,
				    0, desc_buf, DESCRIPTOR_BUFFER_SIZE);
	if (ret) {
		pr_err("Failed to read Product Name String Descriptor\n");
		return ret;
	}

	memset(str_buf, 0, STRING_BUFFER_SIZE);
	ufs_eom_parse_string_desc(desc_buf, str_buf);
	len = strlcpy(pname, str_buf, PRODUCT_NAME_STRING_DESC_SIZE);
	if (len >= PRODUCT_NAME_STRING_DESC_SIZE) {
		pr_err("Truncation occurred. Need %zu bytes but pname is %zu bytes.\n",
		       len, PRODUCT_NAME_STRING_DESC_SIZE);
		return ERROR;
	}

	ret = query_read_descriptor(bsg_fd, STRING_DESCRIPTOR_IDN, pver_idx,
				    0, desc_buf, DESCRIPTOR_BUFFER_SIZE);
	if (ret) {
		pr_err("Failed to read Product Revision Level String Descriptor\n");
		return ret;
	}

	memset(str_buf, 0, STRING_BUFFER_SIZE);
	ufs_eom_parse_string_desc(desc_buf, str_buf);
	len = strlcpy(pversion, str_buf, PRODUCT_REVISION_LEVEL_STRING_DESC_SIZE);
	if (len >= PRODUCT_REVISION_LEVEL_STRING_DESC_SIZE) {
		pr_err("Truncation occurred. Need %zu bytes but pversion is %zu bytes.\n",
		       len, PRODUCT_REVISION_LEVEL_STRING_DESC_SIZE);
		return ERROR;
	}

	return SUCCESS;
}

/**
 * ufs_eom_read_capabilities() - Read EOM hardware capabilities from M-PHY
 *                               registers and populate the caps structure.
 * @ctx: EOM session context.
 *
 * Return: SUCCESS on success, ERROR on failure.
 */
static int ufs_eom_read_capabilities(struct ufs_eom_context *ctx)
{
	struct ufs_eom_caps *caps = &ctx->caps;
	struct ufs_eom_config *cfg = &ctx->cfg;
	int eom_cap, unipro_verinfo, unipro_ver;

	eom_cap = uic_get(ctx->bsg_fd,
			  UIC_ARG_MIB_SEL(RX_EYEMON_CAPABILITY, SELECT_RX(cfg->start_lane)),
			  cfg->local_peer);
	if (eom_cap < 0) {
		pr_err("Failed to read RX_EYEMON_Capability\n");
		return ERROR;
	}

	if (!(eom_cap & 0x1)) {
		pr_err("EOM is not supported\n");
		return ERROR;
	}

	if (eom_cap & EOM_CAP_EXTENDED_VOLTAGE)
		ctx->caps.use_extended_vrange = true;

	caps->rx_eyemon_cap = eom_cap;
	ctx->gear = uic_get(ctx->bsg_fd, UIC_ARG_MIB_SEL(PA_RXGEAR, SELECT_RX(0)), 0);
	if (ctx->gear < 0) {
		pr_err("Failed to get current gear from PA_RXGEAR\n");
		return ERROR;
	}

	if (ctx->gear < EOM_SUPPORTED_MIN_GEAR) {
		pr_err("EOM is not supported at current gear %d\n", ctx->gear);
		return ERROR;
	}

	ctx->rate = uic_get(ctx->bsg_fd, UIC_ARG_MIB_SEL(RX_HSRATE_SERIES, SELECT_RX(0)), 0);
	if (ctx->rate < 0) {
		pr_err("Failed to get current rate from RX_HSRATE_Series\n");
		return ERROR;
	}

	caps->timing_max_steps = uic_get(ctx->bsg_fd,
					 UIC_ARG_MIB_SEL(RX_EYEMON_TIMING_MAX_STEPS_CAPABILITY,
							 SELECT_RX(cfg->start_lane)),
					 cfg->local_peer);
	if (caps->timing_max_steps < 0) {
		pr_err("Failed to get RX_EYEMON_Timing_MAX_Steps_Capability\n");
		return ERROR;
	}

	caps->timing_max_offset = uic_get(ctx->bsg_fd,
					  UIC_ARG_MIB_SEL(RX_EYEMON_TIMING_MAX_OFFSET_CAPABILITY,
							  SELECT_RX(cfg->start_lane)),
					  cfg->local_peer);
	if (caps->timing_max_offset < 0) {
		pr_err("Failed to get RX_EYEMON_Timing_MAX_Offset_Capability\n");
		return ERROR;
	}

	caps->voltage_max_steps = uic_get(ctx->bsg_fd,
					  UIC_ARG_MIB_SEL(RX_EYEMON_VOLTAGE_MAX_STEPS_CAPABILITY,
							  SELECT_RX(cfg->start_lane)),
					  cfg->local_peer);
	if (caps->voltage_max_steps < 0) {
		pr_err("Failed to get RX_EYEMON_Voltage_MAX_Steps_Capability\n");
		return ERROR;
	}

	caps->voltage_max_offset = uic_get(ctx->bsg_fd,
					   UIC_ARG_MIB_SEL(RX_EYEMON_VOLTAGE_MAX_OFFSET_CAPABILITY,
							   SELECT_RX(cfg->start_lane)),
					   cfg->local_peer);
	if (caps->voltage_max_offset < 0) {
		pr_err("Failed to get RX_EYEMON_Voltage_MAX_Offset_Capability\n");
		return ERROR;
	}

	unipro_verinfo = uic_get(ctx->bsg_fd,
				 UIC_ARG_MIB_SEL(PA_LOCALVERINFO, SELECT_RX(0)),
				 cfg->local_peer);
	if (unipro_verinfo < 0) {
		pr_err("Failed to get PA_LOCALVERINFO\n");
		return ERROR;
	}

	unipro_ver = unipro_verinfo & UFS_UNIPRO_VER_MASK;
	caps->unipro_ver = unipro_ver;

	if (cfg->verbose_logging) {
		printf("PA_RxGear: %d\n", ctx->gear);
		printf("RX_HSRATE_Series: %d\n", ctx->rate);
		printf("EOM Capabilities:\n");
		printf("  TimingMaxSteps  %d  TimingMaxOffset  %d\n",
		       caps->timing_max_steps, caps->timing_max_offset);
		printf("  VoltageMaxSteps %d  VoltageMaxOffset %d\n",
		       caps->voltage_max_steps, caps->voltage_max_offset);
	}

	return SUCCESS;
}

/**
 * ufs_eom_populate_data_pattern() - Fill a buffer with pseudo-random data for
 *                                   I/O stress.
 * @buffer: Buffer to fill; must be at least EOM_TEMP_DATA_SIZE bytes.
 */
static void ufs_eom_populate_data_pattern(char *buffer)
{
	unsigned int count = EOM_TEMP_DATA_SIZE / sizeof(uint32_t);
	unsigned int i;
	uint64_t seed = (uint64_t)rand();
	uint32_t *buf = (uint32_t *)buffer;

	for (i = 0; i < count; i++)
		buf[i] = (uint32_t)(fast_rand64(&seed) & 0xFFFFFFFF);
}

/**
 * ufs_eom_do_io_stress() - Write and optionally read back a random data block
 *                          to stress the UFS link during an EOM scan.
 * @ctx: EOM session context.
 *
 * Return: SUCCESS on success, ERROR on I/O failure.
 */
static int ufs_eom_do_io_stress(struct ufs_eom_context *ctx)
{
	int ret;

	ufs_eom_populate_data_pattern(ufs_eom_tmp_buf);

	ret = pwrite(ctx->data_fd, ufs_eom_tmp_buf, EOM_TEMP_DATA_SIZE, 0);
	if (ret < 0) {
		pr_err("Failed to write tmp file\n");
		return ERROR;
	}

	if (ctx->cfg.local_peer == LOCAL) {
		ret = pread(ctx->data_fd, ufs_eom_tmp_buf, EOM_TEMP_DATA_SIZE, 0);
		if (ret < 0) {
			pr_err("Failed to read tmp file\n");
			return ERROR;
		}
	}

	return SUCCESS;
}

/**
 * ufs_eom_encode_steps() - Encode a signed voltage or timing value into the
 *                          M-PHY register format.
 * @val: Signed step value to encode.
 * @use_extended: True if the extended (7-bit) step format should be used.
 *
 * Return: Encoded register value with direction bit and magnitude.
 */
static __u32 ufs_eom_encode_steps(int val, bool use_extended)
{
	int dir_shift = use_extended ? EOM_DIRECTION_SHIFT_EXT : EOM_DIRECTION_SHIFT;
	__u32 step_mask = use_extended ? EOM_STEP_MASK_EXT : EOM_STEP_MASK;
	__u32 dir = (val < 0) ? 1 : 0;
	__u32 step = (val < 0) ? ((__u32)-val) : ((__u32)val);

	return (dir << dir_shift) | (step & step_mask);
}

/**
 * ufs_eom_apply_steps() - Apply encoded voltage and timing steps.
 * @ctx: EOM session context.
 * @tstep: Encoded timing step value.
 * @vstep: Encoded voltage step value.
 * @lane: Lane index to configure.
 * @peer: 0 for local, non-zero for peer.
 *
 * Return: SUCCESS on success, ERROR on failure.
 */
static int ufs_eom_apply_steps(struct ufs_eom_context *ctx, __u32 tstep,
			       __u32 vstep, int lane, int peer)
{
	int ret;

	ret = uic_set(ctx->bsg_fd,
		      UIC_ARG_MIB_SEL(RX_EYEMON_TIMING_STEPS, SELECT_RX(lane)),
		      ATTR_SET_NOR, tstep, peer);
	if (ret) {
		pr_err("Failed to set RX_EYEMON_Timing_Steps\n");
		return ret;
	}

	if (peer == LOCAL) {
		ret = ufs_eom_qcom_set_vstep(vstep, lane);
		if (ret != INVAL)
			return ret;
	}

	ret = uic_set(ctx->bsg_fd,
		      UIC_ARG_MIB_SEL(RX_EYEMON_VOLTAGE_STEPS, SELECT_RX(lane)),
		      ATTR_SET_NOR, vstep, ctx->cfg.local_peer);
	if (ret) {
		pr_err("Failed to set RX_EYEMON_Voltage_Steps\n");
		return ret;
	}

	return SUCCESS;
}

/**
 * ufs_eom_config_and_start() - Program all EOM M-PHY attributes for a given point
 *                              and trigger a Power Mode Change to start the monitor.
 * @ctx: EOM session context.
 * @lane: Lane index to configure.
 * @tstep: Encoded timing step value.
 * @vstep: Encoded voltage step value.
 * @peer: 0 for local, non-zero for peer.
 *
 * Return: SUCCESS on success, ERROR on failure.
 */
static int ufs_eom_config_and_start(struct ufs_eom_context *ctx, int lane,
				    __u32 tstep, __u32 vstep, int peer)
{
	int target_test_count = ctx->cfg.target_test_count;
	int ret;

	ret = uic_set(ctx->bsg_fd,
		      UIC_ARG_MIB_SEL(RX_EYEMON_ENABLE, SELECT_RX(lane)),
		      ATTR_SET_NOR, 1, peer);
	if (ret) {
		pr_err("Failed to set RX_EYEMON_Enable\n");
		return ret;
	}

	ret = ufs_eom_apply_steps(ctx, tstep, vstep, lane, peer);
	if (ret)
		return ret;

	ret = uic_set(ctx->bsg_fd,
		      UIC_ARG_MIB_SEL(RX_EYEMON_TARGET_TEST_COUNT, SELECT_RX(lane)),
		      ATTR_SET_NOR, target_test_count, peer);
	if (ret) {
		pr_err("Failed to set RX_EYEMON_Target_Test_Count\n");
		return ret;
	}

	/* Select NO_ADAPT to avoid disturbing the link */
	ret = uic_set(ctx->bsg_fd,
		      UIC_ARG_MIB_SEL(PA_TXHSADAPTTYPE, SELECT_TX(0)),
		      ATTR_SET_NOR, PA_NO_ADAPT, 0);
	if (ret) {
		pr_err("Failed to set NO_ADAPT\n");
		return ret;
	}

	/* Power Mode Change to Fast Mode triggers an RCT that starts EOM */
	ret = uic_set(ctx->bsg_fd,
		      UIC_ARG_MIB_SEL(PA_PWRMODE, SELECT_TX(0)),
		      ATTR_SET_NOR, 0x11, 0);
	if (ret) {
		pr_err("Failed to trigger RCT via PA_PWRMODE\n");
		return ret;
	}

	/* Wait for the Power Mode Change to complete */
	while (1) {
		ret = uic_get(ctx->bsg_fd,
			      UIC_ARG_MIB_SEL(QCOM_DME_VS_UNIPRO_STATE,
					      SELECT_TX(0)), 0);
		if (ret < 0)
			break;
		if ((ret & QCOM_DME_VS_UNIPRO_STATE_MASK) ==
		    QCOM_DME_VS_UNIPRO_STATE_LINK_UP)
			break;
	}

	/* If DME_VS_UNIPRO_STATE is unsupported, wait a fixed interval */
	if (ret < 0)
		usleep(200000);

	return SUCCESS;
}

/**
 * ufs_eom_may_stop() - Check whether the current EOM measurement has
 *                      reached a terminal condition.
 * @ctx: EOM session context.
 * @tested_count: Number of symbols tested so far.
 * @error_count: Number of PHY errors observed so far.
 *
 * Return: SUCCESS if the measurement is complete and the result should be
 *         recorded, AGAIN if the measurement is still in progress, or ERROR
 *         if the result buffer is full.
 */
static int ufs_eom_may_stop(struct ufs_eom_context *ctx, int tested_count,
			    int error_count)
{
	struct ufs_eom_data *data = &ctx->data;
	int target_test_count = ctx->cfg.target_test_count;

	if (tested_count >= target_test_count - 3 ||
	    error_count >= EOM_PHY_ERROR_COUNT_THRESHOLD) {
		if (data->data_cnt >= ctx->eom_result_count) {
			pr_err("Result count exceeds maximum %d\n",
			       ctx->eom_result_count);
			return ERROR;
		}
		return SUCCESS;
	}
	return AGAIN;
}

/**
 * ufs_eom_scan_point() - Perform a single EOM measurement at a given lane, timing
 *                  and voltage offset.
 * @ctx: EOM session context.
 * @lane: Lane index to measure.
 * @timing: Signed timing offset to apply.
 * @volt: Signed voltage offset to apply.
 *
 * Return: SUCCESS on success, ERROR on failure.
 */
int ufs_eom_scan_point(struct ufs_eom_context *ctx, int lane, int timing, int volt)
{
	struct ufs_eom_data *data = &ctx->data;
	__u32 timing_steps, voltage_steps;
	int eom_start, eom_tested_count, eom_error_count, ret;

	timing_steps = ufs_eom_encode_steps(timing, false);
	voltage_steps = ufs_eom_encode_steps(volt, ctx->caps.use_extended_vrange);

	ret = ufs_eom_config_and_start(ctx, lane, timing_steps, voltage_steps,
				       ctx->cfg.local_peer);
	if (ret) {
		pr_err("Failed to configure EOM hardware\n");
		return ret;
	}

	while (1) {
		if (ctx->cfg.generate_io) {
			ret = ufs_eom_do_io_stress(ctx);
			if (ret)
				return ret;
		}

		eom_start = uic_get(ctx->bsg_fd,
				    UIC_ARG_MIB_SEL(RX_EYEMON_START,
						    SELECT_RX(lane)),
				    ctx->cfg.local_peer);
		if (eom_start < 0) {
			pr_err("Failed to get RX_EYEMON_Start\n");
			return ERROR;
		}

		/* EOM is still running, keep polling */
		if (eom_start & RX_EYEMON_START_MASK)
			continue;

		eom_tested_count = uic_get(ctx->bsg_fd,
					   UIC_ARG_MIB_SEL(RX_EYEMON_TESTED_COUNT,
							   SELECT_RX(lane)),
					   ctx->cfg.local_peer);
		if (eom_tested_count < 0) {
			pr_err("Failed to get RX_EYEMON_Tested_Count\n");
			return ERROR;
		}

		eom_error_count = uic_get(ctx->bsg_fd,
					  UIC_ARG_MIB_SEL(RX_EYEMON_ERROR_COUNT,
							  SELECT_RX(lane)),
					  ctx->cfg.local_peer);
		if (eom_error_count < 0) {
			pr_err("Failed to get RX_EYEMON_Error_Count\n");
			return ERROR;
		}

		ret = ufs_eom_may_stop(ctx, eom_tested_count, eom_error_count);
		if (ret != AGAIN) {
			if (ctx->cfg.verbose_logging)
				printf("lane: %d timing: %d voltage: %d error_cnt: %d [tested: %d]\n",
					lane, timing, volt, eom_error_count,
					eom_tested_count);

			data->er[data->data_cnt].lane = lane;
			data->er[data->data_cnt].timing = timing;
			data->er[data->data_cnt].volt = volt;
			data->er[data->data_cnt].error_cnt = eom_error_count;
			data->data_cnt++;

			return ret;
		}

		/* EOM has not yet kicked start, keep polling */
	}
}

/**
 * ufs_eom_disable_eye_monitor() - Disable the M-PHY eye monitor for a lane.
 * @ctx: EOM session context.
 * @lane: Lane index to disable.
 *
 * Return: SUCCESS on success, ERROR on failure.
 */
int ufs_eom_disable_eye_monitor(struct ufs_eom_context *ctx, int lane)
{
	int ret;

	ret = uic_set(ctx->bsg_fd,
		      UIC_ARG_MIB_SEL(RX_EYEMON_ENABLE, SELECT_RX(lane)),
		      ATTR_SET_NOR, 0, ctx->cfg.local_peer);
	if (ret) {
		pr_err("Failed to disable EOM for lane %d\n", lane);
		return ret;
	}

	return SUCCESS;
}

/**
 * ufs_eom_scan_range() - Scan the full configured timing/voltage range across
 *                        all requested lanes.
 * @ctx: EOM session context.
 *
 * Return: SUCCESS on success, ERROR on failure.
 */
static int ufs_eom_scan_range(struct ufs_eom_context *ctx)
{
	struct ufs_eom_config *cfg = &ctx->cfg;
	int l, n, t, v, ret;

	for (l = cfg->start_lane, n = ctx->data.num_lanes; n > 0; n--, l++) {
		for (t = cfg->timing_left; t <= cfg->timing_right; t++) {
			for (v = cfg->voltage_low; v <= cfg->voltage_high; v++) {
				ret = ufs_eom_scan_point(ctx, l, t, v);
				if (ret) {
					pr_err("EOM scan failed at lane %d "
					       "timing %d volt %d\n", l, t, v);
					ufs_eom_disable_eye_monitor(ctx, l);
					return ret;
				}
			}
		}

		ufs_eom_disable_eye_monitor(ctx, l);
	}

	return SUCCESS;
}

/**
 * ufs_eom_validate_range() - Validate and apply defaults for the scan range.
 * @ctx: EOM session context.
 *
 * Return: SUCCESS on success, ERROR if any range is invalid.
 */
static int ufs_eom_validate_range(struct ufs_eom_context *ctx)
{
	struct ufs_eom_config *cfg = &ctx->cfg;
	struct ufs_eom_caps *caps = &ctx->caps;

	if (cfg->voltage_low == EOM_TIMING_VOLTAGE_INIT)
		cfg->voltage_low = -caps->voltage_max_steps;
	if (cfg->voltage_high == EOM_TIMING_VOLTAGE_INIT)
		cfg->voltage_high = caps->voltage_max_steps;
	if (cfg->timing_left == EOM_TIMING_VOLTAGE_INIT)
		cfg->timing_left = -caps->timing_max_steps;
	if (cfg->timing_right == EOM_TIMING_VOLTAGE_INIT)
		cfg->timing_right = caps->timing_max_steps;

	if (cfg->voltage_low < -caps->voltage_max_steps ||
	    cfg->voltage_high > caps->voltage_max_steps ||
	    cfg->voltage_low > cfg->voltage_high) {
		pr_err("Invalid voltage range [%d, %d]; actual voltage range caps [-%d, %d]\n",
		       cfg->voltage_low, cfg->voltage_high,
		       caps->voltage_max_steps, caps->voltage_max_steps);
		return ERROR;
	}

	if (cfg->timing_left < -caps->timing_max_steps ||
	    cfg->timing_right > caps->timing_max_steps ||
	    cfg->timing_left > cfg->timing_right) {
		pr_err("Invalid timing range [%d, %d]; actual timing range caps [-%d, %d]\n",
		       cfg->timing_left, cfg->timing_right,
		       caps->timing_max_steps, caps->timing_max_steps);
		return ERROR;
	}

	if (cfg->verbose_logging)
		printf("Scan range: timing [%d, %d], voltage [%d, %d]\n",
		       cfg->timing_left, cfg->timing_right,
		       cfg->voltage_low, cfg->voltage_high);

	return SUCCESS;
}

/**
 * ufs_eom_alloc_memory_for_results() - Allocate the result array sized for the full scan
 *                                      range.
 * @ctx: EOM session context.
 *
 * Return: SUCCESS on success, ERROR if allocation fails.
 */
static int ufs_eom_alloc_memory_for_results(struct ufs_eom_context *ctx)
{
	struct ufs_eom_data *data = &ctx->data;
	size_t result_size;

	ctx->eom_result_count = (ctx->caps.timing_max_steps * 2 + 1) *
				(ctx->caps.voltage_max_steps * 2 + 1) *
				data->num_lanes;

	result_size = (size_t)ctx->eom_result_count * sizeof(struct ufs_eom_result);
	data->er = malloc(result_size);
	if (!data->er) {
		pr_err("Failed to allocate %zu bytes for EOM results\n",
		       result_size);
		return ERROR;
	}
	memset(data->er, 0, result_size);

	return SUCCESS;
}

/**
 * ufs_eom_setup_io() - Open the temporary I/O stress file and allocate the
 *                      aligned I/O buffer if I/O stress is enabled.
 * @ctx: EOM session context.
 *
 * Return: SUCCESS on success, ERROR on failure.
 */
static int ufs_eom_setup_io(struct ufs_eom_context *ctx)
{
	char tmp_file[DEVICE_PATH_NAME_SIZE_MAX + 64];

	if (!ctx->cfg.generate_io)
		return SUCCESS;

	snprintf(tmp_file, sizeof(tmp_file), "%s%s",
		 ctx->cfg.output_path, EOM_TMP_FILENAME);

	ctx->data_fd = open(tmp_file, O_RDWR | O_DIRECT | O_CREAT,
			    S_IWUSR | S_IRUSR);
	if (ctx->data_fd < 0) {
		pr_err("Failed to open tmp file %s\n", tmp_file);
		return ERROR;
	}

	ufs_eom_tmp_buf = memalign(EOM_TEMP_DATA_MEM_ALIGN_SIZE,
				   EOM_TEMP_DATA_SIZE);
	if (!ufs_eom_tmp_buf) {
		pr_err("Failed to allocate I/O buffer\n");
		return ERROR;
	}

	return SUCCESS;
}

/**
 * ufs_eom_format_output_filename() - Construct the output report file path.
 * @ctx: EOM session context.
 * @out: Output buffer to store the constructed path.
 * @out_size: Size of @out in bytes.
 */
static void ufs_eom_format_output_filename(const struct ufs_eom_context *ctx,
					   char *out, size_t out_size)
{
	const struct ufs_eom_config *cfg = &ctx->cfg;
	const struct ufs_eom_data *data = &ctx->data;
	char lane_str[16];

	if (data->num_lanes == 2)
		snprintf(lane_str, sizeof(lane_str), "0_1");
	else
		snprintf(lane_str, sizeof(lane_str), "%d", cfg->start_lane);

	snprintf(out, out_size, "%s%s%s_lane_%s_gear_%d_ttc_%d.eom",
		 cfg->output_path,
		 cfg->local_peer ? "peer" : "local",
		 cfg->slt_mode ? "_slt" : "",
		 lane_str,
		 ctx->gear,
		 cfg->target_test_count);
}

/**
 * ufs_eom_scan() - Execute the full EOM scan over the configured range.
 * @ctx: EOM session context.
 *
 * Return: SUCCESS on success, ERROR on failure.
 */
static int ufs_eom_scan(struct ufs_eom_context *ctx)
{
	if (ctx->cfg.slt_mode)
		return ufs_eom_slt_scan(ctx);

	return ufs_eom_scan_range(ctx);
}

/**
 * main() - Entry point for the ufseom command-line tool.
 * @argc: Argument count.
 * @argv: Argument vector.
 *
 * Return: SUCCESS on success, ERROR on failure.
 */
int main(int argc, char *argv[])
{
	struct ufs_eom_context *ctx;
	char output_file[DEVICE_PATH_NAME_SIZE_MAX + 256];
	struct timespec ts_start, ts_end;
	int ret;

	ctx = calloc(1, sizeof(*ctx));
	if (!ctx) {
		pr_err("Failed to allocate EOM context\n");
		return ERROR;
	}

	ctx->bsg_fd = -1;
	ctx->data_fd = -1;
	ctx->cfg.local_peer = INIT;
	ctx->cfg.start_lane = INIT;
	ctx->cfg.target_test_count = INIT;
	ctx->cfg.voltage_low = EOM_TIMING_VOLTAGE_INIT;
	ctx->cfg.voltage_high = EOM_TIMING_VOLTAGE_INIT;
	ctx->cfg.timing_left = EOM_TIMING_VOLTAGE_INIT;
	ctx->cfg.timing_right = EOM_TIMING_VOLTAGE_INIT;

	ret = ufs_eom_parse_args(argc, argv, &ctx->cfg);
	if (ret)
		goto cleanup;

	/* Determine number of lanes to scan */
	if (ctx->cfg.start_lane == INIT) {
		pr_err("Lane not specified, scanning all connected lanes\n");
		ctx->cfg.start_lane = 0;
		ctx->data.num_lanes = 2;
	} else {
		ctx->data.num_lanes = 1;
	}

	ctx->bsg_fd = open(ctx->cfg.device_path, O_RDWR);
	if (ctx->bsg_fd < 0) {
		pr_err("Failed to open BSG device %s\n", ctx->cfg.device_path);
		ret = ERROR;
		goto cleanup;
	}

	ret = ufs_eom_read_capabilities(ctx);
	if (ret)
		goto cleanup;

	if (ctx->cfg.local_peer == LOCAL) {
		ret = ufs_eom_qcom_init(ctx);
		if (ret) {
			pr_err("QCOM EOM init failed!\n");
			goto cleanup;
		}
	}

	ret = ufs_eom_validate_range(ctx);
	if (ret)
		goto cleanup;

	ret = ufs_eom_alloc_memory_for_results(ctx);
	if (ret)
		goto cleanup;

	ret = ufs_eom_setup_io(ctx);
	if (ret)
		goto cleanup;

	ufs_eom_format_output_filename(ctx, output_file, sizeof(output_file));

	srand((unsigned)clock());
	printf("Start EOM Scan...\n");
	clock_gettime(CLOCK_MONOTONIC, &ts_start);

	ret = ufs_eom_scan(ctx);
	if (ret)
		goto cleanup;

	ret = ufs_eom_generate_reports(ctx, output_file);
	if (ret)
		pr_err("Failed to generate EOM reports\n");

	clock_gettime(CLOCK_MONOTONIC, &ts_end);
	printf("EOM Scan %s! Time elapsed: %ld seconds\n",
	       ret ? "Failed" : "Finished",
	       ts_end.tv_sec - ts_start.tv_sec);

cleanup:
	free(ctx->data.er);
	free(ufs_eom_tmp_buf);

	close(ctx->data_fd);
	close(ctx->bsg_fd);

	if (ctx->cfg.local_peer == LOCAL)
		ufs_eom_qcom_exit(ctx);

	free(ctx);

	return ret;
}
