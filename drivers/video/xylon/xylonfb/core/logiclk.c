/*
 * Xylon logiCVC frame buffer driver:
 *   pixel clock generation using logiCLK IP core
 *
 * Author: Xylon d.o.o.
 * e-mail: goran.pantar@logicbricks.com
 *
 * 2013 Xylon d.o.o.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */


#include <linux/io.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include "logiclk.h"


#define FRAC_PRECISION              10

#define FVCO_MIN                    800
#define FVCO_MAX                    1600

#define NUM_OF_MULT_STEPS           64
#define NUM_OF_DIV_INPUT_STEPS      56
#define NUM_OF_DIV_OUTPUT_STEPS     128

#define CLK_FB_OUT_DUTY             50000
#define DIV_CLK_PHASE               0


static inline u32 get_bits(u64 input, u32 msb, u32 lsb)
{
	return (input >> lsb) & ((1 << (msb-lsb+1)) - 1);
}

static u32 round_frac(u32 decimal, u32 precision)
{
	u32 ret;

	if (decimal & (1 << (FRAC_PRECISION-precision-1)))
		ret = decimal + (1 << (FRAC_PRECISION-precision-1));
	else
		ret = decimal;

	return ret;
}

static u32 pll_divider(u32 divide, u32 duty_cycle)
{
	u32 duty_cycle_fix;
	u32 high_time;
	u32 low_time;
	u32 w_edge;
	u32 no_count;
	u32 temp;

	if (duty_cycle <= 0 || duty_cycle >= 100000) {
		pr_err("%s: invalid duty_cycle %d", __func__, duty_cycle);
		return -1;
	}
	duty_cycle_fix = (duty_cycle << FRAC_PRECISION) / 100000;

	if (divide == 1) {
		high_time = 1;
		w_edge = 0;
		low_time = 1;
		no_count = 1;
	} else {
		temp = round_frac(duty_cycle_fix * divide, 1);
		high_time = get_bits(temp, FRAC_PRECISION+6, FRAC_PRECISION);
		w_edge = get_bits(temp, FRAC_PRECISION-1, FRAC_PRECISION-1);

		if (high_time == 0) {
			high_time = 1;
			w_edge = 0;
		}
		if (high_time == divide) {
			high_time = divide - 1;
			w_edge = 1;
		}
		low_time = divide - high_time;
		no_count = 0;
	}

	return (((low_time  & 0x3F) <<  0) |
			((high_time & 0x3F) <<  6) |
			((no_count  & 0x01) << 12) |
			((w_edge    & 0x01) << 13));
}

static u32 pll_phase(u32 divide, s32 phase)
{
	u32 phase_in_cycles;
	u32 phase_fixed;
	u32 mx;
	u32 delay_time;
	u32 phase_mux;
	u32 temp;

	if ((phase < -360000) || (phase > 360000))
		return -1;

	if (phase < 0)
		phase_fixed = ((phase + 360000) << FRAC_PRECISION) / 1000;
	else
		phase_fixed = (phase << FRAC_PRECISION) / 1000;

	phase_in_cycles = (phase_fixed * divide) / 360;

	temp = round_frac(phase_in_cycles, 3);

	mx = 0;
	phase_mux = get_bits(temp, FRAC_PRECISION-1, FRAC_PRECISION-3);
	delay_time = get_bits(temp, FRAC_PRECISION+5, FRAC_PRECISION);

	return ((delay_time & 0x3F) << 0) |
			((phase_mux & 0x07) << 6) |
			((mx        & 0x03) << 9);
}

static u64 pll_lock_lookup(u32 divide)
{
	u64 lookup[] = {
		0x31BE8FA401,
		0x31BE8FA401,
		0x423E8FA401,
		0x5AFE8FA401,
		0x73BE8FA401,
		0x8C7E8FA401,
		0x9CFE8FA401,
		0xB5BE8FA401,
		0xCE7E8FA401,
		0xE73E8FA401,
		0xFFF84FA401,
		0xFFF39FA401,
		0xFFEEEFA401,
		0xFFEBCFA401,
		0xFFE8AFA401,
		0xFFE71FA401,
		0xFFE3FFA401,
		0xFFE26FA401,
		0xFFE0DFA401,
		0xFFDF4FA401,
		0xFFDDBFA401,
		0xFFDC2FA401,
		0xFFDA9FA401,
		0xFFD90FA401,
		0xFFD90FA401,
		0xFFD77FA401,
		0xFFD5EFA401,
		0xFFD5EFA401,
		0xFFD45FA401,
		0xFFD45FA401,
		0xFFD2CFA401,
		0xFFD2CFA401,
		0xFFD2CFA401,
		0xFFD13FA401,
		0xFFD13FA401,
		0xFFD13FA401,
		0xFFCFAFA401,
		0xFFCFAFA401,
		0xFFCFAFA401,
		0xFFCFAFA401,
		0xFFCFAFA401,
		0xFFCFAFA401,
		0xFFCFAFA401,
		0xFFCFAFA401,
		0xFFCFAFA401,
		0xFFCFAFA401,
		0xFFCFAFA401,
		0xFFCFAFA401,
		0xFFCFAFA401,
		0xFFCFAFA401,
		0xFFCFAFA401,
		0xFFCFAFA401,
		0xFFCFAFA401,
		0xFFCFAFA401,
		0xFFCFAFA401,
		0xFFCFAFA401,
		0xFFCFAFA401,
		0xFFCFAFA401,
		0xFFCFAFA401,
		0xFFCFAFA401,
		0xFFCFAFA401,
		0xFFCFAFA401,
		0xFFCFAFA401,
		0xFFCFAFA401
	};
	return lookup[divide-1];
}

static u32 pll_filter_lookup(u32 divide, bool bw_low)
{
	u32 lookup_entry;
	u32 lookup_low[] = {
		0x5F,
		0x57,
		0x7B,
		0x5B,
		0x6B,
		0x73,
		0x73,
		0x73,
		0x73,
		0x4B,
		0x4B,
		0x4B,
		0xB3,
		0x53,
		0x53,
		0x53,
		0x53,
		0x53,
		0x53,
		0x53,
		0x53,
		0x53,
		0x53,
		0x63,
		0x63,
		0x63,
		0x63,
		0x63,
		0x63,
		0x63,
		0x63,
		0x63,
		0x63,
		0x63,
		0x63,
		0x63,
		0x63,
		0x93,
		0x93,
		0x93,
		0x93,
		0x93,
		0x93,
		0x93,
		0x93,
		0x93,
		0x93,
		0xA3,
		0xA3,
		0xA3,
		0xA3,
		0xA3,
		0xA3,
		0xA3,
		0xA3,
		0xA3,
		0xA3,
		0xA3,
		0xA3,
		0xA3,
		0xA3,
		0xA3,
		0xA3,
		0xA3
	};
	u32 lookup_high[] = {
		0x17C,
		0x3FC,
		0x3F4,
		0x3E4,
		0x3F8,
		0x3C4,
		0x3C4,
		0x3D8,
		0x3E8,
		0x3E8,
		0x3E8,
		0x3B0,
		0x3F0,
		0x3F0,
		0x3F0,
		0x3F0,
		0x3F0,
		0x3F0,
		0x3F0,
		0x3F0,
		0x3B0,
		0x3B0,
		0x3B0,
		0x3E8,
		0x370,
		0x308,
		0x370,
		0x370,
		0x3E8,
		0x3E8,
		0x3E8,
		0x1C8,
		0x330,
		0x330,
		0x3A8,
		0x188,
		0x188,
		0x188,
		0x1F0,
		0x188,
		0x110,
		0x110,
		0x110,
		0x110,
		0x110,
		0x110,
		0x0E0,
		0x0E0,
		0x0E0,
		0x0E0,
		0x0E0,
		0x0E0,
		0x0E0,
		0x0E0,
		0x0E0,
		0x0E0,
		0x0E0,
		0x0E0,
		0x0E0,
		0x0E0,
		0x0E0,
		0x0E0,
		0x0E0,
		0x0E0
	};

	if (bw_low)
		lookup_entry = lookup_low[divide-1];
	else
		lookup_entry = lookup_high[divide-1];

	return lookup_entry;
}

static u32 calc_pll_count(u32 divide, s32 phase, u32 duty_cycle)
{
	u32 div_calc;
	u32 phase_calc;
	u32 ret;

	div_calc = pll_divider(divide, duty_cycle);
	phase_calc = pll_phase(divide, phase);

	ret = ((get_bits(div_calc,   11,  0) << 0)  |
		   (get_bits(phase_calc,  8,  6) << 13) |
		   (get_bits(phase_calc,  5,  0) << 16) |
		   (get_bits(div_calc,   13, 12) << 22) |
		   (get_bits(phase_calc, 10,  9) << 24));

	return ret;
}

static void calc_pll_mult(u32 osc_clk_freq, u32 out_clk_freq,
	u32 *p_mult, u32 *p_div_in)
{
	u32 freq_err = 0xFFFFFFFF;
	u32 din;
	u32 dout;
	u32 mult;
	u32 fvco;
	u64 freq_err_new;
	u64 freq_hz;
	u32 remainder;

	*p_mult = 0;
	*p_div_in = 0;

	for (din = 1; din <= NUM_OF_DIV_INPUT_STEPS; din++) {
		for (mult = 2; mult <= NUM_OF_MULT_STEPS; mult++) {
			for (dout = 1; dout <= NUM_OF_DIV_OUTPUT_STEPS; dout++) {
				freq_hz = osc_clk_freq;
				freq_hz *= mult;
				freq_hz = div_u64_rem(freq_hz, din, &remainder);
				fvco = (u32)(div_u64_rem(freq_hz, 1000000, &remainder));
				if ((fvco >= FVCO_MIN) && (fvco <= FVCO_MAX)) {
					freq_hz = div_u64_rem(freq_hz, dout, &remainder);
					if (((u64)out_clk_freq) >= freq_hz)
						freq_err_new = ((u64)out_clk_freq) - freq_hz;
					else
						freq_err_new = freq_hz - ((u64)out_clk_freq);
					if (freq_err_new < freq_err) {
						freq_err = (u32)freq_err_new;
						*p_mult = mult;
						*p_div_in = din;
					}
				}
			}
		}
	}
}

static u32 calc_pll_div(u32 osc_clk_freq, u32 out_clk_freq,
	u32 mult, u32 div_in)
{
	u32 div_out = 0;
	u32 freq_err = 0xFFFFFFFF;
	u32 dout;
	u32 khz;
	u32 freq_err_new;

	for (dout = 1; dout <= NUM_OF_DIV_OUTPUT_STEPS; dout++) {
		khz = (osc_clk_freq / 1000 * mult) / (dout * div_in);
		freq_err_new = abs((int)(out_clk_freq - (1000 * khz)));
		if (freq_err_new < freq_err) {
			freq_err = freq_err_new;
			div_out = dout;
		}
	}

	return div_out;
}

int logiclk_calc_regs(struct logiclk_freq_out *freq_out,
	u32 c_osc_clk_freq_hz, u32 *regs_out)
{
	u32 clkout_phase = 0;
	u32 clkfbout_phase = 0;
	u32 clkout_duty = 50000;
	u32 bandwith = 0;
	u32 divclk_divide = 1;
	u64 lock;
	u32 clkout_divide[LOGICLK_OUTPUTS];
	u32 clkfbout_mult;
	u32 clkout[LOGICLK_OUTPUTS];
	u32 divclk;
	u32 clkfbout;
	u32 digital_filt;
	int i;

	calc_pll_mult(c_osc_clk_freq_hz, (u32)freq_out->freq_out_hz[0],
		&clkfbout_mult, &divclk_divide);
	if ((clkfbout_mult == 0) || (divclk_divide == 0))
		return -EINVAL;

	for (i = 0; i < LOGICLK_OUTPUTS; i++)
		clkout_divide[i] = calc_pll_div(c_osc_clk_freq_hz,
			freq_out->freq_out_hz[i], clkfbout_mult, divclk_divide);

	for (i = 0; i < LOGICLK_OUTPUTS; i++)
		clkout[i] = calc_pll_count(
			clkout_divide[i], clkout_phase, clkout_duty);

	divclk = calc_pll_count(divclk_divide, DIV_CLK_PHASE, CLK_FB_OUT_DUTY);
	clkfbout = calc_pll_count(clkfbout_mult, clkfbout_phase, clkout_duty);

	digital_filt = pll_filter_lookup(clkfbout_mult-1, bandwith);
	lock = pll_lock_lookup(clkfbout_mult-1);

	regs_out[0] = 0xFFFF;
	for (i = 0; i < LOGICLK_OUTPUTS; i++) {
		regs_out[1 + i*2 + 0] = get_bits(clkout[i], 15, 0);
		regs_out[1 + i*2 + 1] = get_bits(clkout[i], 31, 16);
	}

	/* DIVCLK[23:22] & DIVCLK[11:0] */
	regs_out[13] = (get_bits(divclk, 23, 22) << 12) |
					(get_bits(divclk, 11, 0) << 0);
	/* CLKFBOUT[15:0] */
	regs_out[14] = get_bits(clkfbout, 15, 0);
	/* CLKFBOUT[31:16] */
	regs_out[15] = get_bits(clkfbout, 31, 16);
	/* LOCK[29:20] */
	regs_out[16] = get_bits(lock, 29, 20);
	/* LOCK[34:30] & LOCK[9:0] */
	regs_out[17] = (get_bits(lock, 34, 30) << 10) |
					get_bits(lock, 9, 0);
	/* LOCK[39:35] & S10_LOCK[19:10] */
	regs_out[18] = (get_bits(lock, 39, 35) << 10) |
					get_bits(lock, 19, 10);
	/* DIGITAL_FILT[9] & 00 & DIGITAL_FILT[8:7] & 00 &
	   DIGITAL_FILT[6] & 0000000 */
	regs_out[19] = (get_bits(digital_filt, 6, 6) << 8)  |
				   (get_bits(digital_filt, 8, 7) << 11) |
				   (get_bits(digital_filt, 9, 9) << 15);
	/* DIGITAL_FILT[5] & 00 & DIGITAL_FILT[4:3] & 00 &
	   DIGITAL_FILT[2:1] & 00 & DIGITAL_FILT[0] & 0000  */
	regs_out[20] = (get_bits(digital_filt, 0, 0) << 4)  |
				   (get_bits(digital_filt, 2, 1) << 7)  |
				   (get_bits(digital_filt, 4, 3) << 11) |
				   (get_bits(digital_filt, 5, 5) << 15);

#ifdef LOGICLK_DUMP_REGS
	for (i = 0; i < LOGICLK_REGS; i++)
		pr_info("reg[%d]=0x%lx\n", i, regs_out[i]);
#endif

	return 0;
}
