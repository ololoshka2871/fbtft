/*
 * FB driver for Two KS0108 LCD controllers in AGM1264K-FL display
 *
 * Copyright (C) 2014 ololoshka2871
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include "fbtft.h"

/* Uncomment text line to use negative image on display */
/*#define NEGATIVE*/

#define WHITE		0xff
#define BLACK		0

#define DRVNAME		"fb_agm1264k-fl"
#define WIDTH		64
#define HEIGHT		64
#define TOTALWIDTH	(WIDTH * 2)	 /* because 2 x ks0108 in one display */
#define FPS			20

#define EPIN		gpio.wr
#define RS			gpio.dc
#define RW			gpio.aux[2]
#define CS0			gpio.aux[0]
#define CS1			gpio.aux[1]
#define USE_DITHERING		gpio.aux[15] /* to store flag */


/* diffusing error (“Floyd-Steinberg”) */
#define DIFFUSING_MATRIX_WIDTH	2
#define DIFFUSING_MATRIX_HEIGHT	2

static const signed char
diffusing_matrix[DIFFUSING_MATRIX_WIDTH][DIFFUSING_MATRIX_HEIGHT] = {
	{-1, 3},
	{3, 2},
};

static const unsigned char gamma_correction_table[] = {
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
1, 1, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6,
6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 11, 11, 11, 12, 12, 13,
13, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 19, 19, 20, 20, 21,
22, 22, 23, 23, 24, 25, 25, 26, 26, 27, 28, 28, 29, 30, 30, 31, 32,
33, 33, 34, 35, 35, 36, 37, 38, 39, 39, 40, 41, 42, 43, 43, 44, 45,
46, 47, 48, 49, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 73, 74, 75, 76, 77, 78, 79, 81,
82, 83, 84, 85, 87, 88, 89, 90, 91, 93, 94, 95, 97, 98, 99, 100, 102,
103, 105, 106, 107, 109, 110, 111, 113, 114, 116, 117, 119, 120, 121,
123, 124, 126, 127, 129, 130, 132, 133, 135, 137, 138, 140, 141, 143,
145, 146, 148, 149, 151, 153, 154, 156, 158, 159, 161, 163, 165, 166,
168, 170, 172, 173, 175, 177, 179, 181, 182, 184, 186, 188, 190, 192,
194, 196, 197, 199, 201, 203, 205, 207, 209, 211, 213, 215, 217, 219,
221, 223, 225, 227, 229, 231, 234, 236, 238, 240, 242, 244, 246, 248,
251, 253, 255
};

static const char dithering[] = "dithering";
static const char threshold[] = "threshold";

static ssize_t show_convertmode(struct device *device,
				struct device_attribute *attr, char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	struct fbtft_par *par = fb_info->par;

	if (par->USE_DITHERING)
		return snprintf(buf, PAGE_SIZE, "[%s] %s",
				dithering, threshold);
	else
		return snprintf(buf, PAGE_SIZE, "%s [%s]",
				dithering, threshold);
}

static ssize_t store_convertmode(struct device *device,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	struct fbtft_par *par = fb_info->par;

	if (strcasecmp(buf, dithering) == 0)
		par->USE_DITHERING = 1;
	else if (strcasecmp(buf, threshold) == 0)
		par->USE_DITHERING = 0;
	else
		dev_err(par->info->device,
			"Incorrect mode setting: %s\n", buf);
	return count;
}

static struct device_attribute convertmode_device_attr =
	__ATTR(convertmode, 0660, show_convertmode, store_convertmode);

static void _sysfs_init(struct fbtft_par *par)
{
	device_create_file(par->info->dev, &convertmode_device_attr);
}

static void _sysfs_exit(struct fbtft_par *par)
{
	device_remove_file(par->info->dev, &convertmode_device_attr);
}

static int init_display(struct fbtft_par *par)
{
	u8 i;

	fbtft_par_dbg(DEBUG_INIT_DISPLAY, par, "%s()\n", __func__);

	par->fbtftops.reset(par);

	for (i = 0; i < 2; ++i) {
		write_reg(par, i, 0x3f); /* display on */
		write_reg(par, i, 0x40); /* set x to 0 */
		write_reg(par, i, 0xb0); /* set page to 0 */
		write_reg(par, i, 0xc0); /* set start line to 0 */
	}

	_sysfs_init(par);

	return 0;
}

void reset(struct fbtft_par *par)
{
	if (par->gpio.reset == -1)
		return;

	fbtft_dev_dbg(DEBUG_RESET, par, par->info->device, "%s()\n", __func__);

	gpio_set_value(par->gpio.reset, 0);
	udelay(20);
	gpio_set_value(par->gpio.reset, 1);
	mdelay(120);
}

/* Check if all necessary GPIOS defined */
static int verify_gpios(struct fbtft_par *par)
{
	int i;

	fbtft_dev_dbg(DEBUG_VERIFY_GPIOS, par, par->info->device,
		"%s()\n", __func__);

	if (par->EPIN < 0) {
		dev_err(par->info->device,
			"Missing info about 'wr' (aka E) gpio. Aborting.\n");
		return -EINVAL;
	}
	for (i = 0; i < 8; ++i) {
		if (par->gpio.db[i] < 0) {
			dev_err(par->info->device,
				"Missing info about 'db[%i]' gpio. Aborting.\n",
				i);
			return -EINVAL;
		}
	}
	if (par->CS0 < 0) {
		dev_err(par->info->device,
			"Missing info about 'cs0' gpio. Aborting.\n");
		return -EINVAL;
	}
	if (par->CS1 < 0) {
		dev_err(par->info->device,
			"Missing info about 'cs1' gpio. Aborting.\n");
		return -EINVAL;
	}
	if (par->RW < 0) {
		dev_err(par->info->device,
			"Missing info about 'rw' gpio. Aborting.\n");
		return -EINVAL;
	}

	return 0;
}

static unsigned long
request_gpios_match(struct fbtft_par *par, const struct fbtft_gpio *gpio)
{
	fbtft_dev_dbg(DEBUG_REQUEST_GPIOS_MATCH, par, par->info->device,
		"%s('%s')\n", __func__, gpio->name);

	if (strcasecmp(gpio->name, "wr") == 0) {
		/* left ks0108 E pin */
		par->EPIN = gpio->gpio;
		return GPIOF_OUT_INIT_LOW;
	} else if (strcasecmp(gpio->name, "cs0") == 0) {
		/* left ks0108 controller pin */
		par->CS0 = gpio->gpio;
		return GPIOF_OUT_INIT_HIGH;
	} else if (strcasecmp(gpio->name, "cs1") == 0) {
		/* right ks0108 controller pin */
		par->CS1 = gpio->gpio;
		return GPIOF_OUT_INIT_HIGH;
	}

	/* if write (rw = 0) e(1->0) perform write */
	/* if read (rw = 1) e(0->1) set data on D0-7*/
	else if (strcasecmp(gpio->name, "rw") == 0) {
		par->RW = gpio->gpio;
		return GPIOF_OUT_INIT_LOW;
	}

	return FBTFT_GPIO_NO_MATCH;
}

/* This function oses to enter commands
 * first byte - destination controller 0 or 1
 * folowing - commands
 */
static void write_reg8_bus8(struct fbtft_par *par, int len, ...)
{
	va_list args;
	int i, ret;
	u8 *buf = (u8 *)par->buf;

	if (unlikely(par->debug & DEBUG_WRITE_REGISTER)) {
		va_start(args, len);
		for (i = 0; i < len; i++)
			buf[i] = (u8)va_arg(args, unsigned int);

		va_end(args);
		fbtft_par_dbg_hex(DEBUG_WRITE_REGISTER, par,
			par->info->device, u8, buf, len, "%s: ", __func__);
	}

	va_start(args, len);

	*buf = (u8)va_arg(args, unsigned int);

	if (*buf > 1) {
		va_end(args);
		dev_err(par->info->device, "%s: Incorrect chip sellect request (%d)\n",
			__func__, *buf);
		return;
	}

	/* select chip */
	if (*buf) {
		/* cs1 */
		gpio_set_value(par->CS0, 1);
		gpio_set_value(par->CS1, 0);
	} else {
		/* cs0 */
		gpio_set_value(par->CS0, 0);
		gpio_set_value(par->CS1, 1);
	}

	gpio_set_value(par->RS, 0); /* RS->0 (command mode) */
	len--;

	if (len) {
		i = len;
		while (i--)
			*buf++ = (u8)va_arg(args, unsigned int);
		ret = par->fbtftops.write(par, par->buf, len * (sizeof(u8)));
		if (ret < 0) {
			va_end(args);
			dev_err(par->info->device, "%s: write() failed and returned %d\n",
				__func__, ret);
			return;
		}
	}

	va_end(args);
}

static struct
{
	int xs, ys_page, xe, ye_page;
} addr_win;

/* save display writing zone */
static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	addr_win.xs = xs;
	addr_win.ys_page = ys / 8;
	addr_win.xe = xe;
	addr_win.ye_page = ye / 8;

	fbtft_par_dbg(DEBUG_SET_ADDR_WIN, par,
		"%s(xs=%d, ys_page=%d, xe=%d, ye_page=%d)\n", __func__,
		addr_win.xs, addr_win.ys_page, addr_win.xe, addr_win.ye_page);
}

static void
construct_line_bitmap(struct fbtft_par *par, u8 *dest, signed short *src,
						int xs, int xe, int y)
{
	int x, i;

	for (x = xs; x < xe; ++x) {
		u8 res = 0;

		for (i = 0; i < 8; i++)
			if (src[(y * 8 + i) * par->info->var.xres + x])
				res |= 1 << i;
#ifdef NEGATIVE
		*dest++ = res;
#else
		*dest++ = ~res;
#endif
	}
}

static int write_vmem(struct fbtft_par *par, size_t offset, size_t len)
{
	u16 *vmem16 = (u16 *)par->info->screen_base;
	u8 *buf = par->txbuf.buf;
	int x, y;
	int ret = 0;

	/* buffer to convert RGB565 -> grayscale16 -> Ditherd image 1bpp */
	signed short *convert_buf = kmalloc(par->info->var.xres *
		par->info->var.yres * sizeof(signed short), GFP_NOIO);

	fbtft_par_dbg(DEBUG_WRITE_VMEM, par, "%s()\n", __func__);

	/* converting to grayscale16 */
	for (x = 0; x < par->info->var.xres; ++x)
		for (y = 0; y < par->info->var.yres; ++y) {
			u16 pixel = vmem16[y *  par->info->var.xres + x];
			u16 b = pixel & 0x1f;
			u16 g = (pixel & (0x3f << 5)) >> 5;
			u16 r = (pixel & (0x1f << (5 + 6))) >> (5 + 6);

			pixel = (299 * r + 587 * g + 114 * b) / 200;
			if (pixel > 255)
				pixel = 255;

			/* gamma-correction by table */
			convert_buf[y *  par->info->var.xres + x] =
				(signed short)gamma_correction_table[pixel];
		}

	/* Image Dithering */
	for (x = 0; x < par->info->var.xres; ++x)
		for (y = 0; y < par->info->var.yres; ++y) {
			signed short pixel =
				convert_buf[y *  par->info->var.xres + x];
			signed short error_b = pixel - BLACK;
			signed short error_w = pixel - WHITE;
			signed short error;
			u16 i, j;

			/* what color close? */
			if (abs(error_b) >= abs(error_w)) {
				/* white */
				error = error_w;
				pixel = 0xff;
			} else {
				/* black */
				error = error_b;
				pixel = 0;
			}

			error /= 8;

			/* diffusion matrix row */
			for (i = 0; i < DIFFUSING_MATRIX_WIDTH; ++i)
				/* diffusion matrix column */
				for (j = 0; j < DIFFUSING_MATRIX_HEIGHT; ++j) {
					signed short *write_pos;
					signed char coeff;

					/* skip pixels out of zone */
					if (x + i < 0 ||
						x + i >= par->info->var.xres
						|| y + j >= par->info->var.yres)
						continue;
					write_pos = &convert_buf[
						(y + j) * par->info->var.xres +
						x + i];
					coeff = diffusing_matrix[i][j];
					if (coeff == -1)
						/* pixel itself */
						*write_pos = pixel;
					else {
						signed short p = *write_pos +
							error * coeff;

						if (p > WHITE)
							p = WHITE;
						if (p < BLACK)
							p = BLACK;
						*write_pos = p;
					}
				}
		}

	 /* 1 string = 2 pages */
	 for (y = addr_win.ys_page; y <= addr_win.ye_page; ++y) {
		/* left half of display */
		if (addr_win.xs < par->info->var.xres / 2) {
			construct_line_bitmap(par, buf, convert_buf,
				addr_win.xs, par->info->var.xres / 2, y);

			len = par->info->var.xres / 2 - addr_win.xs;

			/* select left side (sc0)
			 * set addr
			 */
			write_reg(par, 0x00, (1 << 6) | (u8)addr_win.xs);
			write_reg(par, 0x00, (0x17 << 3) | (u8)y);

			/* write bitmap */
			gpio_set_value(par->RS, 1); /* RS->1 (data mode) */
			ret = par->fbtftops.write(par, buf, len);
			if (ret < 0)
				dev_err(par->info->device,
					"%s: write failed and returned: %d\n",
					__func__, ret);
		}
		/* right half of display */
		if (addr_win.xe >= par->info->var.xres / 2) {
			construct_line_bitmap(par, buf,
				convert_buf, par->info->var.xres / 2,
				addr_win.xe + 1, y);

			len = addr_win.xe + 1 - par->info->var.xres / 2;

			/* select right side (sc1)
			 * set addr
			 */
			write_reg(par, 0x01, (1 << 6));
			write_reg(par, 0x01, (0x17 << 3) | (u8)y);

			/* write bitmap */
			gpio_set_value(par->RS, 1); /* RS->1 (data mode) */
			par->fbtftops.write(par, buf, len);
			if (ret < 0)
				dev_err(par->info->device,
					"%s: write failed and returned: %d\n",
					__func__, ret);
		}
	}
	kfree(convert_buf);

	gpio_set_value(par->CS0, 1);
	gpio_set_value(par->CS1, 1);

	return ret;
}

static int write(struct fbtft_par *par, void *buf, size_t len)
{
	fbtft_par_dbg_hex(DEBUG_WRITE, par, par->info->device, u8, buf, len,
		"%s(len=%d): ", __func__, len);

	gpio_set_value(par->RW, 0); /* set write mode */


	while (len--) {
		u8 i, data;

		data = *(u8 *) buf++;

		/* set data bus */
		for (i = 0; i < 8; ++i)
			gpio_set_value(par->gpio.db[i], data & (1 << i));
		/* set E */
		gpio_set_value(par->EPIN, 1);
		udelay(5);
		/* unset E - write */
		gpio_set_value(par->EPIN, 0);
		udelay(1);
	}

	return 0;
}

static struct fbtft_display display = {
	.regwidth = 8,
	.width = TOTALWIDTH,
	.height = HEIGHT,
	.fps = FPS,
	.fbtftops = {
		.init_display = init_display,
		.set_addr_win = set_addr_win,
		.verify_gpios = verify_gpios,
		.request_gpios_match = request_gpios_match,
		.reset = reset,
		.write = write,
		.write_register = write_reg8_bus8,
		.write_vmem = write_vmem,
	},
};

#define FBTFT_REGISTER_DRIVER_V_SYS(_name, _compatible, _display, sysfs_unreg) \
									   \
static int fbtft_driver_probe_spi(struct spi_device *spi)                  \
{                                                                          \
	return fbtft_probe_common(_display, spi, NULL);                    \
}                                                                          \
									   \
static int fbtft_driver_remove_spi(struct spi_device *spi)                 \
{                                                                          \
	struct fb_info *info = spi_get_drvdata(spi);                       \
									   \
	return fbtft_remove_common(&spi->dev, info);                       \
}                                                                          \
									   \
static int fbtft_driver_probe_pdev(struct platform_device *pdev)           \
{                                                                          \
	return fbtft_probe_common(_display, NULL, pdev);                   \
}                                                                          \
									   \
static int fbtft_driver_remove_pdev(struct platform_device *pdev)          \
{                                                                          \
	struct fb_info *info = platform_get_drvdata(pdev);                 \
									   \
	sysfs_unreg(info->par);						   \
	return fbtft_remove_common(&pdev->dev, info);                      \
}                                                                          \
									   \
static const struct of_device_id dt_ids[] = {                              \
		{ .compatible = _compatible },                             \
		{},                                                        \
};                                                                         \
									   \
MODULE_DEVICE_TABLE(of, dt_ids);                                           \
									   \
									   \
static struct spi_driver fbtft_driver_spi_driver = {                       \
	.driver = {                                                        \
		.name   = _name,                                           \
		.owner  = THIS_MODULE,                                     \
		.of_match_table = of_match_ptr(dt_ids),                    \
	},                                                                 \
	.probe  = fbtft_driver_probe_spi,                                  \
	.remove = fbtft_driver_remove_spi,                                 \
};                                                                         \
									   \
static struct platform_driver fbtft_driver_platform_driver = {             \
	.driver = {                                                        \
		.name   = _name,                                           \
		.owner  = THIS_MODULE,                                     \
		.of_match_table = of_match_ptr(dt_ids),                    \
	},                                                                 \
	.probe  = fbtft_driver_probe_pdev,                                 \
	.remove = fbtft_driver_remove_pdev,                                \
};                                                                         \
									   \
static int __init fbtft_driver_module_init(void)                           \
{                                                                          \
	int ret;                                                           \
									   \
	ret = spi_register_driver(&fbtft_driver_spi_driver);               \
	if (ret < 0)                                                       \
		return ret;                                                \
	return platform_driver_register(&fbtft_driver_platform_driver);	   \
}                                                                          \
									   \
static void __exit fbtft_driver_module_exit(void)                          \
{                                                                          \
	spi_unregister_driver(&fbtft_driver_spi_driver);                   \
	platform_driver_unregister(&fbtft_driver_platform_driver);         \
}                                                                          \
									   \
module_init(fbtft_driver_module_init);                                     \
module_exit(fbtft_driver_module_exit);

FBTFT_REGISTER_DRIVER_V_SYS(DRVNAME, "displaytronic,fb_agm1264k-fl",
				&display, _sysfs_exit);

MODULE_ALIAS("platform:" DRVNAME);

MODULE_DESCRIPTION("Two KS0108 LCD controllers in AGM1264K-FL display");
MODULE_AUTHOR("ololoshka2871");
MODULE_LICENSE("GPL");

