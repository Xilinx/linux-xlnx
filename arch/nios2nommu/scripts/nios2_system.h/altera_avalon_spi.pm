package altera_avalon_spi;

use base qw(BasicModule);
use strict;

sub required_module_names {
	"spi"
}

sub required_class_name {
	"altera_avalon_spi"
}

sub base_address_cast {
	"np_spi"
}

sub print_prefix {
	my ($class, $system) = @_;

	print "#ifndef __ASSEMBLY__\n";
	print "#include <asm/spi_struct.h>\n";
	print "#endif\n\n";
}

sub run {
	altera_avalon_spi->run2 (@_);
}

1;
