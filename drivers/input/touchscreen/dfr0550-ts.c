// SPDX-License-Identifier: GPL-2.0
/*
 * DFROBOT DFR0550 touchscreen driver
 *
 * These touchscreen displays are intended to be compatible with the official
 * Raspberry Pi 7in display which has an FTx06 touch controller directly
 * attached to the 15pin connector to the host processor. However these
 * displays have an FTx06 touch controller that connected to an I2C master
 * on a STM32F103 micro controller which polls the FTx06 and emulates a
 * virtual I2C device connected to the 15pin connector to the host processor.
 * The emulated FTx06 implements a subset of the FTx06 register set but
 * must be read with individual transactions between reading the number
 * of points and the point data itself.
 *
 * Additionally there is no IRQ made available so this is a polling driver.
 *
 * Copyright (C) 2015, 2017 Raspberry Pi
 * Copyright (C) 2018 Nicolas Saenz Julienne <nsaenzjulienne@suse.de>
 */
#include <linux/of.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/input/mt.h>
// #include <linux/input-polldev.h>
#include <linux/input/touchscreen.h>
#include <linux/i2c.h>

#define TS_DEFAULT_WIDTH 800
#define TS_DEFAULT_HEIGHT 480
#define TS_MAX_SUPPORTED_POINTS 5
#define TS_FTS_TOUCH_DOWN 0
#define TS_FTS_TOUCH_CONTACT 2
#define TS_POLL_INTERVAL 17	/* 60fps */
#define TS_NPOINTS_REG_INVALIDATE 99

struct dfr0550_ts {
	struct i2c_client *i2c;
	struct input_dev *input;
	struct touchscreen_properties prop;
	unsigned int rotate;
	int known_ids;
};

static int dfr0550_i2c_read(struct i2c_client *client, u8 reg, u8 *buf, int len)
{
	struct i2c_msg msgs[2];
	int ret;
	msgs[0].flags = 0;
	msgs[0].addr = client->addr;
	msgs[0].len = 1;
	msgs[0].buf = &reg;
	msgs[1].flags = I2C_M_RD;
	msgs[1].addr = client->addr;
	msgs[1].len = len;
	msgs[1].buf = buf;
	ret = i2c_transfer(client->adapter, msgs, 2);
	return reg < 0 ? ret : (ret != ARRAY_SIZE(msgs) ? -EIO : 0);
}

static void dfr0550_ts_poll(struct input_dev *input)
{
	struct dfr0550_ts *ts = input_get_drvdata(input);
	// struct input_dev *input = dev->input;
	// struct dfr0550_ts *ts = dev->private;
	int modified_ids = 0;
	long released_ids;
	int points, i;
	int event_type;
	int touchid;
	int x, y;
	u8 buf[4];

	dfr0550_i2c_read(ts->i2c, 0x2, buf, 1);

	if (buf[0] == 0xff)
		return;

	points = min(buf[0] & 0xf, 5);

	for (i = 0; i < points; i++) {
		dfr0550_i2c_read(ts->i2c, 3+6*i, buf, 4);
		x = ((((int)buf[0] & 0xf) << 8) + buf[1]);
		y = ((((int)buf[2] & 0xf) << 8) + buf[3]);
		if (ts->rotate == 180) {
			x = TS_DEFAULT_WIDTH - x;
			y = TS_DEFAULT_HEIGHT - y;
		}
		touchid = (buf[2] >> 4) & 0xf;
		event_type = (buf[0] >> 6) & 0x03;
		modified_ids |= BIT(touchid);
		if (event_type == TS_FTS_TOUCH_DOWN ||
		    event_type == TS_FTS_TOUCH_CONTACT) {
			input_mt_slot(input, touchid);
			input_mt_report_slot_state(input, MT_TOOL_FINGER, 1);
			touchscreen_report_pos(input, &ts->prop, x, y, true);
		}
		dev_dbg(&ts->i2c->dev, "rotate: %d, point[%d]: %d, %d\n", ts->rotate, i, x, y);
	}

	released_ids = ts->known_ids & ~modified_ids;
	for_each_set_bit(i, &released_ids, TS_MAX_SUPPORTED_POINTS) {
		input_mt_slot(input, i);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, 0);
		modified_ids &= ~(BIT(i));
	}

	ts->known_ids = modified_ids;
	input_mt_sync_frame(input);
	input_sync(input);
}

static int dfr0550_ts_i2c_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	// struct input_polled_dev *poll_dev;
	struct input_dev *input;
	struct dfr0550_ts *ts;
	int error;

	ts = devm_kzalloc(dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->i2c = i2c;

	i2c_set_clientdata(i2c, ts);

	input = devm_input_allocate_device(dev);
	if (!input) {
		dev_err(dev, "Failed to allocate input device\n");
		return -ENOMEM;
	}

	ts->input = input;
	input_set_drvdata(input, ts);

	// input = poll_dev->input;

	input->name = "dfr0550-ts";
	input->id.bustype = BUS_HOST;

	// poll_dev->poll_interval = TS_POLL_INTERVAL;
	// poll_dev->poll = dfr0550_ts_poll;
	// poll_dev->private = ts;

	input_set_abs_params(input, ABS_MT_POSITION_X, 0,
			     TS_DEFAULT_WIDTH, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0,
			     TS_DEFAULT_HEIGHT, 0, 0);
	touchscreen_parse_properties(input, true, &ts->prop);

	error = input_mt_init_slots(input, TS_MAX_SUPPORTED_POINTS,
				    INPUT_MT_DIRECT);
	if (error) {
		dev_err(dev, "could not init mt slots, %d\n", error);
		return error;
	}

	error = input_setup_polling(input, dfr0550_ts_poll);
	if (error) {
		dev_err(dev, "could not set up polling mode, %d\n", error);
		return error;
	}

	input_set_poll_interval(input, TS_POLL_INTERVAL);

	of_property_read_u32(dev->of_node, "rotate", &ts->rotate);
	if (ts->rotate != 0 && ts->rotate != 180) {
		dev_err(dev, "can't support rotate %d, only support 180\n", ts->rotate);
		ts->rotate = 0;
	}

	error = input_register_device(input);
	if (error) {
		dev_err(dev, "could not register input device, %d\n", error);
		return error;
	}

	return 0;
}

static void dfr0550_ts_i2c_remove(struct i2c_client *i2c)
{
	return;
}

static const struct of_device_id dfr0550_ts_dt_ids[] = {
	{ .compatible = "realtek,dfr0550_ts", },
	{},
};

static struct i2c_driver dfr0550_ts_i2c_driver = {
	.driver = {
		.name = "dfr0550_ts_i2c",
		.of_match_table =  of_match_ptr(dfr0550_ts_dt_ids),
	},
	.probe = dfr0550_ts_i2c_probe,
	.remove = dfr0550_ts_i2c_remove,
};

static int __init dfr0550_ts_init(void)
{
	int err;

	err = i2c_add_driver(&dfr0550_ts_i2c_driver);
	if (err != 0) {
		pr_err("rtk dsi add i2c driver fail\n");
		return -EFAULT;
	}

	return 0;
}

static void __exit dfr0550_ts_exit(void)
{
	i2c_del_driver(&dfr0550_ts_i2c_driver);
}

module_init(dfr0550_ts_init);
module_exit(dfr0550_ts_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DFR0550 touchscreen kernel module");
