package opencores_i2c;

use base qw(BasicModule);
use strict;

sub required_module_names {
	("i2c_0", "i2c_1")
}

sub required_class_name {
	"opencores_i2c";
}

sub run {
	opencores_i2c->run2 (@_);
}

1;
