// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Realtek video encoder v4l2 driver
 *
 * Copyright (c) 2024 Realtek Semiconductor Corp. All rights reserved.
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-Proprietary
 *
 * This software component is confidential and proprietary to Realtek
 * Semiconductor Corp. Disclosure, reproduction, redistribution, in whole
 * or in part, of this work and its derivatives without express permission
 * is prohibited.
 */

#include <media/v4l2-ioctl.h>

#include <soc/realtek/rtk-krpc-agent.h>

#include "rtkve-rpc.h"
#include "rtkve-common.h"

#define RPC_BUF_SIZE (1024)
#define STREAM_RBSIZE (0x600000)
#define COMMAND_RBSIZE (0x40000)
#define SIZE_4MB (1024 * 1024 * 4)
#define SIZE_2MB (1024 * 1024 * 2)

static struct rtk_krpc_ept_info *get_ve3_krpc_info(void)
{
	struct device_node *np;
	struct rtk_krpc_ept_info *vcpu_ept_info = NULL;

	np = of_find_compatible_node(NULL, NULL, "realtek,ve3rpc");
	if (!np) {
		pr_err("Get ve3 rpc node from device tree fail");
		goto exit;
	}

	vcpu_ept_info = of_krpc_ept_info_get(np, 0);
exit:
	return vcpu_ept_info;
}

static int SendReply(struct rtk_krpc_ept_info *krpc_ept_info,
		     uint32_t req_taskID, int32_t req_context,
		     char *ReplyParameter, // parameter's start address
		     uint32_t ParameterSize) // parameter's size
{
	ssize_t val;
	struct rpc_struct *rpc;
	char *mem_ToShm;
	char *p;
	int size_ToShm = 0; // total mem size for writing to share memory
	uint32_t *context;
	int ret = 0;

	mem_ToShm = kmalloc(sizeof(struct RPC_STRUCT) + sizeof(uint32_t) +
				    ParameterSize,
			    GFP_KERNEL | __GFP_ZERO);
	if (!mem_ToShm) {
		pr_err("SendReply malloc fail\n");
		ret = -ENOMEM;
		goto exit;
	}
	p = mem_ToShm;
	rpc = (struct rpc_struct *)p;
	p += sizeof(struct rpc_struct);
	context = (uint32_t *)p;
	*context = htonl(req_taskID);
	size_ToShm += sizeof(uint32_t);
	p += sizeof(uint32_t);
	for (context = (uint32_t *)p; (char *)context < p + ParameterSize;
	     context++) {
		*context = *ReplyParameter;
		ReplyParameter += sizeof(uint32_t);
	}
	size_ToShm += ParameterSize;
	rpc->programID = REPLYID;
	rpc->versionID = REPLYID;
	rpc->procedureID = 0;
	rpc->mycontext = req_context; // fill in req's para addr
	rpc->taskID = 0xffffffff;
	rpc->sysPID = 0xffffffff;
	rpc->parameterSize = size_ToShm;
	size_ToShm += sizeof(struct rpc_struct);
	val = rtk_send_rpc(krpc_ept_info, mem_ToShm, size_ToShm);
	if (val != size_ToShm) {
		pr_err("ve2RPC: ERROR in send kernel RPC\n");
		ret = -EINVAL;
	}
	kfree(mem_ToShm);

exit:
	return ret;
}

static int handle_rpc_command(struct rtk_krpc_ept_info *krpc_ept_info,
			      char *buf)
{
	struct vpu_handler *hndl = (struct vpu_handler *)krpc_ept_info->priv;
	int cmd;
	HRESULT retval = S_OK;
	struct rpc_struct *rpc_head = (struct rpc_struct *)buf;
	int ret = 0;

	dev_dbg(hndl->dev, "rpc_kern_ve3_read, cmd %d, count %lu, size %d\n",
		rpc_head->procedureID, sizeof(rpc_head),
		rpc_head->parameterSize);
	cmd = rpc_head->procedureID;
	switch (cmd) {
	default:
		break;
	}
	if (rpc_head->taskID != 0)
		SendReply(hndl->vcpu_ept_info, rpc_head->taskID,
			  rpc_head->mycontext, (char *)&retval, sizeof(retval));

	return ret;
}

static int krpc_vcpu_cb(struct rtk_krpc_ept_info *krpc_ept_info, char *buf)
{
	uint32_t *tmp;
	struct rpc_struct *rpc = (struct rpc_struct *)buf;

	if (rpc->programID == REPLYID) {
		tmp = (uint32_t *)(buf + sizeof(struct rpc_struct));
		*(krpc_ept_info->retval) = *(tmp + 1);

		complete(&krpc_ept_info->ack);
	} else {
		handle_rpc_command(krpc_ept_info, buf);
	}

	return 0;
}

static char *prepare_rpc_data(struct rtk_krpc_ept_info *krpc_ept_info,
			      uint32_t command, uint32_t param1,
			      uint32_t param2, int *len)
{
	struct rpc_struct *rpc;
	uint32_t *tmp;
	char *buf;

	*len = sizeof(struct rpc_struct) + 3 * sizeof(uint32_t);
	buf = kmalloc(sizeof(struct rpc_struct) + 3 * sizeof(uint32_t),
		      GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	rpc = (struct rpc_struct *)buf;
	rpc->programID = KERNELID;
	rpc->versionID = KERNELID;
	rpc->procedureID = 0;
	rpc->taskID = krpc_ept_info->id;
	rpc->sysTID = krpc_ept_info->id;
	rpc->sysPID = krpc_ept_info->id;
	rpc->parameterSize = 3 * sizeof(uint32_t);
	rpc->mycontext = 0;
	tmp = (uint32_t *)(buf + sizeof(struct rpc_struct));
	*tmp = command;
	*(tmp + 1) = param1;
	*(tmp + 2) = param2;

	return buf;
}

int rtkve_send_rpc(struct rtk_krpc_ept_info *krpc_ept_info, char *buf, int len,
		   uint32_t *retval)
{
	int ret = 0;

	mutex_lock(&krpc_ept_info->send_mutex);

	krpc_ept_info->retval = retval;
	rtk_send_rpc(krpc_ept_info, buf, len);
	if (!wait_for_completion_timeout(&krpc_ept_info->ack, RPC_TIMEOUT)) {
		pr_err("[%s]kernel rpc timeout: %s...\n", __func__,
		       krpc_ept_info->name);
		rtk_krpc_dump_ringbuf_info(krpc_ept_info);
		mutex_unlock(&krpc_ept_info->send_mutex);
		ret = -EINVAL;
		goto exit;
	}
	mutex_unlock(&krpc_ept_info->send_mutex);

exit:
	return ret;
}

static int send_rpc(struct vpu_handler *hndl, int opt, uint32_t command,
		    uint32_t param1, uint32_t param2, uint32_t *retval)
{
	int ret = 0;
	char *buf;
	int len;

	if (opt == RPC_VIDEO) {
		buf = prepare_rpc_data(hndl->vcpu_ept_info, command, param1,
				       param2, &len);
		if (!IS_ERR(buf)) {
			ret = rtkve_send_rpc(hndl->vcpu_ept_info, buf, len,
					     retval);
			kfree(buf);
		}
	}

	return ret;
}

static int rtkve_rpc_shuttle(struct vpu_handler *hndl, int cmd, void *data,
			     int size, void *rpc_ret, int rpc_ret_size)
{
	struct vpu_buf *rpc_buf;
	int offset;
	uint32_t dat;
	unsigned int RPC_ret;
	int ret = 0;

	rpc_buf = rtkve_allocate_dma_memory(hndl->dev, RPC_BUF_SIZE);
	if (!rpc_buf) {
		dev_err(hndl->dev,
			"%s: Allocating shuttle buf of size %d, fail: %d\n",
			__func__, RPC_BUF_SIZE, ret);
		goto exit;
	}

	memcpy_toio(rpc_buf->vaddr, data, size);
	dsb(sy);
	offset = get_rpc_alignment_offset(size);
	dat = rpc_buf->daddr;

	if (send_rpc(hndl, RPC_VIDEO, cmd, dat, dat + offset, &RPC_ret)) {
		rtkve_free_dma_memory(hndl->dev, rpc_buf);
		dev_err(hndl->dev, "rtkve rpc shuttle fail, cmd %d\n", cmd);
		ret = -EPERM;
	} else {
		if (RPC_ret == S_OK) {
			if (rpc_ret)
				memcpy_toio(rpc_ret, rpc_buf->vaddr + offset,
					    rpc_ret_size);
			rtkve_free_dma_memory(hndl->dev, rpc_buf);
			ret = 0;
		} else {
			rtkve_free_dma_memory(hndl->dev, rpc_buf);
			dev_err(hndl->dev, "rtkve rpc return fail, cmd %d\n",
				cmd);
			ret = -EPERM;
		}
	}
exit:
	return ret;
}

int rtkve_rpc_open(struct vpu_handler *hndl, int type)
{
	struct VIDEO_RPC_INSTANCE instance;
	struct RPCRES_LONG retval;
	unsigned int ret = 0;

	if (!hndl) {
		pr_err("%s, handler is NULL", __func__);
		ret = -EPERM;
		goto exit;
	}

	hndl->vcpu_ept_info = get_ve3_krpc_info();
	ret = krpc_info_init(hndl->vcpu_ept_info, "ve3rpc", krpc_vcpu_cb);
	if (ret) {
		dev_err(hndl->dev, "%s krpc_info_init fail\n",
			v4l2_type_names[hndl->type]);
		goto exit;
	}
	hndl->vcpu_ept_info->priv = (void *)hndl;

	instance.type = htonl(type);
	ret = rtkve_rpc_shuttle(hndl, VIDEO_RPC_VENC_ToAgent_Create, &instance,
				sizeof(instance), &retval, sizeof(retval));
	if (ret) {
		dev_err(hndl->dev, "fail to open encoder(%s)\n",
			v4l2_type_names[hndl->type]);
		goto exit;
	}

	mutex_lock(&hndl->lock);
	hndl->inst_type = type;
	if (htonl(retval.result) == S_OK) {
		hndl->inst_id = htonl(retval.data);
	} else {
		dev_err(hndl->dev, "fail to get instance(%s)\n",
			v4l2_type_names[hndl->type]);
		mutex_unlock(&hndl->lock);
		ret = (-EPERM);
	}
	mutex_unlock(&hndl->lock);
exit:
	return ret;
}

int rtkve_rpc_close(struct vpu_handler *hndl)
{
	int ret = 0;

	if (!hndl || !hndl->inst_id) {
		pr_err("%s, handler is NULL", __func__);
		ret = -EPERM;
		goto exit;
	}

	mutex_lock(&hndl->lock);
	if (hndl->inst_id) {
		uint32_t inst_id;
		int ret;

		inst_id = htonl(hndl->inst_id);
		ret = rtkve_rpc_shuttle(hndl, VIDEO_RPC_VENC_ToAgent_Destroy,
					&inst_id, sizeof(inst_id), NULL, 0);
		if (ret) {
			mutex_unlock(&hndl->lock);
			dev_err(hndl->dev, "fail to close encoder\n");
			ret = -EPERM;
			goto exit;
		}
	}

	krpc_info_deinit(hndl->vcpu_ept_info);
	krpc_ept_info_put(hndl->vcpu_ept_info);

	hndl->inst_type = -1;
	hndl->inst_id = -1;
	mutex_unlock(&hndl->lock);

exit:
	return ret;
}

int rtkve_rpc_set_srcfmt(struct vpu_handler *hndl, enum YUV_FMT fmt)
{
	struct VIDEO_RPC_ENC_INIT info;
	int ret = 0;

	if (!hndl || !hndl->inst_id) {
		pr_err("%s, encoder handler is NULL", __func__);
		ret = -EPERM;
		goto exit;
	}

	mutex_lock(&hndl->lock);

	memset(&info, 0, sizeof(info));

	info.instanceID = htonl(hndl->inst_id);
	info.type = htonl(VF_TYPE_VIDEO_ENCODER);
	info.yuvFormat = htonl(fmt);
	dev_dbg(hndl->dev, "%s : fmt %d", __func__, htonl(fmt));
	ret = rtkve_rpc_shuttle(hndl, VIDEO_RPC_VENC_ToAgent_Init, &info,
				sizeof(info), NULL, 0);
	mutex_unlock(&hndl->lock);
	if (ret) {
		dev_err(hndl->dev, "fail to do Init cmd\n");
		ret = (-EPERM);
		goto exit;
	}

exit:
	return ret;
}

int rtkve_rpc_set_encfmt(struct vpu_handler *hndl, enum VIDEO_STREAM_TYPE fmt)
{
	struct VIDEO_RPC_ENC_SET_ENCFORMAT info;
	int ret = 0;

	if (!hndl || !hndl->inst_id) {
		pr_err("%s, encoder handler is NULL", __func__);
		ret = -EPERM;
		goto exit;
	}

	mutex_lock(&hndl->lock);

	memset(&info, 0, sizeof(info));

	info.instanceID = htonl(hndl->inst_id);
	info.streamType = htonl(fmt);
	dev_dbg(hndl->dev, "%s : fmt %d", __func__, htonl(fmt));
	ret = rtkve_rpc_shuttle(hndl, VIDEO_RPC_VENC_ToAgent_SetEncodeFormat,
				&info, sizeof(info), NULL, 0);
	mutex_unlock(&hndl->lock);
	if (ret) {
		dev_err(hndl->dev, "fail to do SetEncodeFormat cmd\n");
		ret = (-EPERM);
		goto exit;
	}

exit:
	return ret;
}

int rtkve_rpc_set_resolution(struct vpu_handler *hndl, uint32_t in_width,
			     uint32_t in_height, uint32_t out_width, uint32_t out_height)
{
	struct VIDEO_RPC_ENC_SET_NEW_RESOLUTION info;
	int ret = 0;

	if (!hndl || !hndl->inst_id) {
		pr_err("%s, encoder handler is NULL", __func__);
		ret = -EPERM;
		goto exit;
	}

	mutex_lock(&hndl->lock);

	memset(&info, 0, sizeof(info));

	info.instanceID = htonl(hndl->inst_id);
	info.in_height = htonl(in_height);
	info.in_width = htonl(in_width);
	info.out_height = htonl(out_height);
	info.out_width = htonl(out_width);
	info.bit_depth = htonl(8);
	dev_dbg(hndl->dev, "%s : in %dx%d, out %dx%d", __func__,
		htonl(info.in_width), htonl(info.in_height),
		htonl(info.out_width), htonl(info.out_height));
	ret = rtkve_rpc_shuttle(hndl, VIDEO_RPC_VENC_ToAgent_SetNewResolution,
				&info, sizeof(info), NULL, 0);
	mutex_unlock(&hndl->lock);
	if (ret) {
		dev_err(hndl->dev, "fail to do SetNewResolution cmd\n");
		ret = (-EPERM);
		goto exit;
	}

exit:
	return ret;
}

int rtkve_rpc_set_GOPStruct(struct vpu_handler *hndl, uint32_t M, uint32_t N)
{
	struct VIDEO_RPC_ENC_SET_GOPSTRUCTURE info;
	int ret = 0;

	if (!hndl || !hndl->inst_id) {
		pr_err("%s, encoder handler is NULL", __func__);
		ret = -EPERM;
		goto exit;
	}

	mutex_lock(&hndl->lock);

	memset(&info, 0, sizeof(info));

	info.instanceID = htonl(hndl->inst_id);
	info.M = htonl(M);
	info.N = htonl(N);
	dev_dbg(hndl->dev, "%s : M %d, N %d", __func__, M, N);

	ret = rtkve_rpc_shuttle(hndl, VIDEO_RPC_VENC_ToAgent_SetGOPStructure,
				&info, sizeof(info), NULL, 0);
	mutex_unlock(&hndl->lock);
	if (ret) {
		dev_err(hndl->dev, "fail to do SetGOPStructure cmd\n");
		ret = (-EPERM);
		goto exit;
	}

exit:
	return ret;
}

int rtkve_rpc_set_profile(struct vpu_handler *hndl,
			  enum VIDEO_ENC_PROFILE profile)
{
	struct VIDEO_RPC_ENC_SET_PROFILE info;
	int ret = 0;

	if (!hndl || !hndl->inst_id) {
		pr_err("%s, encoder handler is NULL", __func__);
		ret = -EPERM;
		goto exit;
	}

	mutex_lock(&hndl->lock);

	memset(&info, 0, sizeof(info));

	info.instanceID = htonl(hndl->inst_id);
	info.profile = htonl(profile);
	dev_dbg(hndl->dev, "%s : profile %d", __func__, profile);
	ret = rtkve_rpc_shuttle(hndl, VIDEO_RPC_VENC_ToAgent_SetProfile, &info,
				sizeof(info), NULL, 0);
	mutex_unlock(&hndl->lock);
	if (ret) {
		dev_err(hndl->dev, "fail to do SetBitRate cmd\n");
		ret = (-EPERM);
		goto exit;
	}

exit:
	return ret;
}

int rtkve_rpc_set_bitrate(struct vpu_handler *hndl, uint32_t mode,
			  uint32_t bitrate)
{
	struct VIDEO_RPC_ENC_SET_BITRATE info;
	int ret = 0;

	if (!hndl || !hndl->inst_id) {
		pr_err("%s, encoder handler is NULL", __func__);
		ret = -EPERM;
		goto exit;
	}

	mutex_lock(&hndl->lock);

	memset(&info, 0, sizeof(info));

	info.instanceID = htonl(hndl->inst_id);
	info.rateControlMode = htonl(mode);
	info.peakBitRate = htonl(bitrate);
	info.aveBitRate = htonl(bitrate);
	info.bitBufferSize = htonl(SIZE_4MB);
	info.initBufferFullness = htonl(SIZE_2MB);
	dev_dbg(hndl->dev, "%s : mode %d, bitrate %d", __func__, mode, bitrate);
	ret = rtkve_rpc_shuttle(hndl, VIDEO_RPC_VENC_ToAgent_SetBitRate, &info,
				sizeof(info), NULL, 0);
	mutex_unlock(&hndl->lock);
	if (ret) {
		dev_err(hndl->dev, "fail to do SetBitRate cmd\n");
		ret = (-EPERM);
		goto exit;
	}

exit:
	return ret;
}

int rtkve_rpc_set_frmrate(struct vpu_handler *hndl, uint32_t frmrate)
{
	struct VIDEO_RPC_ENC_SET_FRAME_RATE info;
	int ret = 0;

	if (!hndl || !hndl->inst_id) {
		pr_err("%s, encoder handler is NULL", __func__);
		ret = -EPERM;
		goto exit;
	}

	mutex_lock(&hndl->lock);

	memset(&info, 0, sizeof(info));
	dev_dbg(hndl->dev, "%s : frmrate %d", __func__, frmrate);
	info.instanceID = htonl(hndl->inst_id);
	info.frame_rate = htonl(frmrate);
	ret = rtkve_rpc_shuttle(hndl, VIDEO_RPC_VENC_ToAgent_SetFrameRate,
				&info, sizeof(info), NULL, 0);
	mutex_unlock(&hndl->lock);
	if (ret) {
		dev_err(hndl->dev, "fail to do SetFrameRate cmd\n");
		ret = (-EPERM);
		goto exit;
	}

exit:
	return ret;
}

int rtkve_rpc_req_keyfrm(struct vpu_handler *hndl)
{
	struct VIDEO_RPC_ENC_REQ_KEY_FRAME info;
	int ret = 0;

	if (!hndl || !hndl->inst_id) {
		pr_err("%s, encoder handler is NULL", __func__);
		ret = -EPERM;
		goto exit;
	}

	mutex_lock(&hndl->lock);

	memset(&info, 0, sizeof(info));

	info.instanceID = htonl(hndl->inst_id);
	ret = rtkve_rpc_shuttle(hndl, VIDEO_RPC_VENC_ToAgent_ReqKeyFrame, &info,
				sizeof(info), NULL, 0);
	mutex_unlock(&hndl->lock);
	if (ret) {
		dev_err(hndl->dev, "fail to do ReqKeyFrame cmd\n");
		ret = (-EPERM);
		goto exit;
	}

exit:
	return ret;
}

int rtkve_rpc_start_record(struct vpu_handler *hndl)
{
	struct VIDEO_RPC_ENC_START_ENC info;
	int ret = 0;

	if (!hndl || !hndl->inst_id) {
		pr_err("%s, encoder handler is NULL", __func__);
		ret = -EPERM;
		goto exit;
	}

	mutex_lock(&hndl->lock);

	memset(&info, 0, sizeof(info));

	info.instanceID = htonl(hndl->inst_id);
	info.startMode = 0;
	ret = rtkve_rpc_shuttle(hndl, VIDEO_RPC_VENC_ToAgent_StartRecord, &info,
				sizeof(info), NULL, 0);
	mutex_unlock(&hndl->lock);
	if (ret) {
		dev_err(hndl->dev, "fail to do StartRecord cmd\n");
		ret = (-EPERM);
		goto exit;
	}

exit:
	return ret;
}

int rtkve_rpc_stop_record(struct vpu_handler *hndl)
{
	struct VIDEO_RPC_ENC_STOP_ENC info;
	int ret = 0;

	if (!hndl || !hndl->inst_id) {
		pr_err("%s, encoder handler is NULL", __func__);
		ret = -EPERM;
		goto exit;
	}

	mutex_lock(&hndl->lock);

	memset(&info, 0, sizeof(info));

	info.instanceID = htonl(hndl->inst_id);
	ret = rtkve_rpc_shuttle(hndl, VIDEO_RPC_VENC_ToAgent_StopRecord, &info,
				sizeof(info), NULL, 0);
	mutex_unlock(&hndl->lock);
	if (ret) {
		dev_err(hndl->dev, "fail to do StopRecord cmd\n");
		ret = (-EPERM);
		goto exit;
	}

exit:
	return ret;
}

static int rtkve_rpc_common(struct vpu_handler *hndl, int cmd)
{
	uint32_t instanceID;
	int ret = 0;

	mutex_lock(&hndl->lock);
	if (!hndl || !hndl->inst_id || hndl->inst_id == -1) {
		pr_err("%s, handler is NULL", __func__);
		ret = -EPERM;
		mutex_unlock(&hndl->lock);
		goto exit;
	}

	instanceID = htonl(hndl->inst_id);
	ret = rtkve_rpc_shuttle(hndl, cmd, &instanceID, sizeof(instanceID),
				NULL, 0);
	if (ret) {
		dev_err(hndl->dev, "fail to do cmd %d \n", cmd);
		ret = -EPERM;
		mutex_unlock(&hndl->lock);
		goto exit;
	}
	mutex_unlock(&hndl->lock);
exit:
	return ret;
}

int rtkve_rpc_run(struct vpu_handler *hndl)
{
	int ret = 0;

	ret = rtkve_rpc_common(hndl, VIDEO_RPC_VENC_ToAgent_Run);
	return ret;
}

int rtkve_rpc_pause(struct vpu_handler *hndl)
{
	int ret = 0;

	ret = rtkve_rpc_common(hndl, VIDEO_RPC_VENC_ToAgent_Pause);
	return ret;
}

int rtkve_rpc_stop(struct vpu_handler *hndl)
{
	int ret = 0;

	ret = rtkve_rpc_common(hndl, VIDEO_RPC_VENC_ToAgent_Stop);
	return ret;
}

static int rtkve_rpc_set_ringbuf(struct vpu_handler *hndl,
				 struct rtkve_ringbuf_t *prb, uint32_t bodysize,
				 enum RINGBUFFER_TYPE type)
{
	struct RPC_RINGBUFFER ringbuffer;
	struct vpu_buf *body;
	struct vpu_buf *head;
	int ret = 0;

	if (!hndl) {
		pr_err("%s, handler is NULL", __func__);
		ret = -EPERM;
		goto exit;
	}

	body = rtkve_allocate_dma_memory(hndl->dev, bodysize);
	if (!body) {
		dev_err(hndl->dev,
			"%s: Allocating body buf of size %d, fail: %d\n",
			__func__, bodysize, ret);
		ret = -EPERM;
		goto exit;
	}

	head = rtkve_allocate_dma_memory(hndl->dev,
					 sizeof(struct _tagRingBufferHeader));
	if (!head) {
		dev_err(hndl->dev,
			"%s: Allocating head buf of size %ld, fail: %d\n",
			__func__, sizeof(struct _tagRingBufferHeader), ret);
		goto err_head_exit;
	}

	mutex_init(&prb->lock);
	prb->buf_hdl = body;
	prb->phyaddr = body->daddr;
	prb->virtaddr = (uint8_t *)body->vaddr;
	prb->hdr_hdl = head;
	prb->phyaddr_hdr = head->daddr;
	prb->virtaddr_hdr = (uint8_t *)head->vaddr;
	prb->size = bodysize;
	prb->limit = prb->phyaddr + bodysize;

	prb->pRBH = (volatile struct _tagRingBufferHeader *)head->vaddr;
	prb->pRBH->size = htonl(bodysize);
	prb->pRBH->numOfReadPtr = htonl(1);
	prb->pRBH->beginAddr = htonl(body->daddr);
	prb->pRBH->writePtr = htonl(body->daddr);
	prb->pRBH->readPtr[0] = htonl(body->daddr);
	prb->pRBH->readPtr[1] = htonl(body->daddr);
	prb->pRBH->readPtr[2] = htonl(body->daddr);
	prb->pRBH->readPtr[3] = htonl(body->daddr);
	prb->pRBH->reserve2 = 0;
	prb->pRBH->reserve3 = 0;
	prb->pRBH->bufferID = htonl(type);

	if (RINGBUFFER_FAKE == type)
		goto exit;

	mutex_lock(&hndl->lock);
	ringbuffer.instanceID = htonl(hndl->inst_id);
	ringbuffer.readPtrIndex = 0;
	ringbuffer.pinID = 0;
	ringbuffer.pRINGBUFF_HEADER = htonl(head->daddr);
	ret = rtkve_rpc_shuttle(hndl, VIDEO_RPC_VENC_ToAgent_InitRingBuffer,
				&ringbuffer, sizeof(ringbuffer), NULL, 0);
	mutex_unlock(&hndl->lock);
	if (ret)
		goto exit;

	return 0;

err_head_exit:
	rtkve_free_dma_memory(hndl->dev, body);
exit:
	return ret;
}

static int rtkve_rpc_release_ringbuf(struct vpu_handler *hndl,
				     struct rtkve_ringbuf_t *ringbuf)
{
	int ret = 0;

	if (!hndl) {
		pr_err("%s, handler is NULL", __func__);
		ret = -EPERM;
		goto exit;
	}

	if (!ringbuf) {
		dev_err(hndl->dev, "Invaild input in %s\n", __func__);
		ret = -EPERM;
		goto exit;
	}

	if (ringbuf->hdr_hdl) {
		mutex_lock(&ringbuf->lock);
		rtkve_free_dma_memory(hndl->dev, ringbuf->hdr_hdl);
		ringbuf->hdr_hdl = NULL;
		mutex_unlock(&ringbuf->lock);
	}

	if (ringbuf->buf_hdl) {
		mutex_lock(&ringbuf->lock);
		rtkve_free_dma_memory(hndl->dev, ringbuf->buf_hdl);
		ringbuf->buf_hdl = NULL;
		mutex_unlock(&ringbuf->lock);
	}

exit:
	return ret;
}

int rtkve_rpc_create_encoder(struct vpu_instance *inst)
{
	struct vpu_handler *enc_hdl;
	int ret = 0;

	enc_hdl = kzalloc(sizeof(struct vpu_handler), GFP_KERNEL);
	if (!enc_hdl) {
		dev_err(inst->dev->dev, "allocate flash handle fail\n");
		ret = (-ENOMEM);
		goto err_exit;
	}

	enc_hdl->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	enc_hdl->dev = inst->dev->dev;
	enc_hdl->inst_id = -1;

	ret = rtkve_rpc_open(enc_hdl, VF_TYPE_VIDEO_ENCODER);
	if (ret) {
		dev_err(enc_hdl->dev, "fail to open vpu encoder\n");
		ret = (-EPERM);
		goto err_open_exit;
	}

	ret = rtkve_rpc_set_ringbuf(enc_hdl, &enc_hdl->stream_rb, STREAM_RBSIZE,
				    RINGBUFFER_STREAM);
	if (ret) {
		dev_err(enc_hdl->dev, "fail to initial stream rb\n");
		goto err_stream_exit;
	}

	ret = rtkve_rpc_set_ringbuf(enc_hdl, &enc_hdl->inband_rb,
				    COMMAND_RBSIZE, RINGBUFFER_COMMAND);
	if (ret) {
		dev_err(enc_hdl->dev, "fail to initial inband rb\n");
		goto err_com_exit;
	}

	ret = rtkve_rpc_set_ringbuf(
		enc_hdl, &enc_hdl->mesg_rb,
		RTKVE_MAX_MSG_NUM *
			sizeof(struct VIDEO_RPC_ENC_ELEM_FRAME_INFO),
		RINGBUFFER_MESSAGE);
	if (ret) {
		dev_err(enc_hdl->dev, "fail to initial message rb\n");
		goto err_mesg_exit;
	}

	inst->enc_hdl = enc_hdl;

	return 0;
err_mesg_exit:
	rtkve_rpc_release_ringbuf(enc_hdl, &enc_hdl->inband_rb);
err_com_exit:
	rtkve_rpc_release_ringbuf(enc_hdl, &enc_hdl->stream_rb);
err_stream_exit:
	rtkve_rpc_close(enc_hdl);
err_open_exit:
	kfree(enc_hdl);
err_exit:
	return ret;
}

int rtkve_rpc_destroy_encoder(struct vpu_instance *inst)
{
	struct vpu_handler *enc_hdl = inst->enc_hdl;
	int ret = 0;

	if (!enc_hdl)
		goto exit;

	rtkve_rpc_release_ringbuf(enc_hdl, &enc_hdl->inband_rb);
	rtkve_rpc_release_ringbuf(enc_hdl, &enc_hdl->stream_rb);
	rtkve_rpc_release_ringbuf(enc_hdl, &enc_hdl->mesg_rb);
	rtkve_rpc_close(enc_hdl);

	if (enc_hdl)
		kfree(enc_hdl);

	enc_hdl = NULL;
exit:
	return ret;
}

static void rtkve_inband_memcpy(uint8_t *des, uint8_t *src, unsigned int size)
{
	unsigned int *src_int32 = (unsigned int *)src;
	unsigned int *des_int32 = (unsigned int *)des;
	unsigned int i;

	for (i = 0; i < (size / sizeof(int)); i++)
		des_int32[i] = htonl(src_int32[i]);

	dsb(sy);
}

int rtkve_write_rb(struct rtkve_ringbuf_t *ringbuf, int type, uint8_t *buf,
		   int size)
{
	volatile struct rtkve_ringbuf_t *rb = ringbuf;
	uint32_t wp, rp;
	void *wptr, *next, *addr_end;
	uint8_t over = 0;
	int ret = 0;

	if (!ringbuf) {
		pr_err("invaild input ringbuf %p", ringbuf);
		ret = -EPERM;
		goto exit;
	}

	mutex_lock(&ringbuf->lock);

	wp = htonl(rb->pRBH->writePtr);
	rp = htonl(rb->pRBH->readPtr[0]);

	if (rp > wp && (int)(rp - wp - 1) < size) {
		mutex_unlock(&ringbuf->lock);
		ret = -EPERM;
		goto exit;
	}

	if (wp > rp && (int)(rp + rb->size - wp - 1) < size) {
		mutex_unlock(&ringbuf->lock);
		ret = -EPERM;
		goto exit;
	}

	wptr = rb->virtaddr + (wp - rb->phyaddr);
	addr_end = rb->virtaddr + rb->size;
	next = wptr + size;
	if (next >= addr_end) {
		over = 1;
		next -= rb->size;
	}

	if (over) {
		int size0 = 0;
		int size1 = 0;

		size0 = rb->virtaddr + rb->size - (uint8_t *)wptr;
		size1 = size - size0;

		if (size0 != 0)
			rtkve_inband_memcpy(wptr, buf, (unsigned int)size0);

		if (size1 != 0)
			rtkve_inband_memcpy(rb->virtaddr, buf + size0,
					    (unsigned int)size1);
	} else {
		rtkve_inband_memcpy(wptr, buf, (unsigned int)size);
	}

	rb->pRBH->writePtr =
		htonl(rb->phyaddr + ((uint8_t *)next - rb->virtaddr));
	dsb(sy);

	mutex_unlock(&ringbuf->lock);
exit:
	return ret;
}

int rtkve_inband_pts(struct vpu_handler *hndl, uint32_t wptr, uint64_t pts)
{
	struct PTS_INFO cmd;
	int ret = 0;

	cmd.header.type = INBAND_CMD_TYPE_PTS;
	cmd.header.size = sizeof(struct PTS_INFO);
	cmd.wPtr = wptr;
	cmd.PTSH = pts >> 32;
	cmd.PTSL = pts;

	ret = rtkve_write_rb(&hndl->inband_rb, RINGBUFFER_COMMAND,
			     (uint8_t *)&cmd, sizeof(cmd));

	return ret;
}

int rtkve_inband_raw_input(struct vpu_handler *hndl, uint32_t luma_addr,
			   uint32_t luma_size, uint32_t chroma_addr,
			   uint32_t chroma_size, int64_t pts)
{
	struct RAWYUV_INFO cmd;
	int ret = 0;

	cmd.header.type = VENC_INBAND_CMD_TYPE_RAWYUV;
	cmd.header.size = sizeof(struct RAWYUV_INFO);
	cmd.luma_addr = luma_addr;
	cmd.luma_size = luma_size;
	cmd.chroma_addr = chroma_addr;
	cmd.chroma_size = chroma_size;
	cmd.PTSH = pts >> 32;
	cmd.PTSL = pts & 0xFFFFFFFF;

	ret = rtkve_write_rb(&hndl->inband_rb, RINGBUFFER_COMMAND,
			     (uint8_t *)&cmd, sizeof(cmd));
	return ret;
}

int rtkve_inband_set_ref_buffer(struct vpu_handler *hndl)
{
	struct REFYUV_INFO cmd;
	int refbuf_size = (1920 * 1088) * 6 + 18656 * 2 + 8160 * 2;
	int ret = 0;

	hndl->refbuf = rtkve_allocate_dma_memory(hndl->dev, refbuf_size);
	if (!hndl->refbuf) {
		dev_err(hndl->dev,
			"%s: Allocating ref buf of size %d, fail: %d\n",
			__func__, refbuf_size, ret);
		ret = -EPERM;
		goto exit;
	}

	cmd.header.type = VENC_INBAND_CMD_TYPE_REFYUV;
	cmd.header.size = sizeof(struct REFYUV_INFO);
	cmd.start_addr = (unsigned int)hndl->refbuf->daddr;
	cmd.size = refbuf_size;
	ret = rtkve_write_rb(&hndl->inband_rb, RINGBUFFER_COMMAND,
			     (uint8_t *)&cmd, sizeof(cmd));
	if (ret) {
		rtkve_free_dma_memory(hndl->dev, hndl->refbuf);
		hndl->refbuf = NULL;
	}
exit:
	return ret;
}

int rtkve_inband_release_ref_buffer(struct vpu_handler *hndl)
{
	int ret = 0;
	if (hndl->refbuf) {
		rtkve_free_dma_memory(hndl->dev, hndl->refbuf);
		hndl->refbuf = NULL;
	}

	return ret;
}

int rtkve_inband_set_eos(struct vpu_handler *hndl)
{
	struct EOS cmd;
	int ret = 0;

	dev_dbg(hndl->dev, "%s", __func__);

	cmd.header.type = INBAND_CMD_TYPE_EOS;
	cmd.header.size = sizeof(struct EOS);
	cmd.eventID = 0;
	cmd.wPtr = 0;
	ret = rtkve_write_rb(&hndl->inband_rb, RINGBUFFER_COMMAND,
			     (uint8_t *)&cmd, sizeof(cmd));

	return ret;
}
