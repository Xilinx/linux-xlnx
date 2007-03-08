#include <asm/arch/hardware.h>

void s3c44b0x_led_off(int bit)
{
	SYSREG_OR_SET(S3C44B0X_PDATE, 1<<(4+bit));
}

void s3c44b0x_led_on(int bit)
{	
	SYSREG_CLR(S3C44B0X_PDATE, 1<<(4+bit));
}

void s3c44b0x_led_disp(int data)
{
	data = (data << 12) >> 8;
	data = (~data) & 0x1ff;
	SYSREG_AND_SET(S3C44B0X_PDATE, ~data);
}

void s3c44b0x_led_init(void)
{
	SYSREG_AND_SET(S3C44B0X_PCONE, 0xffff556b);
	SYSREG_SET(S3C44B0X_PUPE, 0x6);
	SYSREG_SET(S3C44B0X_PDATE, 0x3f7);
	s3c44b0x_led_disp(15);
}


