#include "xaxipmon.h"
/*****************************************************************************/
/**
*
* This function resets all Metric Counters and Sampled Metric Counters of
* AXI Performance Monitor.
*
* @return	XST_SUCCESS
*
*
* @note		None.
*
******************************************************************************/
int resetmetriccounter(void)
{
	u32 regval;

	/*
	 * Write the reset value to the Control register to reset
	 * Metric counters
	 */
	regval = readreg(baseaddr, XAPM_CTL_OFFSET);
	writereg(baseaddr, XAPM_CTL_OFFSET,
					(regval | XAPM_CR_MCNTR_RESET_MASK));
	/*
	 * Release from Reset
	 */
	writereg(baseaddr, XAPM_CTL_OFFSET,
				(regval & ~(XAPM_CR_MCNTR_RESET_MASK)));
	return XST_SUCCESS;

}

/*****************************************************************************/
/**
*
* This function resets Global Clock Counter of AXI Performance Monitor
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void resetglobalclkcounter(void)
{

	u32 regval;

	/*
	 * Write the reset value to the Control register to reset
	 * Global Clock Counter
	 */
	regval = readreg(baseaddr, XAPM_CTL_OFFSET);
	writereg(baseaddr, XAPM_CTL_OFFSET,
					(regval | XAPM_CR_GCC_RESET_MASK));

	/*
	 * Release from Reset
	 */
	writereg(baseaddr, XAPM_CTL_OFFSET,
				(regval & ~(XAPM_CR_GCC_RESET_MASK)));

}

/*****************************************************************************/
/**
*
* This function resets Streaming FIFO of AXI Performance Monitor
*
* @return	XST_SUCCESS
*
* @note		None.
*
******************************************************************************/
int resetfifo(void)
{
	u32 regval;

	/* Check Event Logging is enabled in Hardware */
	if (params->eventlog == 0)
		/*Event Logging not enabled in Hardware*/
		return XST_SUCCESS;

	/*
	 * Write the reset value to the Control register to reset
	 * FIFO
	 */
	regval = readreg(baseaddr, XAPM_CTL_OFFSET);
	writereg(baseaddr, XAPM_CTL_OFFSET,
					(regval | XAPM_CR_FIFO_RESET_MASK));
	/*
	 * Release from Reset
	 */
	writereg(baseaddr, XAPM_CTL_OFFSET,
				(regval & ~(XAPM_CR_FIFO_RESET_MASK)));

	return XST_SUCCESS;
}

/****************************************************************************/
/**
*
* This function sets Ranges for Incrementers depending on parameters passed.
*
* @param	incrementer specifies the Incrementer for which Ranges
*		need to be set
* @param	rangehigh specifies the Upper limit in 32 bit Register
* @param	rangelow specifies the Lower limit in 32 bit Register
*
* @return	None.
*
* @note		None
*
*****************************************************************************/
void setincrementerrange(u8 incrementer, u16 rangehigh, u16 rangelow)
{
	u32 regval;

	/*
	 * Write to the specified Range register
	 */
	regval = rangehigh << 16;
	regval |= rangelow;
	writereg(baseaddr,
		(XAPM_RANGE0_OFFSET + (incrementer * 16)), regval);
}

/****************************************************************************/
/**
*
* This function returns the Ranges of Incrementers Registers.
*
* @param	incrementer specifies the Incrementer for which Ranges
*		need to be returned.
* @param	rangehigh specifies the user reference variable which returns
*		the Upper Range Value of the specified Incrementer.
* @param	rangelow specifies the user reference variable which returns
*		the Lower Range Value of the specified Incrementer.
*
* @return	None.
*
* @note		None
*
*****************************************************************************/
void getincrementerrange(u8 incrementer, u16 *rangehigh, u16 *rangelow)
{
	u32 regval;

	regval = readreg(baseaddr, (XAPM_RANGE0_OFFSET +
					(incrementer * 16)));

	*rangelow = regval & 0xFFFF;
	*rangehigh = (regval >> 16) & 0xFFFF;
}

/****************************************************************************/
/**
*
* This function sets the Sample Interval Register
*
* @param	sampleinterval is the Sample Interval
*
* @return	None
*
* @note		None.
*
*****************************************************************************/
void setsampleinterval(u32 sampleinterval)
{
	/*
	 * Set Sample Interval
	 */
	writereg(baseaddr, XAPM_SI_LOW_OFFSET, sampleinterval);

}

/****************************************************************************/
/**
*
* This function returns the contents of Sample Interval Register
*
* @param	sampleinterval is a pointer where Sample Interval register
*		contents are returned.
* @return	None.
*
* @note		None.
*
******************************************************************************/
void getsampleinterval(u32 *sampleinterval)
{
	/*
	 * Set Sample Interval Lower
	 */
	*sampleinterval = readreg(baseaddr, XAPM_SI_LOW_OFFSET);

}

/****************************************************************************/
/**
*
* This function sets metrics for specified Counter in the corresponding
* Metric Selector Register.
*
* @param	slot is the slot ID for which specified counter has to
*		be connected.
* @param	metrics is one of the Metric Sets. User has to use
*		XAPM_METRIC_SET_* macros in xaxipmon.h for this parameter
* @param	counter is the Counter Number.
*		The valid values are 0 to 9.
*
* @return	XST_SUCCESS if Success
*		XST_FAILURE if Failure
*
* @note		None.
*
*****************************************************************************/
int setmetrics(u8 slot, u8 metrics, u8 counter)
{
	u32 regval;
	u32 mask;

	/* Find mask value to force zero in counternum byte range */
	if (counter == 0 || counter == 4 || counter == 8)
		mask = 0xFFFFFF00;
	else if (counter == 1 || counter == 5 || counter == 9)
		mask = 0xFFFF00FF;
	else if (counter == 2 || counter == 6)
		mask = 0xFF00FFFF;
	else
		mask = 0x00FFFFFF;

	if (counter <= 3) {
		regval = readreg(baseaddr, XAPM_MSR0_OFFSET);
		regval = regval & mask;
		regval = regval | (metrics << (counter * 8));
		regval = regval | (slot << (counter * 8 + 5));
		writereg(baseaddr, XAPM_MSR0_OFFSET, regval);
	} else if ((counter >= 4) && (counter <= 7)) {
		counter = counter - 4;
		regval = readreg(baseaddr, XAPM_MSR1_OFFSET);
		regval = regval & mask;
		regval = regval | (metrics << (counter * 8));
		regval = regval | (slot << (counter * 8 + 5));
		writereg(baseaddr, XAPM_MSR1_OFFSET, regval);
	} else {
		counter = counter - 8;
		regval = readreg(baseaddr, XAPM_MSR2_OFFSET);

		regval = regval & mask;
		regval = regval | (metrics << (counter * 8));
		regval = regval | (slot << (counter * 8 + 5));
		writereg(baseaddr, XAPM_MSR2_OFFSET, regval);
	}
	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* This function returns metrics in the specified Counter from the corresponding
* Metric Selector Register.
*
* @param	counter is the Counter Number.
*		The valid values are 0 to 9.
* @param	metrics is a reference parameter from application where metrics
*		of specified counter is filled.
* @praram	slot is a reference parameter in which slot Id of
*		specified counter is filled
* @return	XST_SUCCESS if Success
*		XST_FAILURE if Failure
*
* @note		None.
*
*****************************************************************************/
int getmetrics(u8 counter, u8 *metrics, u8 *slot)
{
	u32 regval;

	if (counter <= 3) {
		regval = readreg(baseaddr, XAPM_MSR0_OFFSET);
		*metrics = (regval >> (counter * 8)) & 0x1F;
		*slot	= (regval >> (counter * 8 + 5)) & 0x7;
	} else if ((counter >= 4) && (counter <= 7)) {
		counter = counter - 4;
		regval = readreg(baseaddr, XAPM_MSR1_OFFSET);
		*metrics = (regval >> (counter * 8)) & 0x1F;
		*slot	= (regval >> (counter * 8 + 5)) & 0x7;
	} else {
		counter = counter - 8;
		regval = readreg(baseaddr, XAPM_MSR2_OFFSET);
		*metrics = (regval >> (counter * 8)) & 0x1F;
		*slot	= (regval >> (counter * 8 + 5)) & 0x7;
	}
	return XST_SUCCESS;
}

/****************************************************************************/
/**
*
* This function returns the contents of the Global Clock Counter Register.
*
* @param	cnthigh is the user space pointer with which upper 32 bits
*		of Global Clock Counter has to be filled
* @param	cntlow is the user space pointer with which lower 32 bits
*		of Global Clock Counter has to be filled
*
* @return	None.
*
* @note		None.
*
*****************************************************************************/
void getglobalclkcounter(u32 *cnthigh, u32 *cntlow)
{
	*cnthigh = 0x0;
	*cntlow  = 0x0;

	/*
	 * If Counter width is 64 bit then Counter Value has to be
	 * filled at CntHighValue address also.
	 */
	if (params->globalcntwidth == 64) {
		/* Bits[63:32] exists at XAPM_GCC_HIGH_OFFSET */
		*cnthigh = readreg(baseaddr, XAPM_GCC_HIGH_OFFSET);
	}
	/* Bits[31:0] exists at XAPM_GCC_LOW_OFFSET */
	*cntlow = readreg(baseaddr, XAPM_GCC_LOW_OFFSET);
}

/****************************************************************************/
/**
*
* This function returns the contents of the Metric Counter Register.
*
* @param	counter is the number of the Metric Counter to be read.
*		Use the XAPM_METRIC_COUNTER* defines for the counter number in
*		xaxipmon.h. The valid values are 0 (XAPM_METRIC_COUNTER_0) to
*		9 (XAPM_METRIC_COUNTER_9).
* @return	regval is the content of specified Metric Counter.
*
* @note		None.
*
*****************************************************************************/
u32 getmetriccounter(u32 counter)
{
	u32 regval;

	regval = readreg(baseaddr,
				(XAPM_MC0_OFFSET + (counter * 16)));
	return regval;
}

/****************************************************************************/
/**
*
* This function returns the contents of the Sampled Metric Counter Register.
*
* @param	counter is the number of the Sampled Metric Counter to read.
*		Use the XAPM_METRIC_COUNTER* defines for the counter number in
*		xaxipmon.h. The valid values are 0 (XAPM_METRIC_COUNTER_0) to
*		9 (XAPM_METRIC_COUNTER_9).
*
* @return	regval is the content of specified Sampled Metric Counter.
*
* @note		None.
*
*****************************************************************************/
u32 getsampledmetriccounter(u32 counter)
{
	u32 regval;

	regval = readreg(baseaddr, (XAPM_SMC0_OFFSET +
						(counter * 16)));
	return regval;
}

/****************************************************************************/
/**
*
* This function returns the contents of the Incrementer Register.
*
* @param	incrementer is the number of the Incrementer register to
*		read.Use the XAPM_INCREMENTER_* defines for the Incrementer
*		number.The valid values are 0 (XAPM_INCREMENTER_0) to
*		9 (XAPM_INCREMENTER_9).
* @param	incrementer is the number of the specified Incrementer
*		register
* @return	regval is content of specified Metric Incrementer register.
*
* @note		None.
*
*****************************************************************************/
u32 getincrementer(u32 incrementer)
{
	u32 regval;

	regval = readreg(baseaddr, (XAPM_INC0_OFFSET +
						(incrementer * 16)));
	return regval;
}

/****************************************************************************/
/**
*
* This function returns the contents of the Sampled Incrementer Register.
*
* @param	incrementer is the number of the Sampled Incrementer
*		register to read.Use the XAPM_INCREMENTER_* defines for the
*		Incrementer number.The valid values are 0 (XAPM_INCREMENTER_0)
*		to 9 (XAPM_INCREMENTER_9).
* @param	incrementer is the number of the specified Sampled
*		Incrementer register
* @return	regval is content of specified Sampled Incrementer register.
*
* @note		None.
*
*****************************************************************************/
u32 getsampledincrementer(u32 incrementer)
{
	u32 regval;

	regval = readreg(baseaddr, (XAPM_SINC0_OFFSET +
					(incrementer * 16)));
	return regval;
}

/****************************************************************************/
/**
*
* This function sets Software-written Data Register.
*
* @param	swdata is the Software written Data.
*
* @return	None.
*
* @note		None.
*
*****************************************************************************/
void setswdatareg(u32 swdata)
{
	/*
	 * Set Software-written Data Register
	 */
	writereg(baseaddr, XAPM_SWD_OFFSET, swdata);
}

/****************************************************************************/
/**
*
* This function returns contents of Software-written Data Register.
*
* @return	swdata.
*
* @note		None.
*
*****************************************************************************/
u32 getswdatareg(void)
{
	u32 swdata;

	/*
	 * Set Metric Selector Register
	 */
	swdata = (u32)readreg(baseaddr, XAPM_SWD_OFFSET);
	return swdata;
}

/*****************************************************************************/
/**
*
* This function enables the following in the AXI Performance Monitor:
*   - Event logging
*
* @param        flagenables is a value to write to the flag enables
*               register defined by XAPM_FEC_OFFSET. It is recommended
*               to use the XAPM_FEC_*_MASK mask bits to generate.
*               A value of 0x0 will disable all events to the event
*               log streaming FIFO.
*
* @return       XST_SUCCESS
*
* @note         None
*
******************************************************************************/
int starteventlog(u32 flagenables)
{
	u32 regval;

	/* Read current register value */
	regval = readreg(baseaddr, XAPM_CTL_OFFSET);
	/* Now write to flag enables register */
	writereg(baseaddr, XAPM_FEC_OFFSET, flagenables);
	/* Write the new value to the Control register to
	 *	enable event logging */
	writereg(baseaddr, XAPM_CTL_OFFSET,
			regval | XAPM_CR_EVENTLOG_ENABLE_MASK);
	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* This function disables the following in the AXI Performance Monitor:
*   - Event logging
*
* @return       XST_SUCCESS
*
* @note         None
*
******************************************************************************/
int stopeventlog(void)
{
	u32 regval;

	/* Read current register value */
	regval = readreg(baseaddr, XAPM_CTL_OFFSET);

	/* Write the new value to the Control register to disable
	 * event logging */
	writereg(baseaddr, XAPM_CTL_OFFSET,
				regval & ~XAPM_CR_EVENTLOG_ENABLE_MASK);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* This function enables the following in the AXI Performance Monitor:
*   - Global clock counter
*   - All metric counters
*   - All sampled metric counters
*
* @param    sampleinterval is the sample interval
* @return   XST_SUCCESS
*
* @note	    None
******************************************************************************/
int startcounters(u32 sampleinterval)
{
	u32 regval;

	/* Read current register value */
	regval = readreg(baseaddr, XAPM_CTL_OFFSET);

	/* Global Clock Counter is present in Advanced Mode only */
	if (params->mode == 1)
		regval = regval | XAPM_CR_GCC_ENABLE_MASK;
	/*
	 * Write the new value to the Control register to enable
	 * global clock counter and metric counters
	 */
	writereg(baseaddr, XAPM_CTL_OFFSET, regval | XAPM_CR_MCNTR_ENABLE_MASK);

	/* Set, enable, and load sampled counters */
	setsampleinterval(sampleinterval);
	loadsic();
	enablesic();

	return XST_SUCCESS;
}

/****************************************************************************/
/**
*
* This function disables the following in the AXI Performance Monitor:
*   - Global clock counter
*   - All metric counters
*
* @return       XST_SUCCESS
*
* @note         None
*
******************************************************************************/
int stopcounters(void)
{
	u32 regval;

	/* Read current register value */
	regval = readreg(baseaddr, XAPM_CTL_OFFSET);

	/* Global Clock Counter is present in Advanced Mode only */
	if (params->mode == 1)
		regval = regval & ~XAPM_CR_GCC_ENABLE_MASK;

	/*
	 * Write the new value to the Control register to disable
	 * global clock counter and metric counters
	 */
	writereg(baseaddr, XAPM_CTL_OFFSET,
			regval & ~XAPM_CR_MCNTR_ENABLE_MASK);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* This function enables Metric Counters.
*
* @return	None
*
* @note		None
*
******************************************************************************/
void enablemetricscounter(void)
{
	u32 regval;

	regval = readreg(baseaddr, XAPM_CTL_OFFSET);
	writereg(baseaddr, XAPM_CTL_OFFSET,
					regval | XAPM_CR_MCNTR_ENABLE_MASK);
}
/****************************************************************************/
/**
*
* This function disables the Metric Counters.
*
* @return	None
*
* @note		None
*
*****************************************************************************/
void disablemetricscounter(void)
{
	u32 regval;

	regval = readreg(baseaddr, XAPM_CTL_OFFSET);

	writereg(baseaddr, XAPM_CTL_OFFSET,
					regval & ~(XAPM_CR_MCNTR_ENABLE_MASK));
}

/****************************************************************************/
/**
*
* This function sets the Upper and Lower Ranges for specified Metric Counter
* Log Enable Register.Event Logging starts when corresponding Metric Counter
* value falls in between these ranges
*
* @param	counter is the Metric Counter number for which
*		Ranges are to be assigned.Use the XAPM_METRIC_COUNTER*
*		defines for the counter number in xaxipmon.h.
*		The valid values are 0 (XAPM_METRIC_COUNTER_0) to
*		9 (XAPM_METRIC_COUNTER_9).
* @param	rangehigh specifies the Upper limit in 32 bit Register
* @param	rangelow specifies the Lower limit in 32 bit Register
* @return	None
*
* @note		None.
*
*****************************************************************************/
void setlogenableranges(u32 counter, u16 rangehigh, u16 rangelow)
{
	u32 regval;

	/*
	 * Write the specified Ranges to corresponding Metric Counter Log
	 * Enable Register
	 */
	regval = rangehigh << 16;
	regval |= rangelow;
	writereg(baseaddr, (XAPM_MC0LOGEN_OFFSET +
					(counter * 16)), regval);
}

/****************************************************************************/
/**
*
* This function returns the Ranges of specified Metric Counter Log
* Enable Register.
*
* @param	counter is the Metric Counter number for which
*		Ranges are to be returned.Use the XAPM_METRIC_COUNTER*
*		defines for the counter number in xaxipmon.h.
*		The valid values are 0 (XAPM_METRIC_COUNTER_0) to
*		9 (XAPM_METRIC_COUNTER_9).
*
* @param	rangehigh specifies the user reference variable which returns
*		the Upper Range Value of the specified Metric Counter
*		Log Enable Register.
* @param	rangelow specifies the user reference variable which returns
*		the Lower Range Value of the specified Metric Counter
*		Log Enable Register.
*
* @note		None.
*
*****************************************************************************/
void getlogenableranges(u32 counter, u16 *rangehigh, u16 *rangelow)
{
	u32 regval;

	regval = readreg(baseaddr,
				(XAPM_MC0LOGEN_OFFSET + (counter * 16)));

	*rangelow = regval & 0xFFFF;
	*rangehigh = (regval >> 16) & 0xFFFF;
}

/*****************************************************************************/
/**
*
* This function enables Event Logging.
*
* @return	None
*
* @note		None
*
******************************************************************************/
void enableeventlog(void)
{
	u32 regval;

	regval = readreg(baseaddr, XAPM_CTL_OFFSET);
	writereg(baseaddr, XAPM_CTL_OFFSET,
				regval | XAPM_CR_EVENTLOG_ENABLE_MASK);
}

/*****************************************************************************/
/**
*
* This function enables External trigger pulse so that Metric Counters can be
* started on external trigger pulse for a slot.
*
*
* @return	None
*
* @note		None
*
******************************************************************************/
void enablemctrigger(void)
{
	u32 regval;

	regval = readreg(baseaddr, XAPM_CTL_OFFSET);
	writereg(baseaddr, XAPM_CTL_OFFSET,
			regval | XAPM_CR_MCNTR_EXTTRIGGER_MASK);
}

/****************************************************************************/
/**
*
* This function disables the External trigger pulse used to start Metric
* Counters on external trigger pulse for a slot.
*
* @return	None
*
* @note		None
*
*****************************************************************************/
void disablemctrigger(void)
{
	u32 regval;

	regval = readreg(baseaddr, XAPM_CTL_OFFSET);

	writereg(baseaddr, XAPM_CTL_OFFSET,
			regval & ~(XAPM_CR_MCNTR_EXTTRIGGER_MASK));
}

/*****************************************************************************/
/**
*
* This function enables External trigger pulse for Event Log
* so that Event Logging can be started on external trigger pulse for a slot.
*
* @return	None
*
* @note		None
*
******************************************************************************/
void enableeventlogtrigger(void)
{
	u32 regval;

	regval = readreg(baseaddr, XAPM_CTL_OFFSET);
	writereg(baseaddr, XAPM_CTL_OFFSET,
				regval | XAPM_CR_EVTLOG_EXTTRIGGER_MASK);
}

/****************************************************************************/
/**
*
* This function disables the External trigger pulse used to start Event
* Log on external trigger pulse for a slot.
*
* @return	None
*
* @note		None
*
*****************************************************************************/
void disableeventlogtrigger(void)
{
	u32 regval;

	regval = readreg(baseaddr, XAPM_CTL_OFFSET);

	writereg(baseaddr, XAPM_CTL_OFFSET,
				regval & ~(XAPM_CR_EVTLOG_EXTTRIGGER_MASK));
}

/****************************************************************************/
/**
*
* This function returns a name for a given Metric.
*
* @param        metrics is one of the Metric Sets. User has to use
*               XAPM_METRIC_SET_* macros in xaxipmon.h for this parameter
*
* @return       const char *
*
* @note         None
*
*****************************************************************************/
const char *getmetricname(u8 metrics)
{
	if (metrics == XAPM_METRIC_SET_0)
		return "Write Transaction Count";
	if (metrics == XAPM_METRIC_SET_1)
		return "Read Transaction Count";
	if (metrics == XAPM_METRIC_SET_2)
		return "Write Byte Count";
	if (metrics == XAPM_METRIC_SET_3)
		return "Read Byte Count";
	if (metrics == XAPM_METRIC_SET_4)
		return "Write Beat Count";
	if (metrics == XAPM_METRIC_SET_5)
		return "Total Read Latency";
	if (metrics == XAPM_METRIC_SET_6)
		return "Total Write Latency";
	if (metrics == XAPM_METRIC_SET_7)
		return "Slv_Wr_Idle_Cnt";
	if (metrics == XAPM_METRIC_SET_8)
		return "Mst_Rd_Idle_Cnt";
	if (metrics == XAPM_METRIC_SET_9)
		return "Num_BValids";
	if (metrics == XAPM_METRIC_SET_10)
		return "Num_WLasts";
	if (metrics == XAPM_METRIC_SET_11)
		return "Num_RLasts";
	if (metrics == XAPM_METRIC_SET_12)
		return "Minimum Write Latency";
	if (metrics == XAPM_METRIC_SET_13)
		return "Maximum Write Latency";
	if (metrics == XAPM_METRIC_SET_14)
		return "Minimum Read Latency";
	if (metrics == XAPM_METRIC_SET_15)
		return "Maximum Read Latency";
	if (metrics == XAPM_METRIC_SET_16)
		return "Transfer Cycle Count";
	if (metrics == XAPM_METRIC_SET_17)
		return "Packet Count";
	if (metrics == XAPM_METRIC_SET_18)
		return "Data Byte Count";
	if (metrics == XAPM_METRIC_SET_19)
		return "Position Byte Count";
	if (metrics == XAPM_METRIC_SET_20)
		return "Null Byte Count";
	if (metrics == XAPM_METRIC_SET_21)
		return "Slv_Idle_Cnt";
	if (metrics == XAPM_METRIC_SET_22)
		return "Mst_Idle_Cnt";
	if (metrics == XAPM_METRIC_SET_30)
		return "External event count";
	return "Unsupported";
}

/****************************************************************************/
/**
*
* This function sets Write ID in Latency ID register to capture Write
* Latency metrics.
*
* @param	writeid is the Write ID to be written in Latency ID register.
*
* @return	None.
*
* @note		None.
*
*****************************************************************************/
void setwriteid(u32 writeid)
{
	u32 regval;

	if (params->is_32bit_filter == 0) {
		regval = readreg(baseaddr, XAPM_ID_OFFSET);
		regval = regval & ~(XAPM_ID_WID_MASK);
		regval = regval | writeid;
		writereg(baseaddr, XAPM_ID_OFFSET, regval);
	} else {
		writereg(baseaddr, XAPM_ID_OFFSET, writeid);
	}
}

/****************************************************************************/
/**
*
* This function sets Read ID in Latency ID register to capture
* Read Latency metrics.
*
* @param	readid is the Read ID to be written in Latency ID register.
*
* @return	None.
*
* @note		None.
*
*****************************************************************************/
void setreadid(u32 readid)
{
	u32 regval;

	if (params->is_32bit_filter == 0) {
		regval = readreg(baseaddr, XAPM_ID_OFFSET);
		regval = regval & ~(XAPM_ID_RID_MASK);
		regval = regval | (readid << 16);
		writereg(baseaddr, XAPM_ID_OFFSET, regval);
	} else {
		writereg(baseaddr, XAPM_RID_OFFSET, readid);
	}
}

/****************************************************************************/
/**
*
* This function returns Write ID in Latency ID register.
*
* @return	writeid is the required Write ID in Latency ID register.
*
* @note		None.
*
*****************************************************************************/
u32 getwriteid(void)
{

	u32 writeid;
	u32 regval;

	if (params->is_32bit_filter == 0) {
		regval = readreg(baseaddr, XAPM_ID_OFFSET);
		writeid = regval & XAPM_ID_WID_MASK;
	} else {
		writeid = XAPM_IDMASK_OFFSET;
	}

	return writeid;
}

/****************************************************************************/
/**
*
* This function returns Read ID in Latency ID register.
*
* @return	readid is the required Read ID in Latency ID register.
*
* @note		None.
*
*****************************************************************************/
u32 getreadid(void)
{

	u32 readid;
	u32 regval;

	if (params->is_32bit_filter == 0) {
		regval = readreg(baseaddr, XAPM_ID_OFFSET);
		regval = regval & XAPM_ID_RID_MASK;
		readid = regval >> 16;
	} else {
		readid = XAPM_RID_OFFSET;
	}

	return readid;
}

/*****************************************************************************/
/**
*
* This function sets Latency Start point to calculate write latency.
*
* @param	Param can be 0 - XAPM_LATENCY_ADDR_ISSUE
*		or 1 - XAPM_LATENCY_ADDR_ACCEPT
* @return	None
*
* @note		None
*
******************************************************************************/
void setwrlatencystart(u8 param)
{
	u32 regval;

	regval = readreg(baseaddr, XAPM_CTL_OFFSET);
	if (param == XAPM_LATENCY_ADDR_ACCEPT)
		writereg(baseaddr, XAPM_CTL_OFFSET, regval |
				XAPM_CR_WRLATENCY_START_MASK);
	else
		writereg(baseaddr, XAPM_CTL_OFFSET, readreg(baseaddr,
			XAPM_CTL_OFFSET) & ~(XAPM_CR_WRLATENCY_START_MASK));
}

/*****************************************************************************/
/**
*
* This function sets Latency End point to calculate write latency.
*
* @param	Param can be 0 - XAPM_LATENCY_LASTWR
*		or 1 - XAPM_LATENCY_FIRSTWR
* @return	None
*
* @note		None
*
******************************************************************************/
void setwrlatencyend(u8 param)
{
	u32 regval;

	regval = readreg(baseaddr, XAPM_CTL_OFFSET);
	if (param == XAPM_LATENCY_FIRSTWR)
		writereg(baseaddr, XAPM_CTL_OFFSET, regval |
					XAPM_CR_WRLATENCY_END_MASK);
	else
		writereg(baseaddr, XAPM_CTL_OFFSET, readreg(baseaddr,
			XAPM_CTL_OFFSET) & ~(XAPM_CR_WRLATENCY_END_MASK));
}

/*****************************************************************************/
/**
*
* This function sets Latency Start point to calculate read latency.
*
* @param	Param can be 0 - XAPM_LATENCY_ADDR_ISSUE
*		or 1 - XAPM_LATENCY_ADDR_ACCEPT
* @return	None
*
* @note		None
*
******************************************************************************/
void setrdlatencystart(u8 param)
{
	u32 regval;

	regval = readreg(baseaddr, XAPM_CTL_OFFSET);
	if (param == XAPM_LATENCY_ADDR_ACCEPT)
		writereg(baseaddr, XAPM_CTL_OFFSET, regval |
					XAPM_CR_RDLATENCY_START_MASK);
	else
		writereg(baseaddr, XAPM_CTL_OFFSET, readreg(baseaddr,
			XAPM_CTL_OFFSET) & ~(XAPM_CR_RDLATENCY_START_MASK));
}

/*****************************************************************************/
/**
*
* This function sets Latency End point to calculate read latency.
*
* @param	Param can be 0 - XAPM_LATENCY_LASTRD
*		or 1 - XAPM_LATENCY_FIRSTRD
* @return	None
*
* @note		None
*
******************************************************************************/
void setrdlatencyend(u8 param)
{
	u32 regval;

	regval = readreg(baseaddr, XAPM_CTL_OFFSET);
	if (param == XAPM_LATENCY_FIRSTRD)
		writereg(baseaddr, XAPM_CTL_OFFSET, regval |
				XAPM_CR_RDLATENCY_END_MASK);
	else
		writereg(baseaddr, XAPM_CTL_OFFSET, readreg(baseaddr,
			XAPM_CTL_OFFSET) & ~(XAPM_CR_RDLATENCY_END_MASK));
}

/*****************************************************************************/
/**
*
* This function returns Write Latency Start point.
*
* @return	Returns 0 - XAPM_LATENCY_ADDR_ISSUE or
*			1 - XAPM_LATENCY_ADDR_ACCEPT
*
* @note		None
*
******************************************************************************/
u8 getwrlatencystart(void)
{
	u8 regval;

	regval = readreg(baseaddr, XAPM_CTL_OFFSET);
	regval = regval & XAPM_CR_WRLATENCY_START_MASK;
	if (regval != XAPM_LATENCY_ADDR_ISSUE)
		return XAPM_LATENCY_ADDR_ACCEPT;
	else
		return XAPM_LATENCY_ADDR_ISSUE;
}

/*****************************************************************************/
/**
*
* This function returns Write Latency End point.
*
* @return	Returns 0 - XAPM_LATENCY_LASTWR or
*			1 - XAPM_LATENCY_FIRSTWR.
*
* @note		None
*
******************************************************************************/
u8 getwrlatencyend(void)
{
	u8 regval;

	regval = readreg(baseaddr, XAPM_CTL_OFFSET);
	regval = regval & XAPM_CR_WRLATENCY_END_MASK;
	if (regval != XAPM_LATENCY_LASTWR)
		return XAPM_LATENCY_FIRSTWR;
	else
		return XAPM_LATENCY_LASTWR;
}

/*****************************************************************************/
/**
*
* This function returns read Latency Start point.
*
* @return	Returns 0 - XAPM_LATENCY_ADDR_ISSUE or
*			1 - XAPM_LATENCY_ADDR_ACCEPT
*
* @note		None
*
******************************************************************************/
u8 getrdlatencystart(void)
{
	u8 regval;

	regval = readreg(baseaddr, XAPM_CTL_OFFSET);
	regval = regval & XAPM_CR_RDLATENCY_START_MASK;

	if (regval != XAPM_LATENCY_ADDR_ISSUE)
		return	XAPM_LATENCY_ADDR_ACCEPT;
	else
		return XAPM_LATENCY_ADDR_ISSUE;
}

/*****************************************************************************/
/**
*
* This function returns Read Latency End point.
*
* @return	Returns 0 - XAPM_LATENCY_LASTRD or
*			1 - XAPM_LATENCY_FIRSTRD.
*
* @note		None
*
******************************************************************************/
u8 getrdlatencyend(void)
{
	u8 regval;

	regval = readreg(baseaddr, XAPM_CTL_OFFSET);
	regval = regval & XAPM_CR_RDLATENCY_END_MASK;
	if (regval != XAPM_LATENCY_LASTRD)
		return XAPM_LATENCY_FIRSTRD;
	else
		return XAPM_LATENCY_LASTRD;

}

/****************************************************************************/
/**
*
* This function sets Write ID Mask in ID Mask register.
*
* @param	wrmask is the Write ID mask to be written in ID register.
*
* @return	None.
*
* @note		None.
*
*****************************************************************************/
void setwriteidmask(u32 wrmask)
{
	u32 regval;

	if (params->is_32bit_filter == 0) {
		regval = readreg(baseaddr, XAPM_IDMASK_OFFSET);
		regval = regval & ~(XAPM_MASKID_WID_MASK);
		regval = regval | wrmask;
		writereg(baseaddr, XAPM_IDMASK_OFFSET, regval);
	} else {
		writereg(baseaddr, XAPM_IDMASK_OFFSET, wrmask);
	}
}

/****************************************************************************/
/**
*
* This function sets Read ID Mask in ID Mask register.
*
* @param	rdmask is the Read ID mask to be written in ID Mask register.
*
* @return	None.
*
* @note		None.
*
*****************************************************************************/
void setreadidmask(u32 rdmask)
{
	u32 regval;

	if (params->is_32bit_filter == 0) {
		regval = readreg(baseaddr, XAPM_IDMASK_OFFSET);
		regval = regval & ~(XAPM_MASKID_RID_MASK);
		regval = regval | (rdmask << 16);
		writereg(baseaddr, XAPM_IDMASK_OFFSET, regval);
	} else {
		writereg(baseaddr, XAPM_RIDMASK_OFFSET, rdmask);
	}
}

/****************************************************************************/
/**
*
* This function returns Write ID Mask in ID Mask register.
*
* @return	wrmask is the required Write ID Mask in ID Mask register.
*
* @note		None.
*
*****************************************************************************/
u32 getwriteidmask(void)
{

	u32 wrmask;
	u32 regval;

	if (params->is_32bit_filter == 0) {
		regval = readreg(baseaddr, XAPM_IDMASK_OFFSET);
		wrmask = regval & XAPM_MASKID_WID_MASK;
	} else {
		wrmask = XAPM_IDMASK_OFFSET;
	}
	return wrmask;
}

/****************************************************************************/
/**
*
* This function returns Read ID Mask in ID Mask register.
*
* @return	rdmask is the required Read ID Mask in ID Mask register.
*
* @note		None.
*
*****************************************************************************/
u32 getreadidmask(void)
{

	u32 rdmask;
	u32 regval;

	if (params->is_32bit_filter == 0) {
		regval = readreg(baseaddr, XAPM_IDMASK_OFFSET);
		regval = regval & XAPM_MASKID_RID_MASK;
		rdmask = regval >> 16;
	} else {
		rdmask = XAPM_RIDMASK_OFFSET;
	}
	return rdmask;
}
