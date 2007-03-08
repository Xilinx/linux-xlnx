package mtip_avalon_10_100_mac;

use base qw(BasicModule);
use strict;

sub required_class_name {
	"mtip_avalon_10_100_mac";
}

sub required_module_names {
	"mtip_mac"
}

sub run {
	mtip_avalon_10_100_mac->run2 (@_);
}

1;
