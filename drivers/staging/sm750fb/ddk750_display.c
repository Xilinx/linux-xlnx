#include "ddk750_reg.h"
#include "ddk750_help.h"
#include "ddk750_display.h"
#include "ddk750_power.h"
#include "ddk750_dvi.h"

#define primaryWaitVerticalSync(delay) waitNextVerticalSync(0, delay)

static void setDisplayControl(int ctrl, int disp_state)
{
	/* state != 0 means turn on both timing & plane en_bit */
	unsigned long reg, val, reserved;
	int cnt = 0;

	if (!ctrl) {
		reg = PANEL_DISPLAY_CTRL;
		reserved = PANEL_DISPLAY_CTRL_RESERVED_MASK;
	} else {
		reg = CRT_DISPLAY_CTRL;
		reserved = CRT_DISPLAY_CTRL_RESERVED_MASK;
	}

	val = PEEK32(reg);
	if (disp_state) {
		/*
		 * Timing should be enabled first before enabling the
		 * plane because changing at the same time does not
		 * guarantee that the plane will also enabled or
		 * disabled.
		 */
		val |= DISPLAY_CTRL_TIMING;
		POKE32(reg, val);

		val |= DISPLAY_CTRL_PLANE;

		/*
		 * Somehow the register value on the plane is not set
		 * until a few delay. Need to write and read it a
		 * couple times
		 */
		do {
			cnt++;
			POKE32(reg, val);
		} while ((PEEK32(reg) & ~reserved) != (val & ~reserved));
		pr_debug("Set Plane enbit:after tried %d times\n", cnt);
	} else {
		/*
		 * When turning off, there is no rule on the
		 * programming sequence since whenever the clock is
		 * off, then it does not matter whether the plane is
		 * enabled or disabled.  Note: Modifying the plane bit
		 * will take effect on the next vertical sync. Need to
		 * find out if it is necessary to wait for 1 vsync
		 * before modifying the timing enable bit.
		 */
		val &= ~DISPLAY_CTRL_PLANE;
		POKE32(reg, val);

		val &= ~DISPLAY_CTRL_TIMING;
		POKE32(reg, val);
	}
}

static void waitNextVerticalSync(int ctrl, int delay)
{
	unsigned int status;

	if (!ctrl) {
		/* primary controller */

		/*
		 * Do not wait when the Primary PLL is off or display control is
		 * already off. This will prevent the software to wait forever.
		 */
		if (!(PEEK32(PANEL_PLL_CTRL) & PLL_CTRL_POWER) ||
		    !(PEEK32(PANEL_DISPLAY_CTRL) & DISPLAY_CTRL_TIMING)) {
			return;
		}

		while (delay-- > 0) {
			/* Wait for end of vsync. */
			do {
				status = PEEK32(SYSTEM_CTRL);
			} while (status & SYSTEM_CTRL_PANEL_VSYNC_ACTIVE);

			/* Wait for start of vsync. */
			do {
				status = PEEK32(SYSTEM_CTRL);
			} while (!(status & SYSTEM_CTRL_PANEL_VSYNC_ACTIVE));
		}

	} else {
		/*
		 * Do not wait when the Primary PLL is off or display control is
		 * already off. This will prevent the software to wait forever.
		 */
		if (!(PEEK32(CRT_PLL_CTRL) & PLL_CTRL_POWER) ||
		    !(PEEK32(CRT_DISPLAY_CTRL) & DISPLAY_CTRL_TIMING)) {
			return;
		}

		while (delay-- > 0) {
			/* Wait for end of vsync. */
			do {
				status = PEEK32(SYSTEM_CTRL);
			} while (status & SYSTEM_CTRL_PANEL_VSYNC_ACTIVE);

			/* Wait for start of vsync. */
			do {
				status = PEEK32(SYSTEM_CTRL);
			} while (!(status & SYSTEM_CTRL_PANEL_VSYNC_ACTIVE));
		}
	}
}

static void swPanelPowerSequence(int disp, int delay)
{
	unsigned int reg;

	/* disp should be 1 to open sequence */
	reg = PEEK32(PANEL_DISPLAY_CTRL);
	reg |= (disp ? PANEL_DISPLAY_CTRL_FPEN : 0);
	POKE32(PANEL_DISPLAY_CTRL, reg);
	primaryWaitVerticalSync(delay);

	reg = PEEK32(PANEL_DISPLAY_CTRL);
	reg |= (disp ? PANEL_DISPLAY_CTRL_DATA : 0);
	POKE32(PANEL_DISPLAY_CTRL, reg);
	primaryWaitVerticalSync(delay);

	reg = PEEK32(PANEL_DISPLAY_CTRL);
	reg |= (disp ? PANEL_DISPLAY_CTRL_VBIASEN : 0);
	POKE32(PANEL_DISPLAY_CTRL, reg);
	primaryWaitVerticalSync(delay);

	reg = PEEK32(PANEL_DISPLAY_CTRL);
	reg |= (disp ? PANEL_DISPLAY_CTRL_FPEN : 0);
	POKE32(PANEL_DISPLAY_CTRL, reg);
	primaryWaitVerticalSync(delay);
}

void ddk750_setLogicalDispOut(disp_output_t output)
{
	unsigned int reg;

	if (output & PNL_2_USAGE) {
		/* set panel path controller select */
		reg = PEEK32(PANEL_DISPLAY_CTRL);
		reg &= ~PANEL_DISPLAY_CTRL_SELECT_MASK;
		reg |= (((output & PNL_2_MASK) >> PNL_2_OFFSET) <<
			PANEL_DISPLAY_CTRL_SELECT_SHIFT);
		POKE32(PANEL_DISPLAY_CTRL, reg);
	}

	if (output & CRT_2_USAGE) {
		/* set crt path controller select */
		reg = PEEK32(CRT_DISPLAY_CTRL);
		reg &= ~CRT_DISPLAY_CTRL_SELECT_MASK;
		reg |= (((output & CRT_2_MASK) >> CRT_2_OFFSET) <<
			CRT_DISPLAY_CTRL_SELECT_SHIFT);
		/*se blank off */
		reg &= ~CRT_DISPLAY_CTRL_BLANK;
		POKE32(CRT_DISPLAY_CTRL, reg);
	}

	if (output & PRI_TP_USAGE) {
		/* set primary timing and plane en_bit */
		setDisplayControl(0, (output & PRI_TP_MASK) >> PRI_TP_OFFSET);
	}

	if (output & SEC_TP_USAGE) {
		/* set secondary timing and plane en_bit*/
		setDisplayControl(1, (output & SEC_TP_MASK) >> SEC_TP_OFFSET);
	}

	if (output & PNL_SEQ_USAGE) {
		/* set  panel sequence */
		swPanelPowerSequence((output & PNL_SEQ_MASK) >> PNL_SEQ_OFFSET, 4);
	}

	if (output & DAC_USAGE)
		setDAC((output & DAC_MASK) >> DAC_OFFSET);

	if (output & DPMS_USAGE)
		ddk750_setDPMS((output & DPMS_MASK) >> DPMS_OFFSET);
}
