// SPDX-License-Identifier: GPL-2.0
/*
 * Si5324 clock driver
 *
 * Copyright (C) 2017-2018 Xilinx, Inc.
 *
 * Author:	Venkateshwar Rao G <vgannava.xilinx.com>
 *		Leon Woestenberg <leon@sidebranch.com>
 */

#include <linux/module.h>
#include <linux/types.h>
#include "si5324drv.h"

/**
 * si5324_rate_approx - Find closest rational approximation N2_LS/N3 fraction.
 *
 * @f:		Holds the N2_LS/N3 fraction in 36.28 fixed point notation.
 * @md:		Holds the maximum denominator (N3) value allowed.
 * @num:	Store the numinator (N2_LS) found.
 * @denom:	Store the denominator (N3) found.
 *
 * This function finds the closest rational approximation.
 * It allows only n/1 solution and as a part of the calculation
 * multiply fraction until no digits after the decimal point and
 * continued fraction and check denominator at each step.
 */
void si5324_rate_approx(u64 f, u64 md, u32 *num, u32 *denom)
{
	u64 a, h[3] = { 0, 1, 0 }, k[3] = { 1, 0, 0 };
	u64 x, d, m, n = 1;
	int i = 0;

	if (md <= 1) {
		*denom = 1;
		*num = (u32)(f >> 28);
		return;
	}

	n <<= 28;
	for (i = 0; i < 28; i++) {
		if ((f & 0x1) == 0) {
			n >>= 1;
			f >>= 1;
		} else {
			break;
		}
	}
	d = f;

	for (i = 0; i < 64; i++) {
		a = n ? (div64_u64(d, n)) : 0;
		if (i && !a)
			break;
		x = d;
		d = n;
		div64_u64_rem(x, n, &m);
		n = m;
		x = a;
		if (k[1] * a + k[0] >= md) {
			x = div64_u64((md - k[0]), k[1]);
			if (x * 2 >= a || k[1] >= md)
				i = 65;
			else
				break;
		}
		h[2] = x * h[1] + h[0];
		h[0] = h[1];
		h[1] = h[2];
		k[2] = x * k[1] + k[0];
		k[0] = k[1];
		k[1] = k[2];
	}

	*denom = (u32)k[1];
	*num = (u32)h[1];
}

/**
 * si5324_find_n2ls - Search through the possible settings for the N2_LS.
 *
 * @settings:	Holds the settings up till now.
 *
 * This function finds the best setting for N2_LS and N3n with the values
 * for N1_HS, NCn_LS, and N2_HS.
 *
 * Return:	1 when the best possible result has been found, 0 on failure.
 */
static int si5324_find_n2ls(struct si5324_settingst *settings)
{
	u32 result = 0;
	u64 f3_actual;
	u64 fosc_actual;
	u64 fout_actual;
	u64 delta_fout;
	u64 n2_ls_div_n3, mult_res;
	u32 mult;

	n2_ls_div_n3 = div64_u64(div64_u64(div64_u64(settings->fosc,
			(settings->fin >> SI5324_FIN_FOUT_SHIFT)),
			(u64)settings->n2_hs), (u64)2);

	si5324_rate_approx(n2_ls_div_n3, settings->n31_max, &settings->n2_ls,
				&settings->n31);
	settings->n2_ls *= 2;

	if (settings->n2_ls < settings->n2_ls_min) {
		mult = div64_u64(settings->n2_ls_min, settings->n2_ls);
		div64_u64_rem(settings->n2_ls_min, settings->n2_ls, &mult_res);
		mult = mult_res ? mult + 1 : mult;
		settings->n2_ls *= mult;
		settings->n31 *= mult;
	}

	if (settings->n31 < settings->n31_min) {
		mult = div64_u64(settings->n31_min, settings->n31);
		div64_u64_rem(settings->n31_min, settings->n31, &mult_res);
		mult = mult_res ? mult + 1 : mult;
		settings->n2_ls *= mult;
		settings->n31 *= mult;
	}
	pr_debug("Trying N2_LS = %d N3 = %d.\n", settings->n2_ls,
		 settings->n31);

	if (settings->n2_ls < settings->n2_ls_min ||
	    settings->n2_ls > settings->n2_ls_max) {
		pr_info("N2_LS out of range.\n");
	} else if ((settings->n31 < settings->n31_min) ||
		   (settings->n31 > settings->n31_max)) {
		pr_info("N3 out of range.\n");
	} else {
		f3_actual = div64_u64(settings->fin, settings->n31);
		fosc_actual = f3_actual * settings->n2_hs * settings->n2_ls;
		fout_actual = div64_u64(fosc_actual,
					(settings->n1_hs * settings->nc1_ls));
		delta_fout = fout_actual - settings->fout;

		if ((f3_actual < ((u64)SI5324_F3_MIN) <<
			SI5324_FIN_FOUT_SHIFT) ||
			(f3_actual > ((u64)SI5324_F3_MAX) <<
			SI5324_FIN_FOUT_SHIFT)) {
			pr_debug("F3 frequency out of range.\n");
		} else if ((fosc_actual < ((u64)SI5324_FOSC_MIN) <<
				SI5324_FIN_FOUT_SHIFT) ||
				(fosc_actual > ((u64)SI5324_FOSC_MAX) <<
				SI5324_FIN_FOUT_SHIFT)) {
			pr_debug("Fosc frequency out of range.\n");
		} else if ((fout_actual < ((u64)SI5324_FOUT_MIN) <<
				SI5324_FIN_FOUT_SHIFT) ||
				(fout_actual > ((u64)SI5324_FOUT_MAX) <<
				SI5324_FIN_FOUT_SHIFT)) {
			pr_debug("Fout frequency out of range.\n");
		} else {
			u64 divident = fosc_actual >> SI5324_FIN_FOUT_SHIFT;

			pr_debug("Found solution: fout = %dHz delta = %dHz.\n",
				 (u32)(fout_actual >> SI5324_FIN_FOUT_SHIFT),
				 (u32)(delta_fout >> SI5324_FIN_FOUT_SHIFT));
			pr_debug("fosc = %dkHz f3 = %dHz.\n",
				 (u32)(do_div(divident, 1000)),
				 (u32)(f3_actual >> SI5324_FIN_FOUT_SHIFT));

			if (((u64)abs(delta_fout)) <
				settings->best_delta_fout) {
				settings->best_n1_hs = settings->n1_hs;
				settings->best_nc1_ls = settings->nc1_ls;
				settings->best_n2_hs = settings->n2_hs;
				settings->best_n2_ls = settings->n2_ls;
				settings->best_n3 = settings->n31;
				settings->best_fout = fout_actual;
				settings->best_delta_fout = abs(delta_fout);
				if (delta_fout == 0)
					result = 1;
			}
		}
	}
	return result;
}

/**
 * si5324_find_n2 - Find a valid setting for N2_HS and N2_LS.
 *
 * @settings:	Holds the settings up till now.
 *
 * This function finds a valid settings for N2_HS and N2_LS. Iterates over
 * all possibilities of N2_HS and then performs a binary search over the
 * N2_LS values.
 *
 * Return:	1 when the best possible result has been found.
 */
static int si5324_find_n2(struct si5324_settingst *settings)
{
	u32 result = 0;

	for (settings->n2_hs = SI5324_N2_HS_MAX; settings->n2_hs >=
		SI5324_N2_HS_MIN; settings->n2_hs--) {
		pr_debug("Trying N2_HS = %d.\n", settings->n2_hs);
		settings->n2_ls_min = (u32)(div64_u64(settings->fosc,
					((u64)(SI5324_F3_MAX * settings->n2_hs)
						<< SI5324_FIN_FOUT_SHIFT)));

		if (settings->n2_ls_min < SI5324_N2_LS_MIN)
			settings->n2_ls_min = SI5324_N2_LS_MIN;

		settings->n2_ls_max = (u32)(div64_u64(settings->fosc,
					((u64)(SI5324_F3_MIN *
					settings->n2_hs) <<
					SI5324_FIN_FOUT_SHIFT)));
		if (settings->n2_ls_max > SI5324_N2_LS_MAX)
			settings->n2_ls_max = SI5324_N2_LS_MAX;

		result = si5324_find_n2ls(settings);
		if (result)
			break;
	}
	return result;
}

/**
 * si5324_calc_ncls_limits - Calculates the valid range for NCn_LS.
 *
 * @settings:	Holds the input and output frequencies and the setting
 * for N1_HS.
 *
 * This function calculates the valid range for NCn_LS with the value
 * for the output frequency and N1_HS already set in settings.
 *
 * Return:	-1 when there are no valid settings, 0 otherwise.
 */
int si5324_calc_ncls_limits(struct si5324_settingst *settings)
{
	settings->nc1_ls_min = div64_u64(settings->n1_hs_min,
					 settings->n1_hs);

	if (settings->nc1_ls_min < SI5324_NC_LS_MIN)
		settings->nc1_ls_min = SI5324_NC_LS_MIN;
	if (settings->nc1_ls_min > 1 &&	(settings->nc1_ls_min & 0x1) == 1)
		settings->nc1_ls_min++;
	settings->nc1_ls_max = div64_u64(settings->n1_hs_max, settings->n1_hs);

	if (settings->nc1_ls_max > SI5324_NC_LS_MAX)
		settings->nc1_ls_max = SI5324_NC_LS_MAX;

	if ((settings->nc1_ls_max & 0x1) == 1)
		settings->nc1_ls_max--;
	if ((settings->nc1_ls_max * settings->n1_hs < settings->n1_hs_min) ||
	    (settings->nc1_ls_min * settings->n1_hs > settings->n1_hs_max))
		return -1;

	return 0;
}

/**
 * si5324_find_ncls - Find a valid setting for NCn_LS
 *
 * @settings:	Holds the input and output frequencies, the setting for
 * N1_HS, and the limits for NCn_LS.
 *
 * This function find a valid setting for NCn_LS that can deliver the correct
 * output frequency. Assumes that the valid range is relatively small
 * so a full search can be done (should be true for video clock frequencies).
 *
 * Return:	1 when the best possible result has been found.
 */
static int si5324_find_ncls(struct si5324_settingst *settings)
{
	u64 fosc_1;
	u32 result;

	fosc_1 = settings->fout * settings->n1_hs;
	for (settings->nc1_ls = settings->nc1_ls_min;
		settings->nc1_ls <= settings->nc1_ls_max;) {
		settings->fosc = fosc_1 * settings->nc1_ls;
		pr_debug("Trying NCn_LS = %d: fosc = %dkHz.\n",
			 settings->nc1_ls,
			 (u32)(div64_u64((settings->fosc >>
				SI5324_FIN_FOUT_SHIFT), 1000)));

		result = si5324_find_n2(settings);
		if (result)
			break;
		if (settings->nc1_ls == 1)
			settings->nc1_ls++;
		else
			settings->nc1_ls += 2;
	}
	return result;
}

/**
 * si5324_calcfreqsettings - Calculate the frequency settings
 *
 * @clkinfreq:	Frequency of the input clock.
 * @clkoutfreq:	Desired output clock frequency.
 * @clkactual:	Actual clock frequency.
 * @n1_hs:	Set to the value for the N1_HS register.
 * @ncn_ls:	Set to the value for the NCn_LS register.
 * @n2_hs:	Set to the value for the N2_HS register.
 * @n2_ls:	Set to the value for the N2_LS register.
 * @n3n:	Set to the value for the N3n register.
 * @bwsel:	Set to the value for the BW_SEL register.
 *
 * This funciton calculates the frequency settings for the desired output
 * frequency.
 *
 * Return:	SI5324_SUCCESS for success, SI5324_ERR_FREQ when the
 * requested frequency cannot be generated.
 */
int si5324_calcfreqsettings(u32 clkinfreq, u32 clkoutfreq, u32 *clkactual,
			    u8 *n1_hs, u32 *ncn_ls, u8 *n2_hs, u32 *n2_ls,
			    u32 *n3n, u8 *bwsel)
{
	struct si5324_settingst settings;
	int result;

	settings.fin = (u64)clkinfreq << SI5324_FIN_FOUT_SHIFT;
	settings.fout = (u64)clkoutfreq << SI5324_FIN_FOUT_SHIFT;
	settings.best_delta_fout = settings.fout;

	settings.n1_hs_min = (int)(div64_u64(SI5324_FOSC_MIN, clkoutfreq));
	if (settings.n1_hs_min < SI5324_N1_HS_MIN * SI5324_NC_LS_MIN)
		settings.n1_hs_min = SI5324_N1_HS_MIN * SI5324_NC_LS_MIN;

	settings.n1_hs_max = (int)(div64_u64(SI5324_FOSC_MAX, clkoutfreq));
	if (settings.n1_hs_max > SI5324_N1_HS_MAX * SI5324_NC_LS_MAX)
		settings.n1_hs_max = SI5324_N1_HS_MAX * SI5324_NC_LS_MAX;

	settings.n31_min = div64_u64(clkinfreq, SI5324_F3_MAX);
	if (settings.n31_min < SI5324_N3_MIN)
		settings.n31_min = SI5324_N3_MIN;

	settings.n31_max = div64_u64(clkinfreq, SI5324_F3_MIN);
	if (settings.n31_max > SI5324_N3_MAX)
		settings.n31_max = SI5324_N3_MAX;

	/* Find a valid oscillator frequency with the highest setting of N1_HS
	 * possible (reduces power)
	 */
	for (settings.n1_hs = SI5324_N1_HS_MAX;
		settings.n1_hs >= SI5324_N1_HS_MIN; settings.n1_hs--) {
		pr_debug("Trying N1_HS = %d.\n", settings.n1_hs);

		result = si5324_calc_ncls_limits(&settings);
		if (result) {
			pr_debug("No valid settings\n");
			continue;
		}
		result = si5324_find_ncls(&settings);
		if (result)
			break;
	}

		pr_debug("Si5324: settings.best_delta_fout = %llu\n",
			 (unsigned long long)settings.best_delta_fout);
		pr_debug("Si5324: settings.fout = %llu\n",
			 (unsigned long long)settings.fout);

	if (settings.best_delta_fout == settings.fout) {
		pr_debug("Si5324: No valid settings found.");
		return SI5324_ERR_FREQ;
	}
		pr_debug("Si5324: Found solution: fout = %dHz.\n",
			 (u32)(settings.best_fout >> 28));

	/* Post processing: convert temporary values to actual registers */
	*n1_hs = (u8)settings.best_n1_hs - 4;
	*ncn_ls = settings.best_nc1_ls - 1;
	*n2_hs = (u8)settings.best_n2_hs - 4;
	*n2_ls = settings.best_n2_ls - 1;
	*n3n = settings.best_n3 - 1;
	/*
	 * How must the bandwidth selection be determined?
	 * Not all settings will be valid.
	 * refclk 2, 0xA2, BWSEL_REG=1010 (?)
	 * free running 2, 0x42, BWSEL_REG=0100 (?)
	 */
	*bwsel = 6;

	if (clkactual)
		*clkactual = (settings.best_fout >> SI5324_FIN_FOUT_SHIFT);

	return SI5324_SUCCESS;
}
