// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)

#include <linux/v4l2-common.h>
#include "rtkve-common.h"

struct vpu_buf *rtkve_allocate_dma_memory(struct device *dev, size_t size)
{
	struct vpu_buf *buf_hdl = NULL;
	void *vaddr;
	dma_addr_t dma_addr;

	if (!size) {
		dev_err(dev, "%s(): requested size==0\n", __func__);
		goto exit;
	}
#if 0
	if (size < SZ_8K)
		size = SZ_8K;
#endif
	buf_hdl = kzalloc(sizeof(*buf_hdl), GFP_KERNEL);
	if (!buf_hdl) {
		dev_err(dev, "%s allocate vpu_buf fail, No Memory\n", __func__);
		goto exit;
	}

	vaddr = dma_alloc_coherent(dev, size, &dma_addr, GFP_KERNEL);
	if (!vaddr) {
		dev_err(dev, "%s dma_alloc fail \n", __func__);
		kfree(buf_hdl);
		buf_hdl = NULL;
		goto exit;
	}

	buf_hdl->vaddr = vaddr;
	buf_hdl->daddr = dma_addr;
	buf_hdl->size = size;
	memset(buf_hdl->vaddr, 0, size);
exit:
	return buf_hdl;
}

void rtkve_free_dma_memory(struct device *dev, struct vpu_buf *vb)
{
	if (vb->size == 0)
		goto exit;

	if (!vb->vaddr)
		dev_err(dev, "%s(): requested free of unmapped buffer\n",
			__func__);
	else
		dma_free_coherent(dev, vb->size, vb->vaddr, vb->daddr);

	kfree(vb);
exit:
	return;
}

void word_endian_convert(uint8_t *dst, uint8_t *src, int len)
{
	int i;
	uint16_t *s_ptr = (uint16_t *)src;
	uint16_t *d_ptr = (uint16_t *)dst;
	int size = len / sizeof(uint16_t);

	for (i = 0; i < size; i++) {
		d_ptr[i] = htons(s_ptr[i]);
	}
}

void dword_endian_convert(uint8_t *dst, uint8_t *src, int len)
{
	int i;
	uint32_t *s_ptr = (uint32_t *)src;
	uint32_t *d_ptr = (uint32_t *)dst;
	int size = len / sizeof(uint32_t);

	for (i = 0; i < size; i++) {
		d_ptr[i] = htonl(s_ptr[i]);
	}
}

uint64_t htonll(long long val)
{
	return (((long long)htonl(val)) << 32) + htonl(val >> 32);
}

void qword_endian_convert(uint8_t *dst, uint8_t *src, int len)
{
	int i;
	uint64_t *s_ptr = (uint64_t *)src;
	uint64_t *d_ptr = (uint64_t *)dst;
	int size = len / sizeof(uint64_t);

	for (i = 0; i < size; i++) {
		d_ptr[i] = htonll(s_ptr[i]);
	}
}
