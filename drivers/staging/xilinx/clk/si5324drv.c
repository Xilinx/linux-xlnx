#include <linux/types.h>
#include <linux/module.h>

#include "si5324drv.h"

#define xil_printf(format, ...) printk(KERN_INFO format, ## __VA_ARGS__)

/*****************************************************************************/
/**
 * Find the closest rational approximation for the N2_LS/N3 fraction.
 *
 * @param f  Holds the N2_LS/N3 fraction in 36.28 fixed point notation.
 * @param md Holds the maximum denominator (N3) value allowed.
 * @param num Will store the numinator (N2_LS) found.
 * @param denom Will store the denominator (N3) found.
 */
void Si5324_RatApprox(u64 f, u64 md, u32 *num, u32 *denom)
{
    /*  a: Continued fraction coefficients. */
    u64 a, h[3] = { 0, 1, 0 }, k[3] = { 1, 0, 0 };
    u64 x, d, n = 1;
    int i = 0;

    // Degenerate case: only n/1 solution allowed. Return trunc(f)/1.
    if (md <= 1) {
        *denom = 1;
        *num = (u32)(f >> 28);
        return;
    }

    // Multiply fraction until there are no more digits after the decimal point
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

    /* Continued fraction and check denominator each step */
    for (i = 0; i < 64; i++) {
        a = n ? d / n : 0;
        if (i && !a) {
            break;
        }

        x = d;
        d = n;
        n = x % n;

        x = a;
        if (k[1] * a + k[0] >= md) {
            x = (md - k[0]) / k[1];
            if (x * 2 >= a || k[1] >= md) {
                i = 65;
            } else {
                break;
            }
        }

        h[2] = x * h[1] + h[0];
        h[0] = h[1];
        h[1] = h[2];
        k[2] = x * k[1] + k[0];
        k[0] = k[1];
        k[1] = k[2];
    }
    *denom = (u32)k[1];
    *num   = (u32)h[1];
}


/*****************************************************************************/
/**
 * Search through the possible settings for the N2_LS parameter. Finds the best
 * setting for N2_LS and N3n with the values for N1_HS, NCn_LS, and N2_HS
 * already set in settings.
 *
 * @param settings Holds the settings up till now.
 * @return 1 when the best possible result has been found.
 * @note     Private function.
 */
int Si5324_FindN2ls(si5324_settings_t *settings) {
    u32 result = 0;
    u64 f3_actual;
    u64 fosc_actual;
    u64 fout_actual;
    u64 delta_fout;
    u64 n2_ls_div_n3;
    u32 mult;

    n2_ls_div_n3 = settings->fosc / (settings->fin >> 28) / settings->n2_hs / 2;
    Si5324_RatApprox(n2_ls_div_n3, settings->n31_max, &(settings->n2_ls), &(settings->n31));
    settings->n2_ls *= 2;
    // Rational approximation returns the smalles ratio possible. Upscaling
    // might be needed when when one or both of the numbers are too low.
    if (settings->n2_ls < settings->n2_ls_min) {
        mult =  settings->n2_ls_min / settings->n2_ls;
        mult = (settings->n2_ls_min % settings->n2_ls) ? mult + 1 : mult;
        settings->n2_ls *= mult;
        settings->n31    *= mult;
    }
    if (settings->n31 < settings->n31_min) {
        mult =  settings->n31_min / settings->n31;
        mult = (settings->n31_min % settings->n31) ? mult + 1 : mult;
        settings->n2_ls *= mult;
        settings->n31    *= mult;
    }
    if (SI5324_DEBUG) {
        printk(KERN_INFO "Trying N2_LS = %d N3 = %d.\n",
            settings->n2_ls, settings->n31);
    }
    // Check if N2_LS and N3 are within the required ranges
    if ((settings->n2_ls < settings->n2_ls_min) || (settings->n2_ls > settings->n2_ls_max)) {
         printk(KERN_INFO "N2_LS out of range.\n");
    } else if ((settings->n31 < settings->n31_min) || (settings->n31 > settings->n31_max)) {
        printk(KERN_INFO "N3 out of range.\n");
    }
    else {
        // N2_LS and N3 values within range: check actual output frequency
        f3_actual = settings->fin / settings->n31;
        fosc_actual = f3_actual * settings->n2_hs * settings->n2_ls;
        fout_actual = fosc_actual / (settings->n1_hs * settings->nc1_ls);
        delta_fout = fout_actual - settings->fout;
        // Check actual frequencies for validity
        if ((f3_actual < ((u64)SI5324_F3_MIN) << 28) || (f3_actual > ((u64)SI5324_F3_MAX) << 28)) {
            if (SI5324_DEBUG) {
                printk(KERN_INFO "F3 frequency out of range.\n");
            }
        } else if ((fosc_actual < ((u64)SI5324_FOSC_MIN) << 28) || (fosc_actual > ((u64)SI5324_FOSC_MAX) << 28)) {
            if (SI5324_DEBUG) {
                printk(KERN_INFO "Fosc frequency out of range.\n");
            }
        } else if ((fout_actual < ((u64)SI5324_FOUT_MIN) << 28) || (fout_actual >((u64)SI5324_FOUT_MAX) << 28)) {
            if (SI5324_DEBUG) {
                printk(KERN_INFO "Fout frequency out of range.\n");
            }
        } else {
            if (SI5324_DEBUG) {
                printk(KERN_INFO "Found solution: fout = %dHz delta = %dHz.\n",
                    (u32)(fout_actual >> 28), (u32)(delta_fout >> 28));
                printk(KERN_INFO "                fosc = %dkHz f3 = %dHz.\n",
                    (u32)((fosc_actual >> 28) / 1000), (u32)(f3_actual >> 28));
            }
            if (((u64)llabs(delta_fout)) < settings->best_delta_fout) {
                // Found a better solution: remember this one!
                if (SI5324_DEBUG) {
                    printk(KERN_INFO "This solution is the best yet!\n");
                }
                settings->best_n1_hs = settings->n1_hs;
                settings->best_nc1_ls = settings->nc1_ls;
                settings->best_n2_hs = settings->n2_hs;
                settings->best_n2_ls = settings->n2_ls;
                settings->best_n3 = settings->n31;
                settings->best_fout = fout_actual;
                settings->best_delta_fout = llabs(delta_fout);
                if (delta_fout == 0) {
                    // Best possible result found. Skip the rest of the possibilities.
                    result = 1;
                }
            }
        }
    }
    return result;
}


/*****************************************************************************/
/**
 * Find a valid setting for N2_HS and N2_LS. Finds the best
 * setting for N2_HS, N2_LS, and N3n with the values for N1_HS, and NCn_LS
 * already set in settings. Iterates over all possibilities
 * of N2_HS and then performs a binary search over the N2_LS values.
 *
 * @param settings Holds the settings up till now.
 * @return 1 when the best possible result has been found.
 * @note     Private function.
 */
int Si5324_FindN2(si5324_settings_t *settings) {
    u32 result;

    for (settings->n2_hs = SI5324_N2_HS_MAX; settings->n2_hs >= SI5324_N2_HS_MIN; settings->n2_hs--) {
        if (SI5324_DEBUG) {
            printk(KERN_INFO "Trying N2_HS = %d.\n", settings->n2_hs);
        }
        settings->n2_ls_min = (u32)(settings->fosc / ((u64)(SI5324_F3_MAX * settings->n2_hs) << 28));
        if (settings->n2_ls_min < SI5324_N2_LS_MIN) {
            settings->n2_ls_min = SI5324_N2_LS_MIN;
        }
        settings->n2_ls_max = (u32)(settings->fosc / ((u64)(SI5324_F3_MIN * settings->n2_hs) << 28));
        if (settings->n2_ls_max > SI5324_N2_LS_MAX) {
            settings->n2_ls_max = SI5324_N2_LS_MAX;
        }
        result = Si5324_FindN2ls(settings);
        if (result) {
            // Best possible result found. Skip the rest of the possibilities.
            break;
        }
    }
    return result;
}


/*****************************************************************************/
/**
 * Calculates the valid range for NCn_LS with the value for the output
 * frequency and N1_HS already set in settings.
 *
 * @param settings Holds the input and output frequencies and the setting
 *                 for N1_HS.
 * @return -1 when there are no valid settings for NCn_LS, 0 otherwise.
 * @note     Private function.
 */
int Si5324_CalcNclsLimits(si5324_settings_t *settings) {
    // Calculate limits for NCn_LS
    settings->nc1_ls_min = settings->n1_hs_min / settings->n1_hs;
    if (settings->nc1_ls_min < SI5324_NC_LS_MIN) {
        settings->nc1_ls_min = SI5324_NC_LS_MIN;
    }
    // Make sure NC_ls_min is one or even
    if ((settings->nc1_ls_min > 1) && ((settings->nc1_ls_min & 0x1) == 1)) {
        settings->nc1_ls_min++;
    }
    settings->nc1_ls_max = settings->n1_hs_max / settings->n1_hs;
    if (settings->nc1_ls_max > SI5324_NC_LS_MAX) {
        settings->nc1_ls_max = SI5324_NC_LS_MAX;
    }
    // Make sure NC_ls_max is even
    if ((settings->nc1_ls_max & 0x1) == 1) {
        settings->nc1_ls_max--;
    }
    // Check if actual N1 is within limits
    if ((settings->nc1_ls_max * settings->n1_hs < settings->n1_hs_min) ||
        (settings->nc1_ls_min * settings->n1_hs > settings->n1_hs_max)) {
        // No valid NC_ls possible: take next N1_hs
        return -1;
    }
    return 0;
}


/*****************************************************************************/
/**
 * Find a valid setting for NCn_LS that can deliver the correct output
 * frequency. Assumes that the valid range is relatively small so a full search
 * can be done (should be true for video clock frequencies).
 *
 * @param settings Holds the input and output frequencies, the setting for
 *                 N1_HS, and the limits for NCn_LS.
 * @return 1 when the best possible result has been found.
 * @note     Private function.
 */
int Si5324_FindNcls(si5324_settings_t *settings) {
    u64 fosc_1;
    u32 result;

    fosc_1 = settings->fout * settings->n1_hs;
    for (settings->nc1_ls = settings->nc1_ls_min; settings->nc1_ls <= settings->nc1_ls_max;) {
        settings->fosc = fosc_1 * settings->nc1_ls;
        if (SI5324_DEBUG) {
            printk(KERN_INFO "Trying NCn_LS = %d: fosc = %dkHz.\n",
                    settings->nc1_ls, (u32)((settings->fosc >> 28) / 1000));
        }
        result = Si5324_FindN2(settings);
        if (result) {
            // Best possible result found. Skip the rest of the possibilities.
            break;
        }
        if (settings->nc1_ls == 1) {
            settings->nc1_ls++;
        } else {
            settings->nc1_ls += 2;
        }
    }
    return result;
}

/*****************************************************************************/
/**
 * Calculate the frequency settings for the desired output frequency.
 *
 * @param    ClkInFreq contains the frequency of the input clock.
 * @param    ClkOutFreq contains the desired output clock frequency.
 * @param    N1_hs  will be set to the value for the N1_HS register.
 * @param    NCn_ls will be set to the value for the NCn_LS register.
 * @param    N2_hs  will be set to the value for the N2_HS register.
 * @param    N2_ls  will be set to the value for the N2_LS register.
 * @param    N3n    will be set to the value for the N3n register.
 * @param    BwSel  will be set to the value for the BW_SEL register.
 *
 * @return   SI5324_SUCCESS for success, SI5324_ERR_FREQ when the requested
 *           frequency cannot be generated.
 * @note     Private function.
 *****************************************************************************/
int Si5324_CalcFreqSettings(u32 ClkInFreq, u32 ClkOutFreq, u32 *ClkActual,
                        u8  *N1_hs, u32 *NCn_ls,
                        u8  *N2_hs, u32 *N2_ls,
                        u32 *N3n,   u8  *BwSel) {
    /* TBD */
    si5324_settings_t settings;
    int result;

    settings.fin = (u64)ClkInFreq  << 28; // 32.28 fixed point
    settings.fout= (u64)ClkOutFreq << 28; // 32.28 fixed point
    settings.best_delta_fout = settings.fout; // High frequency error to start with

    // Calculate some limits for N1_HS * NCn_LS and for N3 base on the input
    // and output frequencies.
    settings.n1_hs_min = (int)(SI5324_FOSC_MIN / ClkOutFreq);
    if (settings.n1_hs_min < SI5324_N1_HS_MIN * SI5324_NC_LS_MIN) {
        settings.n1_hs_min = SI5324_N1_HS_MIN * SI5324_NC_LS_MIN;
    }
    settings.n1_hs_max = (int)(SI5324_FOSC_MAX / ClkOutFreq);
    if (settings.n1_hs_max > SI5324_N1_HS_MAX * SI5324_NC_LS_MAX) {
        settings.n1_hs_max = SI5324_N1_HS_MAX * SI5324_NC_LS_MAX;
    }
    settings.n31_min = ClkInFreq / SI5324_F3_MAX;
    if (settings.n31_min < SI5324_N3_MIN) {
        settings.n31_min = SI5324_N3_MIN;
    }
    settings.n31_max = ClkInFreq / SI5324_F3_MIN;
    if (settings.n31_max > SI5324_N3_MAX) {
        settings.n31_max = SI5324_N3_MAX;
    }
    // Find a valid oscillator frequency with the highest setting of N1_HS
    // possible (reduces power)
    for (settings.n1_hs = SI5324_N1_HS_MAX; settings.n1_hs >= SI5324_N1_HS_MIN; settings.n1_hs--) {
        if (SI5324_DEBUG) {
            printk(KERN_INFO "Trying N1_HS = %d.\n", settings.n1_hs);
        }
        result = Si5324_CalcNclsLimits(&settings);
        if (result) {
            if (SI5324_DEBUG) {
                printk(KERN_INFO "No valid settings for NCn_LS.\n");
            }
            continue;
        }
        result = Si5324_FindNcls(&settings);
        if (result) {
            // Best possible result found. Skip the rest of the possibilities.
            break;
        }
    }
	
	if(SI5324_DEBUG) {
		printk(KERN_INFO "Si5324: settings.best_delta_fout = %llu\n", (unsigned long long)settings.best_delta_fout);
		printk(KERN_INFO "Si5324: settings.fout = %llu\n", (unsigned long long)settings.fout);
	}
	
    if (settings.best_delta_fout == settings.fout) {
        if (1 || SI5324_DEBUG) {
            printk(KERN_INFO "Si5324: ERROR: No valid settings found.");
        }
        return SI5324_ERR_FREQ;
    }
    if (SI5324_DEBUG) {
        printk(KERN_INFO "Si5324: Found solution: fout = %dHz.\n",
                   (u32)(settings.best_fout >> 28));
    }

    // Post processing: convert temporary values to actual register settings
    *N1_hs  = (u8)settings.best_n1_hs - 4;
    *NCn_ls =     settings.best_nc1_ls - 1;
    *N2_hs  = (u8)settings.best_n2_hs - 4;
    *N2_ls  =     settings.best_n2_ls - 1;
    *N3n    =     settings.best_n3    - 1;
    /* How must the bandwidth selection be determined? Not all settings will
     * be valid.
    refclk        2, 0xA2,  //              BWSEL_REG=1010 (?)
    free running  2, 0x42,  //              BWSEL_REG=0100 (?)
    */
    *BwSel  = 6; //4

    if (ClkActual) *ClkActual = (settings.best_fout >> 28);
    return SI5324_SUCCESS;

}
