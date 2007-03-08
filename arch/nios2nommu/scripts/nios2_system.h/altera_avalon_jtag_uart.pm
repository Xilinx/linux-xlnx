package altera_avalon_jtag_uart;

use base qw(BasicModule);
use strict;

sub required_module_names {
	("jtag_uart")
}

sub required_class_name {
	"altera_avalon_jtag_uart";
}

sub run {
	altera_avalon_jtag_uart->run2 (@_);
}

1;
