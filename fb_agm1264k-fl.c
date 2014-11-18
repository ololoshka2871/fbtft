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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/delay.h>

#include "fbtft.h"

#define DRVNAME		"fb_agm1264k-fl"
#define WIDTH		64
#define HEIGHT		64
#define TOTALWIDTH	(WIDTH * 2)	 // because 2 x ks0108 in one display
#define FPS			20

#define EPIN		gpio.wr
#define RS			gpio.dc
#define RW			gpio.aux[2]
#define CS0			gpio.aux[0]
#define CS1			gpio.aux[1]


// diffusing error (“Floyd-Steinberg”)
#define DIFFUSING_MATRIX_WIDTH	2
#define DIFFUSING_MATRIX_HEIGHT	2

static const signed char
diffusing_matrix[DIFFUSING_MATRIX_WIDTH][DIFFUSING_MATRIX_HEIGHT] = {
	{-1, 3},
	{3, 2},
};


static int init_display(struct fbtft_par *par)
{
	u8 i;
	fbtft_par_dbg(DEBUG_INIT_DISPLAY, par, "%s()\n", __func__);

	par->fbtftops.reset(par);

	for (i = 0; i < 2; ++i)
	{
		write_reg(par, i, 0b00111111); // display on
		write_reg(par, i, 0b01000000); // set x to 0
		write_reg(par, i, 0b10111000); // set page to 0
		write_reg(par, i, 0b11000000); // set start line to 0
	}

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

// Check if all necessary GPIOS defined
static int verify_gpios(struct fbtft_par *par)
{
	int i;
	fbtft_dev_dbg(DEBUG_VERIFY_GPIOS, par, par->info->device,
		"%s()\n", __func__);

    if (par->EPIN < 0) {
        dev_err(par->info->device, "Missing info about 'wr' (aka E) gpio. Aborting.\n");
        return -EINVAL;
    }
    for (i = 0; i < 8; ++i)
	    if (par->gpio.db[i] < 0) {
    	    dev_err(par->info->device,
				"Missing info about 'db[%i]' gpio. Aborting.\n", i);
    	    return -EINVAL;
    	}
    if (par->CS0 < 0) {
        dev_err(par->info->device, "Missing info about 'cs0' gpio. Aborting.\n");
        return -EINVAL;
    }
    if (par->CS1 < 0) {
        dev_err(par->info->device, "Missing info about 'cs1' gpio. Aborting.\n");
        return -EINVAL;
    }
    if (par->RW < 0) {
        dev_err(par->info->device, "Missing info about 'rw' gpio. Aborting.\n");
        return -EINVAL;
    }

    return 0;
}

static unsigned long
request_gpios_match(struct fbtft_par *par, const struct fbtft_gpio *gpio)
{
    fbtft_dev_dbg(DEBUG_REQUEST_GPIOS_MATCH, par, par->info->device, 
    	"%s('%s')\n", __func__, gpio->name);

    if (strcasecmp(gpio->name, "wr") == 0) { // left ks0108 E pin
        par->EPIN = gpio->gpio;
        return GPIOF_OUT_INIT_LOW;
    }

    if (strcasecmp(gpio->name, "cs0") == 0) { // left ks0108 controller pin
        par->CS0 = gpio->gpio;
        return GPIOF_OUT_INIT_HIGH;
    }
    else if (strcasecmp(gpio->name, "cs1") == 0) { // right ks0108 controller pin
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
		for (i = 0; i < len; i++) {
			buf[i] = (u8)va_arg(args, unsigned int);
		}
		va_end(args);
		fbtft_par_dbg_hex(DEBUG_WRITE_REGISTER, par, par->info->device, u8, 
			buf, len, "%s: ", __func__);
	}

	va_start(args, len);

	*buf = (u8)va_arg(args, unsigned int);

	if (*buf > 1)
	{
		va_end(args);
		dev_err(par->info->device, "%s: Incorrect chip sellect request (%d)\n",
			__func__, *buf);
		return;
	}

	// select chip
	if (*buf)
	{ // cs1
		gpio_set_value(par->CS0, 1);
		gpio_set_value(par->CS1, 0);
	}
	else
	{ // cs0
		gpio_set_value(par->CS0, 0);
		gpio_set_value(par->CS1, 1);
	}

	gpio_set_value(par->RS, 0); // RS->0 (command mode)
	len--;

	if (len) {
		i = len;
		while (i--) {
			*buf++ = (u8)va_arg(args, unsigned int);
		}
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
construct_line_bitmap(struct fbtft_par *par, u8* dest, signed short *src, int xs,
						int xe, int y)
{
/*
	int x, i;
	for (x = xs; x < xe; ++x)
	{
		u8 res = 0;
		for (i = 0; i < 8; i++)
			if(src[(y * 8 + i) * par->info->var.xres + x])
				res |= 1 << i;
		*dest++ = res;
	}
*/
}

static int write_vmem(struct fbtft_par *par, size_t offset, size_t len)
{
	u16 *vmem16 = (u16 *)par->info->screen_base;
	u8 *buf = par->txbuf.buf;
	int x, y;
	int ret = 0;

	// buffer to convert RGB565 -> grayscale16 -> Ditherd image 1bpp
	signed short convert_buf[par->info->var.xres * par->info->var.yres];

	fbtft_par_dbg(DEBUG_WRITE_VMEM, par, "%s()\n", __func__);

	// converting to grayscale16
	for (x = 0; x < par->info->var.xres; ++x)
		for (y = 0; y < par->info->var.yres; ++y)
		{
			u16 pixel = vmem16[y *  par->info->var.xres + x];
			u16 b = pixel & 0b11111;
			u16 g = (pixel & (0b111111 << 5)) >> 5;
			u16 r = (pixel & (0b11111 << (5 + 6))) >> (5 + 6);
			convert_buf[y *  par->info->var.xres + x] =
				(r + g + b) / 3;
		}

	// Image Dithering
	for (x = 0; x < par->info->var.xres; ++x)
		for (y = 0; y < par->info->var.yres; ++y)
		{
			signed short black = 0;
			signed short white = 0xff;
			signed short pixel = convert_buf[y *  par->info->var.xres + x];
			signed short error_b = pixel - black;
			signed short error_w = pixel - white;
			signed short error;
			u16 i, j;
			// what color close
			if (abs(black) > abs(white))
			{
				// white
				error = error_w;
				pixel = white;
			}
			else
			{	// black
				error = error_b;
				pixel = black;
			}

			error /= 8;
			// diffusion matrix row
			for (i = 0; i < DIFFUSING_MATRIX_WIDTH; ++i)
				// diffusion matrix column
				for (j = 0; j < DIFFUSING_MATRIX_HEIGHT; ++j)
				{
					signed short *write_pos;
					signed char coeff;
					// skip pixels out of zone
					if ((x + i < 0) || (x + i >= par->info->var.xres)
						|| (y + j >= par->info->var.yres))
						continue;
					write_pos = &convert_buf[
							(y + j) *  par->info->var.xres +
							x + i];
					coeff = diffusing_matrix[i][j];
					if (coeff == -1)
						*write_pos = pixel; // pixel itself
					else
						*write_pos += error * coeff;
				}
		}

	 // 1 одна строка - 2 страницы
	 for (y = addr_win.ys_page; y <= addr_win.ye_page; ++y)
	 {
	 	// left half of display
	 	if (addr_win.xs < par->info->var.xres / 2)
		{
			construct_line_bitmap(par, buf, convert_buf, addr_win.xs, 
		 		par->info->var.xres / 2, y);

			len = par->info->var.xres / 2 - addr_win.xs;

			// выбрать левую половину (sc0)
			// установить адрес
			write_reg(par, 0x00, (0b01 << 6) | (u8)addr_win.xs); // x
			write_reg(par, 0x00, (0b10111 << 3) | (u8)y); // page

	 		// записать битмап
			gpio_set_value(par->RS, 1); // RS->1 (data mode)
			ret = par->fbtftops.write(par, buf, len);
			if (ret < 0)
				dev_err(par->info->device,
					"%s: write failed and returned: %d\n", __func__, ret);
		}
		// right half of display
		if (addr_win.xe >= par->info->var.xres / 2)
		{
			construct_line_bitmap(par, buf, convert_buf,
				par->info->var.xres / 2, addr_win.xe + 1,y);

			len = addr_win.xe + 1 - par->info->var.xres / 2;

			// выбрать правую половину (sc0)
			// установить адрес
			write_reg(par, 0x01, (0b01 << 6) | (u8)0); // x
			write_reg(par, 0x01, (0b10111 << 3) | (u8)y); // page

			// записать битмап
			gpio_set_value(par->RS, 1); // RS->1 (data mode)
		 	par->fbtftops.write(par, buf, len);
			if (ret < 0)
				dev_err(par->info->device,
					"%s: write failed and returned: %d\n", __func__, ret);
		}
	}

	gpio_set_value(par->CS0, 1);
	gpio_set_value(par->CS1, 1);

	return ret;
}

/*
 * тупая запись, что пришло в массиве, то и записать
 * используется только шина par->gpio.db и par->gpio.E = latch
 * rs должна быть установлена до записи
 * CSx должна быть установлена до записи
 */
static int write(struct fbtft_par *par, void *buf, size_t len)
{
	fbtft_par_dbg_hex(DEBUG_WRITE, par, par->info->device, u8, buf, len,
		"%s(len=%d): ", __func__, len);

	gpio_set_value(par->RW, 0); // set write mode


	while (len--) {
		u8 i, data;

		data = *(u8 *) buf++;

		// set data bus
		for (i = 0; i < 8; ++i)
			gpio_set_value(par->gpio.db[i], data & (1 << i));
		// set E
		gpio_set_value(par->EPIN, 1);
		udelay(5);
		// unset e - write
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
FBTFT_REGISTER_DRIVER(DRVNAME, "displaytronic,fb_agm1264k-fl", &display);

MODULE_ALIAS("platform:" DRVNAME);

MODULE_DESCRIPTION("Two KS0108 LCD controllers in AGM1264K-FL display");
MODULE_AUTHOR("ololoshka2871");
MODULE_LICENSE("GPL");
