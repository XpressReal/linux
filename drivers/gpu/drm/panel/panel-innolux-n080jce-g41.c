/*
 * Innolux N080JCE-G41 Panel Driver
 *
 * Copyright (c) 2024 Realtek Semiconductor Corp.
 *
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>


#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>

struct n080jce_g41 {
	struct drm_panel panel;
	struct mipi_dsi_device	*dsi;
};

static inline struct n080jce_g41 *panel_to_n080jce_g41(struct drm_panel *panel)
{
	return container_of(panel, struct n080jce_g41, panel);
}

static int n080jce_g41_prepare(struct drm_panel *panel)
{
	return 0;
}

static int n080jce_g41_enable(struct drm_panel *panel)
{
	return 0;
}

static int n080jce_g41_disable(struct drm_panel *panel)
{
	return 0;
}

static int n080jce_g41_unprepare(struct drm_panel *panel)
{
	return 0;
}

static const struct drm_display_mode n080jce_g41_mode = {
	.clock = 176863680 / 1000,
	.hdisplay = 1200,
	.hsync_start = 1200 + 162,
	.hsync_end = 1200 + 162 + 122,
	.htotal = 1200 + 162 + 122 + 4,
	.vdisplay = 1920,
	.vsync_start = 1920 + 35,
	.vsync_end = 1920 + 35 + 25,
	.vtotal = 1920 + 35 + 25 + 1,

	.width_mm = 120,
	.height_mm = 192,
};

static int n080jce_g41_get_modes(struct drm_panel *panel,
			      struct drm_connector *connector)
{
	struct n080jce_g41 *ctx = panel_to_n080jce_g41(panel);
	const struct drm_display_mode *m = &n080jce_g41_mode;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, m);
	if (!mode) {
		dev_err(&ctx->dsi->dev, "failed to add mode %ux%ux@%u\n",
			m->hdisplay,
			m->vdisplay,
			drm_mode_vrefresh(m));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

	return 1;
}

static const struct drm_panel_funcs n080jce_g41_funcs = {
	.prepare	= n080jce_g41_prepare,
	.unprepare	= n080jce_g41_unprepare,
	.enable		= n080jce_g41_enable,
	.disable	= n080jce_g41_disable,
	.get_modes	= n080jce_g41_get_modes,
};

static int n080jce_g41_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct n080jce_g41 *ctx;
	int ret;

	ctx = devm_kzalloc(&dsi->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dsi = dsi;

	drm_panel_init(&ctx->panel, &dsi->dev, &n080jce_g41_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	drm_panel_add(&ctx->panel);

	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_SYNC_PULSE;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->lanes = 4;

	ret = mipi_dsi_attach(dsi);
	if (ret)
		dev_err(&dsi->dev, "failed to attach dsi to host: %d\n", ret);

	return ret;
}

static void n080jce_g41_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct n080jce_g41 *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id n080jce_g41_of_match[] = {
	{ .compatible = "innolux,n080jce-g41"},
	{ }
};
MODULE_DEVICE_TABLE(of, n080jce_g41_of_match);

static struct mipi_dsi_driver n080jce_g41_dsi_driver = {
	.probe		= n080jce_g41_dsi_probe,
	.remove		= n080jce_g41_dsi_remove,
	.driver = {
		.name		= "panel-n080jce-g41",
		.of_match_table	= n080jce_g41_of_match,
	},
};
module_mipi_dsi_driver(n080jce_g41_dsi_driver);

MODULE_AUTHOR("Ray Tang <ray.tang@realtek.com>");
MODULE_DESCRIPTION("Innolux N080JCE-G41 Panel Driver");
MODULE_LICENSE("GPL v2");
