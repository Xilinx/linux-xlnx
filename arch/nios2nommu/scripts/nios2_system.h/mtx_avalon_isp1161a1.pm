package mtx_avalon_isp1161a1;

use base qw(BasicModule);
use strict;

sub required_module_names {
	"usb"
}

sub required_class_name {
	"mtx_avalon_isp1161a1";
}

sub run {
	mtx_avalon_isp1161a1->run2(@_);
}

1;
