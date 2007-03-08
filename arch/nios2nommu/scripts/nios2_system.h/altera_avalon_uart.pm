package altera_avalon_uart;

use base qw(BasicModule);
use strict;

sub required_module_names {
	("uart0", "uart1", "uart2", "uart3")
}

sub required_class_name {
	"altera_avalon_uart";
}

sub base_address_cast {
	"np_uart"
}

sub print_prefix {
	my ($class, $system) = @_;

	print "#ifndef __ASSEMBLY__\n";
	print "#include <asm/uart_struct.h>\n";
	print "#endif\n\n";
}	

sub translate {
	my $class = shift;
	my ($system, $required_module_name, $module_name) = @_;

	$class->SUPER::translate (@_);

	if (!defined ($altera_avalon_uart::default_uart)) {
		print "/* The default uart is always the first one found in the PTF file */\n";
		print "#define nasys_printf_uart na_$required_module_name\n\n";
		$altera_avalon_uart::default_uart = $required_module_name;
	}

}

sub run {
	altera_avalon_uart->run2 (@_);
}

1;
