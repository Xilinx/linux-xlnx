#include "xlnk-config.h"

struct xlnk_config_block system_config;

void xlnk_config_clear_block(struct xlnk_config_block *config_block)
{
	int i;

	for (i = 0; i < xlnk_config_valid_size; i++)
		config_block->valid_mask[i] = 0;
}

void xlnk_init_config(void)
{
	int i;

	system_config.dma_type = xlnk_config_dma_manual;

	for (i = 0; i < xlnk_config_valid_size; i++)
		system_config.valid_mask[i] = 1;
}

int xlnk_set_config(const struct xlnk_config_block *config_block)
{
#define XLNK_CONFIG_HELP(x) \
	if (config_block->valid_mask[xlnk_config_valid_##x]) \
	system_config.x = config_block->x

	XLNK_CONFIG_HELP(dma_type);

#undef XLNK_CONFIG_HELP
	return 0;
}

void xlnk_get_config(struct xlnk_config_block *config_block)
{
	*config_block = system_config;
}

int xlnk_config_dma_type(enum xlnk_config_dma type)
{
	if (system_config.dma_type == type)
		return 1;
	return 0;
}
