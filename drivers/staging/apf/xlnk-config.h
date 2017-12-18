#ifndef XLNK_CONFIG_H
#define XLNK_CONFIG_H

#include "xlnk-sysdef.h"

enum xlnk_config_dma {
	xlnk_config_dma_manual,
	xlnk_config_dma_standard,
	xlnk_config_dma_size,
};

enum xlnk_config_valid {
	xlnk_config_valid_dma_type = 0,

	xlnk_config_valid_size = 1,
};

struct __attribute__ ((__packed__)) xlnk_config_block
{
	xlnk_byte_type valid_mask[xlnk_config_valid_size];

	xlnk_enum_type dma_type;
};

void xlnk_config_clear_block(struct xlnk_config_block *config_block);
void xlnk_init_config(void);
int xlnk_set_config(const struct xlnk_config_block *config_block);
void xlnk_get_config(struct xlnk_config_block *config_block);
int xlnk_config_dma_type(enum xlnk_config_dma type);

#endif
