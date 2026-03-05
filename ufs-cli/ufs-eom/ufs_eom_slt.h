// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef UFS_EOM_SLT_H
#define UFS_EOM_SLT_H

#include "ufs_eom.h"

/* Initial search positions (in steps) for each boundary */
#define EOM_SLT_INIT_LEFT_TIMING	-9
#define EOM_SLT_INIT_RIGHT_TIMING	6
#define EOM_SLT_INIT_BOTTOM_VOLT	-57
#define EOM_SLT_INIT_TOP_VOLT		57
#define EOM_SLT_INIT_BOTTOM_VOLT_EXT	-99
#define EOM_SLT_INIT_TOP_VOLT_EXT	99

int ufs_eom_slt_scan(struct ufs_eom_context *ctx);

#endif /* UFS_EOM_SLT_H */
