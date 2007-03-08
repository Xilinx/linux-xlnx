package opencores_ethernet_mac;

use base qw(BasicModule);
use strict;

sub required_module_names {
	"igor_mac"
}

sub required_class_name {
	"opencores_ethernet_mac"
}

sub run {
	opencores_ethernet_mac->run2 (@_);
}

1;
