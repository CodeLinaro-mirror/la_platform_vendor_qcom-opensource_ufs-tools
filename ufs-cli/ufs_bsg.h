// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2024-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __UFS_BSG_H__
#define __UFS_BSG_H__

#ifndef SG_IO
#define SG_IO 0x2285
#endif

enum bsg_ioctl_dir {
	BSG_IOCTL_DIR_TO_DEV,
	BSG_IOCTL_DIR_FROM_DEV,
};

#define UPIU_RSP_CODE_OFFSET		8

enum {
	MASK_SCSI_STATUS		= 0xFF,
	MASK_TASK_RESPONSE              = 0xFF00,
	MASK_RSP_UPIU_RESULT            = 0xFFFF,
	MASK_QUERY_DATA_SEG_LEN         = 0xFFFF,
	MASK_RSP_UPIU_DATA_SEG_LEN	= 0xFFFF,
	MASK_RSP_EXCEPTION_EVENT        = 0x10000,
};

#ifndef SCSI_BSG_UFS_H
#include <linux/types.h>

#define UFS_CDB_SIZE	16

#define UIC_CMD_SIZE (sizeof(__u32) * 4)

enum ufs_bsg_msg_code {
	UPIU_TRANSACTION_UIC_CMD = 0x1F,
	UPIU_TRANSACTION_ARPMB_CMD,
};

enum ufs_rpmb_op_type {
	UFS_RPMB_WRITE_KEY		= 0x01,
	UFS_RPMB_READ_CNT		= 0x02,
	UFS_RPMB_WRITE			= 0x03,
	UFS_RPMB_READ			= 0x04,
	UFS_RPMB_READ_RESP		= 0x05,
	UFS_RPMB_SEC_CONF_WRITE		= 0x06,
	UFS_RPMB_SEC_CONF_READ		= 0x07,
	UFS_RPMB_PURGE_ENABLE		= 0x08,
	UFS_RPMB_PURGE_STATUS_READ	= 0x09,
};

struct utp_upiu_header {
	__be32 dword_0;
	__be32 dword_1;
	__be32 dword_2;
};

struct utp_upiu_query {
	__u8 opcode;
	__u8 idn;
	__u8 index;
	__u8 selector;
	__be16 reserved_osf;
	__be16 length;
	__be32 value;
	__be32 reserved[2];
};

struct utp_upiu_query_v4_0 {
	__u8 opcode;
	__u8 idn;
	__u8 index;
	__u8 selector;
	__u8 osf3;
	__u8 osf4;
	__be16 osf5;
	__be32 osf6;
	__be32 osf7;
	__be32 reserved;
};

struct utp_upiu_cmd {
	__be32 exp_data_transfer_len;
	__u8 cdb[UFS_CDB_SIZE];
};

struct utp_upiu_req {
	struct utp_upiu_header header;
	union {
		struct utp_upiu_cmd		sc;
		struct utp_upiu_query		qr;
		struct utp_upiu_query		uc;
	};
};

struct ufs_arpmb_meta {
	__be16	req_resp_type;
	__u8	nonce[16];
	__be32	write_counter;
	__be16	addr_lun;
	__be16	block_count;
	__be16	result;
} __attribute__((__packed__));

struct ufs_ehs {
	__u8	length;
	__u8	ehs_type;
	__be16	ehssub_type;
	struct ufs_arpmb_meta meta;
	__u8	mac_key[32];
} __attribute__((__packed__));

struct ufs_bsg_request {
	__u32 msgcode;
	struct utp_upiu_req upiu_req;
};

struct ufs_bsg_reply {
	int result;
	__u32 reply_payload_rcv_len;
	struct utp_upiu_req upiu_rsp;
};

struct ufs_rpmb_request {
	struct ufs_bsg_request bsg_request;
	struct ufs_ehs ehs_req;
};

struct ufs_rpmb_reply {
	struct ufs_bsg_reply bsg_reply;
	struct ufs_ehs ehs_rsp;
};
#endif /* SCSI_BSG_UFS_H */

#ifndef _UAPIBSG_H
#include <linux/types.h>

#define BSG_PROTOCOL_SCSI		0

#define BSG_SUB_PROTOCOL_SCSI_CMD	0
#define BSG_SUB_PROTOCOL_SCSI_TMF	1
#define BSG_SUB_PROTOCOL_SCSI_TRANSPORT	2

#define BSG_FLAG_Q_AT_TAIL 0x10
#define BSG_FLAG_Q_AT_HEAD 0x20

struct sg_io_v4 {
	__s32 guard;
	__u32 protocol;
	__u32 subprotocol;

	__u32 request_len;
	__u64 request;
	__u64 request_tag;
	__u32 request_attr;
	__u32 request_priority;
	__u32 request_extra;
	__u32 max_response_len;
	__u64 response;

	__u32 dout_iovec_count;

	__u32 dout_xfer_len;
	__u32 din_iovec_count;
	__u32 din_xfer_len;
	__u64 dout_xferp;
	__u64 din_xferp;

	__u32 timeout;
	__u32 flags;
	__u64 usr_ptr;
	__u32 spare_in;

	__u32 driver_status;
	__u32 transport_status;
	__u32 device_status;
	__u32 retry_delay;
	__u32 info;
	__u32 duration;
	__u32 response_len;
	__s32 din_resid;
	__s32 dout_resid;
	__u64 generated_tag;
	__u32 spare_out;

	__u32 padding;
};
#endif /* _UAPIBSG_H */

int ufs_bsg_io(int fd, struct ufs_bsg_request *req, struct ufs_bsg_reply *reply,
	       __u32 buf_len, __u8 *buf, enum bsg_ioctl_dir dir);
#endif /* __UFS_BSG_H__ */
