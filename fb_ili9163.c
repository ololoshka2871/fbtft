/*
 * FB driver for the ILI9163 LCD Controller
 *
 * Copyright (C) 2014 ololoshka2871
 *
 * Based on ili9325.c by Noralf Tronnes
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

#include "fbtft.h"

#define DRVNAME		"fb_ili9163"
#define WIDTH		128
#define HEIGHT		128
#define BPP		16
#define FPS		20
#define DEFAULT_GAMMA	"36 29 12 22 1C 15 42 B7 2F 13 12 0A 11 0B 06\n"

static int init_display(struct fbtft_par *par)
{
	fbtft_par_dbg(DEBUG_INIT_DISPLAY, par, "%s()\n", __func__);

	par->fbtftops.reset(par);

	if (par->gpio.cs != -1)
		gpio_set_value(par->gpio.cs, 0);  /* Activate chip */

	write_reg(par, A, B);

	return 0;
}

static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	fbtft_par_dbg(DEBUG_SET_ADDR_WIN, par,
		"%s(xs=%d, ys=%d, xe=%d, ye=%d)\n", __func__, xs, ys, xe, ye);
	/*TODO*/
#if 0
	switch (par->info->var.rotate) {
	/* R20h = Horizontal GRAM Start Address */
	/* R21h = Vertical GRAM Start Address */
	case 0:
		write_reg(par, 0x0020, xs);
		write_reg(par, 0x0021, ys);
		break;
	case 180:
		write_reg(par, 0x0020, WIDTH - 1 - xs);
		write_reg(par, 0x0021, HEIGHT - 1 - ys);
		break;
	case 270:
		write_reg(par, 0x0020, WIDTH - 1 - ys);
		write_reg(par, 0x0021, xs);
		break;
	case 90:
		write_reg(par, 0x0020, ys);
		write_reg(par, 0x0021, HEIGHT - 1 - xs);
		break;
	}
	write_reg(par, 0x0022); /* Write Data to GRAM */
#endif
}

static int set_var(struct fbtft_par *par)
{
	fbtft_par_dbg(DEBUG_INIT_DISPLAY, par, "%s()\n", __func__);
	/*TODO*/
#if 0
	switch (par->info->var.rotate) {
	/* AM: GRAM update direction */
	case 0:
		write_reg(par, 0x03, 0x0030 | (par->bgr << 12));
		break;
	case 180:
		write_reg(par, 0x03, 0x0000 | (par->bgr << 12));
		break;
	case 270:
		write_reg(par, 0x03, 0x0028 | (par->bgr << 12));
		break;
	case 90:
		write_reg(par, 0x03, 0x0018 | (par->bgr << 12));
		break;
	}
#endif
	return 0;
}

/*
  Gamma string format:
    TODO
*/
#define CURVE(num, idx)  curves[num*par->gamma.num_values + idx]
static int set_gamma(struct fbtft_par *par, unsigned long *curves)
{
	/*TODO*/
#if 0
	unsigned long mask[] = {
		0b11111, 0b11111, 0b111, 0b111, 0b111,
		0b111, 0b111, 0b111, 0b111, 0b111,
		0b11111, 0b11111, 0b111, 0b111, 0b111,
		0b111, 0b111, 0b111, 0b111, 0b111 };
	int i, j;

	fbtft_par_dbg(DEBUG_INIT_DISPLAY, par, "%s()\n", __func__);

	/* apply mask */
	for (i = 0; i < 2; i++)
		for (j = 0; j < 10; j++)
			CURVE(i, j) &= mask[i*par->gamma.num_values + j];

	write_reg(par, 0x0030, CURVE(0, 5) << 8 | CURVE(0, 4));
	write_reg(par, 0x0031, CURVE(0, 7) << 8 | CURVE(0, 6));
	write_reg(par, 0x0032, CURVE(0, 9) << 8 | CURVE(0, 8));
	write_reg(par, 0x0035, CURVE(0, 3) << 8 | CURVE(0, 2));
	write_reg(par, 0x0036, CURVE(0, 1) << 8 | CURVE(0, 0));

	write_reg(par, 0x0037, CURVE(1, 5) << 8 | CURVE(1, 4));
	write_reg(par, 0x0038, CURVE(1, 7) << 8 | CURVE(1, 6));
	write_reg(par, 0x0039, CURVE(1, 9) << 8 | CURVE(1, 8));
	write_reg(par, 0x003C, CURVE(1, 3) << 8 | CURVE(1, 2));
	write_reg(par, 0x003D, CURVE(1, 1) << 8 | CURVE(1, 0));
#endif
	return 0;
}
#undef CURVE


static struct fbtft_display display = {
	.regwidth = 16,
	.width = WIDTH,
	.height = HEIGHT,
	.bpp = BPP,
	.fps = FPS,
	.gamma_num = 1,
	.gamma_len = 15,
	.gamma = DEFAULT_GAMMA,
	.fbtftops = {
		.init_display = init_display,
		.set_addr_win = set_addr_win,
		.set_var = set_var,
		.set_gamma = set_gamma,
	},
};
FBTFT_REGISTER_DRIVER(DRVNAME, "ilitek,ili9163", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("platform:" DRVNAME);
MODULE_ALIAS("spi:ili9163");
MODULE_ALIAS("platform:ili9163");

MODULE_DESCRIPTION("FB driver for the ILI9163 LCD Controller");
MODULE_AUTHOR("ololoshka2871");
MODULE_LICENSE("GPL");
