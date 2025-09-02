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
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/of_address.h>
#include <linux/genalloc.h>
#include <linux/firmware.h>
#include <linux/of_reserved_mem.h>

#include "rtkve-vpu.h"

#define VPU_PLATFORM_DEVICE_NAME "rtkve-enc"

// rtk

static const struct media_device_ops rtkve_m2m_media_ops = {
	.req_validate = vb2_request_validate,
	.req_queue = v4l2_m2m_request_queue,
};

static int rtkve_enc_open(struct file *filp)
{
	struct video_device *vdev = video_devdata(filp);
	struct vpu_device *dev = video_drvdata(filp);
	const struct rtkve_match_data *enc_pdata = dev->rtkve_mdata;
	struct vpu_instance *inst = NULL;
	int ret;

	inst = kzalloc(sizeof(*inst), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	inst->dev = dev;
	inst->type = VPU_INST_TYPE_DEC;

	v4l2_fh_init(&inst->v4l2_fh, vdev);
	filp->private_data = &inst->v4l2_fh;
	v4l2_fh_add(&inst->v4l2_fh);

	inst->v4l2_fh.m2m_ctx =
		v4l2_m2m_ctx_init(dev->m2m_dev, inst, enc_pdata->queue_init);
	if (IS_ERR(inst->v4l2_fh.m2m_ctx)) {
		ret = PTR_ERR(inst->v4l2_fh.m2m_ctx);
		goto free_inst;
	}

	if (enc_pdata->ctrls_setup(inst)) {
		ret = -ENODEV;
		goto err_m2m_release;
	}

	rtkve_set_default_format(inst, &inst->src_fmt, &inst->dst_fmt);
	inst->colorspace = V4L2_COLORSPACE_DEFAULT;
	inst->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	inst->quantization = V4L2_QUANTIZATION_DEFAULT;
	inst->xfer_func = V4L2_XFER_FUNC_DEFAULT;
	inst->enc_params.framerate_num = DEFAULT_FRAMERATE_DENOM;
	inst->enc_params.framerate_denom = DEFAULT_FRAMERATE_NUM;
	inst->enc_params.gop_size = DEFAULT_GOP;

	INIT_WORK(&inst->encode_work, enc_pdata->dev_run_work);
	init_waitqueue_head(&inst->input_waitq);
	init_waitqueue_head(&inst->output_waitq);

	ret = mutex_lock_interruptible(&dev->dev_lock);
	if (ret)
		goto cleanup_inst;

	mutex_unlock(&dev->dev_lock);
	return 0;

cleanup_inst:
	v4l2_ctrl_handler_free(&inst->v4l2_ctrl_hdl);
err_m2m_release:
	v4l2_m2m_ctx_release(inst->v4l2_fh.m2m_ctx);
free_inst:
	kfree(inst);

	return ret;
}

static int rtkve_enc_release(struct file *file)
{
	struct vpu_instance *inst = rtkve_to_vpu_inst(file->private_data);
	struct vpu_device *dev = inst->dev;
	const struct rtkve_match_data *enc_pdata = dev->rtkve_mdata;

	mutex_lock(&inst->dev->dev_lock);
	v4l2_m2m_ctx_release(inst->v4l2_fh.m2m_ctx);

	if (inst->state != VPU_INST_STATE_NONE) {
		v4l2_m2m_suspend(inst->dev->m2m_dev);
		enc_pdata->destroy_instance(inst);
		v4l2_m2m_resume(inst->dev->m2m_dev);
	}
	mutex_unlock(&inst->dev->dev_lock);

	v4l2_ctrl_handler_free(&inst->v4l2_ctrl_hdl);
	v4l2_fh_del(&inst->v4l2_fh);
	v4l2_fh_exit(&inst->v4l2_fh);
	kfree(inst);
	inst = NULL;

	return 0;
}

static const struct v4l2_file_operations rtkve_enc_fops = {
	.owner = THIS_MODULE,
	.open = rtkve_enc_open,
	.release = rtkve_enc_release,
	.unlocked_ioctl = video_ioctl2,
	.poll = v4l2_m2m_fop_poll,
	.mmap = v4l2_m2m_fop_mmap,
};

static int rtkve_enc_register_device(struct vpu_device *dev)
{
	struct video_device *vdev_enc;
	int ret = 0;

	vdev_enc =
		devm_kzalloc(dev->v4l2_dev.dev, sizeof(*vdev_enc), GFP_KERNEL);
	if (!vdev_enc) {
		ret = -ENOMEM;
		dev_err(dev->dev, "alloc devm failed\n");
		goto exit;
	}

	dev->video_dev_enc = vdev_enc;

	strscpy(vdev_enc->name, VPU_ENC_DEV_NAME, sizeof(vdev_enc->name));
	vdev_enc->fops = &rtkve_enc_fops;
	vdev_enc->ioctl_ops = &rtkve_enc_ioctl_ops;
	vdev_enc->release = video_device_release_empty;
	vdev_enc->v4l2_dev = &dev->v4l2_dev;
	vdev_enc->vfl_dir = VFL_DIR_M2M;
	vdev_enc->device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;
	vdev_enc->lock = &dev->dev_lock;

	ret = video_register_device(vdev_enc, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(dev->dev, "video_register_device failed, ret %d\n",
			ret);
		goto exit;
	}

	video_set_drvdata(vdev_enc, dev);
exit:
	return ret;
}

static void rtkve_enc_unregister_device(struct vpu_device *dev)
{
	video_unregister_device(dev->video_dev_enc);
}

static int rtkve_enc_probe(struct platform_device *pdev)
{
	int ret;
	struct vpu_device *dev;

	dev_info(&pdev->dev, "%d.%s.enter\n", __LINE__, __func__);

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		dev_err(&pdev->dev, "alloc devm failed\n");
		ret = -ENOMEM;
		goto exit;
	}

	dev->rtkve_mdata = device_get_match_data(&pdev->dev);
	if (!dev->rtkve_mdata) {
		dev_err(&pdev->dev, "missing match_data\n");
		ret = -EINVAL;
		goto exit;
	}

	ret = of_reserved_mem_device_init(&pdev->dev);
	if (ret)
		dev_warn(&pdev->dev, "init reserved memory failed");

	mutex_init(&dev->dev_lock);
	mutex_init(&dev->hw_lock);
	dev_set_drvdata(&pdev->dev, dev);
	dev->dev = &pdev->dev;
	strscpy(dev->mdev.model, VPU_ENC_DRV_NAME, sizeof(dev->mdev.model));

	ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (ret) {
		dev_err(&pdev->dev, "v4l2_device_register fail: %d\n", ret);
		goto exit;
	}

	ret = rtkve_enc_register_device(dev);
	if (ret) {
		dev_err(&pdev->dev, "coda_vpu_enc_register_device fail: %d\n",
			ret);
		goto err_v4l2_unregister;
	}

	ret = rtkve_enc_init_m2m_dev(dev);
	if (ret)
		goto err_enc_unreg;

	dev->encode_workqueue = alloc_ordered_workqueue(
		VPU_ENC_DRV_NAME, WQ_MEM_RECLAIM | WQ_FREEZABLE);
	if (!dev->encode_workqueue) {
		dev_err(&pdev->dev, "Failed to create encode workqueue");
		ret = -EINVAL;
		goto err_enc_workq;
	}

	dev_info(&pdev->dev, "%d.%s.leave\n", __LINE__, __func__);
	return 0;

err_enc_workq:
	v4l2_m2m_release(dev->m2m_dev);
err_enc_unreg:
	rtkve_enc_unregister_device(dev);
err_v4l2_unregister:
	v4l2_device_unregister(&dev->v4l2_dev);
exit:
	dev_err(&pdev->dev, "%d.%s.leave.ret:%d\n", __LINE__, __func__, ret);
	return ret;
}

static int rtkve_enc_remove(struct platform_device *pdev)
{
	struct vpu_device *dev = dev_get_drvdata(&pdev->dev);

	if (dev->encode_workqueue)
		destroy_workqueue(dev->encode_workqueue);

	v4l2_m2m_release(dev->m2m_dev);
	rtkve_enc_unregister_device(dev);
	v4l2_device_unregister(&dev->v4l2_dev);

	return 0;
}

static const struct of_device_id rtkve_dt_ids[] = {
	{ .compatible = "realtek,rtd16xxb-ve3", .data = &rtkve3_data_stateful },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rtkve_dt_ids);

static struct platform_driver rtkve_enc_driver = {
	.driver = {
		.name = VPU_PLATFORM_DEVICE_NAME,
		.of_match_table = of_match_ptr(rtkve_dt_ids),
		},
	.probe = rtkve_enc_probe,
	.remove = rtkve_enc_remove,
};

module_platform_driver(rtkve_enc_driver);
MODULE_DESCRIPTION("RTK Video Engine V4L2 encode driver");
MODULE_LICENSE("Dual BSD/GPL");
