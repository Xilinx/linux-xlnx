#ifndef __XILINX_AMS_H__
#define __XILINX_AMS_H__

#define AMS_MISC_CTRL     0x000
#define AMS_ISR_0         0x010
#define AMS_ISR_1         0x014
#define AMS_IMR_0         0x018
#define AMS_IMR_1         0x01c
#define AMS_IER_0         0x020
#define AMS_IER_1         0x024
#define AMS_IDR_0         0x028
#define AMS_IDR_1         0x02c
#define AMS_PS_CSTS       0x040
#define AMS_PL_CSTS       0x044
#define AMS_MON_CSTS      0x050

#define AMS_VCC_PSPLL0    0x060
#define AMS_VCC_PSPLL3    0x06C
#define AMS_VCCINT        0x078
#define AMS_VCCBRAM       0x07C
#define AMS_VCCAUX        0x080
#define AMS_PSDDRPLL      0x084
#define AMS_PSINTFPDDR    0x09C

#define AMS_VCC_PSPLL0_CH 48
#define AMS_VCC_PSPLL3_CH 51
#define AMS_VCCINT_CH     54
#define AMS_VCCBRAM_CH    55
#define AMS_VCCAUX_CH     56
#define AMS_PSDDRPLL_CH   57
#define AMS_PSINTFPDDR_CH 63

#define AMS_REG_CONFIG0   0x100
#define AMS_REG_CONFIG1   0x104
#define AMS_REG_CONFIG2   0x108
#define AMS_REG_CONFIG3   0x10C
#define AMS_REG_CONFIG4   0x110
#define AMS_REG_SEQ_CH0   0x120
#define AMS_REG_SEQ_CH1   0x124
#define AMS_REG_SEQ_CH2   0x118

#define AMS_TEMP          0x000
#define AMS_SUPPLY1       0x004
#define AMS_SUPPLY2       0x008
#define AMS_VP_VN         0x00c
#define AMS_VREFP         0x010
#define AMS_VREFN         0x014
#define AMS_SUPPLY3       0x018
#define AMS_SUPPLY4       0x034
#define AMS_SUPPLY5       0x038
#define AMS_SUPPLY6       0x03c
#define AMS_SUPPLY7       0x200
#define AMS_SUPPLY8       0x204
#define AMS_SUPPLY9       0x208
#define AMS_SUPPLY10      0x20c
#define AMS_VCCAMS        0x210
#define AMS_TEMP_REMOTE   0x214

#define AMS_REG_VAUX(x)   (0x40 + (4*(x)))
#define AMS_REG_VUSER(x)  (0x200 + (4*(x)))

#define AMS_PS_RESET_VALUE   0xFFFFU
#define AMS_PL_RESET_VALUE   0xFFFFU

#define AMS_CONF0_CHANNEL_NUM_MASK      (0x3f << 0)

#define AMS_CONF1_SEQ_MASK              (0xf << 12)
#define AMS_CONF1_SEQ_DEFAULT           (0 << 12)
#define AMS_CONF1_SEQ_SINGLE_PASS       (1 << 12)
#define AMS_CONF1_SEQ_CONTINUOUS        (2 << 12)
#define AMS_CONF1_SEQ_SINGLE_CHANNEL    (3 << 12)

#define AMS_REG_SEQ0_MASK        0xFFFF
#define AMS_REG_SEQ2_MASK        0x3F
#define AMS_REG_SEQ1_MASK        0xFFFF
#define AMS_REG_SEQ2_MASK_SHIFT  16
#define AMS_REG_SEQ1_MASK_SHIFT  22

#define AMS_REGCFG1_ALARM_MASK   0xF0F
#define AMS_REGCFG3_ALARM_MASK   0x3F

#define AMS_ALARM_TEMP            0x140
#define AMS_ALARM_SUPPLY1         0x144
#define AMS_ALARM_SUPPLY2         0x148
#define AMS_ALARM_OT              0x14c

#define AMS_ALARM_SUPPLY3         0x160
#define AMS_ALARM_SUPPLY4         0x164
#define AMS_ALARM_SUPPLY5         0x168
#define AMS_ALARM_SUPPLY6         0x16c
#define AMS_ALARM_SUPPLY7         0x180
#define AMS_ALARM_SUPPLY8         0x184
#define AMS_ALARM_SUPPLY9         0x188
#define AMS_ALARM_SUPPLY10        0x18c
#define AMS_ALARM_VCCAMS          0x190
#define AMS_ALARM_TEMP_REMOTE     0x194
#define AMS_ALARM_THRESOLD_OFF_10 0x10
#define AMS_ALARM_THRESOLD_OFF_20 0x20

#define AMS_ALARM_THR_DIRECT_MASK 0x01
#define AMS_ALARM_THR_MIN         0x0000
#define AMS_ALARM_THR_MAX         0xffff

#define AMS_NO_OF_ALARMS             32
#define AMS_PL_ALARM_START           16
#define AMS_ISR0_ALARM_MASK          0xFFFFFFFFU
#define AMS_ISR1_ALARM_MASK          0xE000001FU
#define AMS_ISR1_INTR_MASK_SHIFT     32
#define AMS_ISR0_ALARM_2_TO_0_MASK     0x07
#define AMS_ISR0_ALARM_6_TO_3_MASK     0x78
#define AMS_ISR0_ALARM_12_TO_7_MASK    0x3F
#define AMS_CONF1_ALARM_2_TO_0_SHIFT   1
#define AMS_CONF1_ALARM_6_TO_3_SHIFT   5
#define AMS_CONF3_ALARM_12_TO_7_SHIFT  8

#define AMS_PS_CSTS_PS_READY       0x08010000U
#define AMS_PL_CSTS_ACCESS_MASK    0x00000001U

#define AMS_PL_MAX_FIXED_CHANNEL   10
#define AMS_PL_MAX_EXT_CHANNEL     20

#define AMS_INIT_TIMEOUT	10000

/* Following scale and offset value is derivef from
 * UG580 (v1.7) December 20, 2016
 */
#define AMS_SUPPLY_SCALE_1VOLT     1000
#define AMS_SUPPLY_SCALE_3VOLT     3000
#define AMS_SUPPLY_SCALE_6VOLT     6000
#define AMS_SUPPLY_SCALE_DIV_BIT   16

#define AMS_TEMP_SCALE             509314
#define AMS_TEMP_SCALE_DIV_BIT     16
#define AMS_TEMP_OFFSET            -((280230L << 16) / 509314)

enum ams_alarm_bit {
	AMS_ALARM_BIT_TEMP,
	AMS_ALARM_BIT_SUPPLY1,
	AMS_ALARM_BIT_SUPPLY2,
	AMS_ALARM_BIT_SUPPLY3,
	AMS_ALARM_BIT_SUPPLY4,
	AMS_ALARM_BIT_SUPPLY5,
	AMS_ALARM_BIT_SUPPLY6,
	AMS_ALARM_BIT_RESERVED,
	AMS_ALARM_BIT_SUPPLY7,
	AMS_ALARM_BIT_SUPPLY8,
	AMS_ALARM_BIT_SUPPLY9,
	AMS_ALARM_BIT_SUPPLY10,
	AMS_ALARM_BIT_VCCAMS,
	AMS_ALARM_BIT_TEMP_REMOTE
};

enum ams_seq {
	AMS_SEQ_VCC_PSPLL,
	AMS_SEQ_VCC_PSBATT,
	AMS_SEQ_VCCINT,
	AMS_SEQ_VCCBRAM,
	AMS_SEQ_VCCAUX,
	AMS_SEQ_PSDDRPLL,
	AMS_SEQ_INTDDR
};

enum ams_ps_pl_seq {
	AMS_SEQ_CALIB,
	AMS_SEQ_RSVD_1,
	AMS_SEQ_RSVD_2,
	AMS_SEQ_TEST,
	AMS_SEQ_RSVD_4,
	AMS_SEQ_SUPPLY4,
	AMS_SEQ_SUPPLY5,
	AMS_SEQ_SUPPLY6,
	AMS_SEQ_TEMP,
	AMS_SEQ_SUPPLY2,
	AMS_SEQ_SUPPLY1,
	AMS_SEQ_VP_VN,
	AMS_SEQ_VREFP,
	AMS_SEQ_VREFN,
	AMS_SEQ_SUPPLY3,
	AMS_SEQ_CURRENT_MON,
	AMS_SEQ_SUPPLY7,
	AMS_SEQ_SUPPLY8,
	AMS_SEQ_SUPPLY9,
	AMS_SEQ_SUPPLY10,
	AMS_SEQ_VCCAMS,
	AMS_SEQ_TEMP_REMOTE,
	AMS_SEQ_MAX
};

#define AMS_SEQ(x)          (AMS_SEQ_MAX + (x))
#define AMS_VAUX_SEQ(x)     (AMS_SEQ_MAX + (x))

#define PS_SEQ_MAX          AMS_SEQ_MAX
#define PS_SEQ(x)           (x)
#define PL_SEQ(x)           (PS_SEQ_MAX + x)

#define AMS_CHAN_TEMP(_scan_index, _addr, _ext) { \
	.type = IIO_TEMP, \
	.indexed = 1, \
	.address = (_addr), \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
		BIT(IIO_CHAN_INFO_SCALE) | \
		BIT(IIO_CHAN_INFO_OFFSET), \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.event_spec = ams_temp_events, \
	.num_event_specs = ARRAY_SIZE(ams_temp_events), \
	.scan_index = (_scan_index), \
	.scan_type = { \
		.sign = 'u', \
		.realbits = 12, \
		.storagebits = 16, \
		.shift = 4, \
		.endianness = IIO_CPU, \
	}, \
	.extend_name = _ext, \
}

#define AMS_CHAN_VOLTAGE(_scan_index, _addr, _ext, _alarm) { \
	.type = IIO_VOLTAGE, \
	.indexed = 1, \
	.address = (_addr), \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
		BIT(IIO_CHAN_INFO_SCALE), \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.event_spec = (_alarm) ? ams_voltage_events : NULL, \
	.num_event_specs = (_alarm) ? ARRAY_SIZE(ams_voltage_events) : 0, \
	.scan_index = (_scan_index), \
	.scan_type = { \
		.realbits = 10, \
		.storagebits = 16, \
		.shift = 6, \
		.endianness = IIO_CPU, \
	}, \
	.extend_name = _ext, \
}

#define AMS_PS_CHAN_TEMP(_scan_index, _addr, _ext) \
	AMS_CHAN_TEMP(PS_SEQ(_scan_index), _addr, _ext)
#define AMS_PS_CHAN_VOLTAGE(_scan_index, _addr, _ext) \
	AMS_CHAN_VOLTAGE(PS_SEQ(_scan_index), _addr, _ext, true)

#define AMS_PL_CHAN_TEMP(_scan_index, _addr, _ext) \
	AMS_CHAN_TEMP(PL_SEQ(_scan_index), _addr, _ext)
#define AMS_PL_CHAN_VOLTAGE(_scan_index, _addr, _ext, _alarm) \
	AMS_CHAN_VOLTAGE(PL_SEQ(_scan_index), _addr, _ext, _alarm)
#define AMS_PL_AUX_CHAN_VOLTAGE(_auxno, _ext) \
	AMS_CHAN_VOLTAGE(PL_SEQ(AMS_VAUX_SEQ(_auxno)), \
			AMS_REG_VAUX(_auxno), _ext, false)
#define AMS_CTRL_CHAN_VOLTAGE(_scan_index, _addr, _ext) \
	AMS_CHAN_VOLTAGE(PL_SEQ(AMS_VAUX_SEQ(AMS_SEQ(_scan_index))), \
			_addr, _ext, false)

struct ams {
	void __iomem *base;
	void __iomem *ps_base;
	void __iomem *pl_base;
	struct clk *clk;
	struct device *dev;

	struct mutex mutex;
	spinlock_t lock;

	unsigned int alarm_mask;
	unsigned int masked_alarm;
	u64 intr_mask;
	int irq;

	struct delayed_work ams_unmask_work;
	const struct ams_pl_bus_ops *pl_bus;
};

struct ams_pl_bus_ops {
	void (*read)(struct ams *ams, unsigned int offset, unsigned int *data);
	void (*write)(struct ams *ams, unsigned int offset, unsigned int data);
	void (*update)(struct ams *ams, unsigned int offset, u32 mask,
			u32 data);
};

#endif /* __XILINX_AMS_H__ */
