// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef EOM_QCOM_H
#define EOM_QCOM_H

#include "ufs_eom.h"

#define RX_EYEMON_VSTEP_WA_LANE_SHIFT	16
#define RX_EYEMON_VSTEP_WA_ENCODE(v, l)	(v | (l << RX_EYEMON_VSTEP_WA_LANE_SHIFT))

/*
 * Adjust eye-height steps for the local (host) Rx path.
 * The hardware voltage DAC has a non-linear step distribution that requires
 * this correction when computing the physical eye height.
 */
#define EOM_HEIGHT_ADJUST(steps) \
	((steps) + ((((steps) - 1) / 8) * 4))

int ufs_eom_qcom_init(struct ufs_eom_context *ctx);
void ufs_eom_qcom_exit(struct ufs_eom_context *ctx);
int ufs_eom_qcom_set_vstep(__u32 vstep, int lane);
int ufs_eom_qcom_generate_json_report(struct ufs_eom_context *ctx,
				      const char *output_file,
				      const char *mname, const char *pname,
				      const char *pver);

#endif /* EOM_QCOM_H */
