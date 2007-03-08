package altera_avalon_cf;

use base qw(BasicModule);
use strict;

sub required_module_names {
	"ide"
}

sub required_class_name {
	"altera_avalon_cf"
}

sub run {
	altera_avalon_cf->run2(@_);
}

1;
