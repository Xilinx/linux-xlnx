package altera_avalon_lan91c111;

require PTF::SystemPTF;
require PTF::SystemPTF::Module;
use base qw(BasicModule);
use strict;

sub required_module_names {
	"enet"
}

sub required_class_name {
	"altera_avalon_lan91c111"
}

sub translate {
	my $class = shift;
	my ($system, $required_module_name, $module_name) = @_;
	$class->SUPER::translate (@_);

	my $module = $system->getModule ($module_name);

	my $offset_keyword = "LAN91C111_REGISTERS_OFFSET";
	my $offset = $module->getWSAConstant ($offset_keyword);
	printf ("%-41s %30s\n", "#define $offset_keyword", $offset);

	my $width_keyword = "LAN91C111_DATA_BUS_WIDTH";
	my $width = $module->getWSAConstant ($width_keyword);
	printf ("%-41s %30s\n", "#define $width_keyword", $width);

	print "\n";
}

sub run {
	altera_avalon_lan91c111->run2 (@_);
}

1;
