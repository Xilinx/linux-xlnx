package altera_avalon_sysid;

use base qw(BasicModule);
use strict;

sub required_class_name {
	"altera_avalon_sysid"
}

sub required_module_names {
	"sysid"
}

sub run {
	altera_avalon_sysid->run2 (@_);
}

1;
