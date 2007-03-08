package altera_avalon_timer;

use base qw(BasicModule);
use strict;

sub required_class_name {
	"altera_avalon_timer";
}

sub required_module_names {
	"timer0"
}

sub print_prefix {
	my ($class, $system) = @_;

	print "\n";
	print "#ifndef __ASSEMBLY__\n";
	print "#include <asm/timer_struct.h>\n";
	print "#endif\n";
	print "\n";
}

sub base_address_cast {
	"np_timer"
}

# only timers with a non-fixed-period are valid
sub is_module_valid {
	my ($class, $system, $module_name) = @_;
	
	my $module = $system->getModule ($module_name);
	my $fixed_period = $module->getWSAAssignment ('fixed_period');	

	if ($fixed_period eq '0') {
		return 1;
	} else {
		return 0;
	}
}

sub run {
	altera_avalon_timer->run2 (@_);
}

1;
