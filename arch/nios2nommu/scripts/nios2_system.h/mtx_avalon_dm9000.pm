package mtx_avalon_dm9000;

use base qw(BasicModule);
use strict;

sub required_module_names {
	"dm9000"
}

sub required_class_name {
	"mtx_avalon_dm9000"
}

sub run {
	mtx_avalon_dm9000->run2(@_);
}

1;
