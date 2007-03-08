package SystemPTF;

use strict;

use PTF::PTFParser;
use PTF::PTFSection;
use PTF::SystemPTF::CPU;
use PTF::SystemPTF::Board;
use PTF::SystemPTF::Module;

# Fields:

my %module_order;

sub new {
	my $invocant = shift;
	my $class = ref($invocant) || $invocant;
	my $self = {
		filename => "",
		@_,
	};
	
	my $parser = PTFParser->new;
	$self->{root} = $parser->readPTF($self->{filename});

	# if the specified PTF file could not be read properly, return undef;
	$self->{root} or return;
	
	# if the specified PTF file is not a SYSTEM, return undef.
	if ($self->{root}->type ne 'SYSTEM') {
		return;
	}
	
	# initialize the modulemap
	my @modules = $self->{root}->getSections ("MODULE");
	my $index = 0;
	foreach my $module (@modules) {
		# if the module is not enabled then do not add
		my $SBI = $module->getSection ('SYSTEM_BUILDER_INFO', '');
		if ($SBI->getAssignment ('Is_Enabled') eq "1") {
			$self->{modules}->{$module->name} = $module;
			$module_order{$module->name} = $index;
			$index += 1;
		}
	}

	bless ($self, $class);
	return $self;
}

sub getName {
	my ($self) = @_;
	return $self->{root}->name;
}

sub getCPUList {
	my ($self, @classes) = @_;
	my @cpulist = ();
	
	foreach my $module_name (keys (%{$self->{modules}})) {
		my $module = $self->{modules}->{$module_name};
		my $module_class = $module->getAssignment ('class');
		foreach my $class (@classes) {
			if ($module_class eq $class) {
				push @cpulist, $module->name;
			}
		}
	}
	
	return @cpulist;
}

sub getCPU {
	my ($self, $name) = @_;
	
	my $cpu = CPU->new (ptf => $self->{modules}->{$name});
}

sub getModule {
	my ($self, $name) = @_;
	
	my $module = Module->new (ptf => $self->{modules}->{$name});
}

sub getSlaveModules {
	my ($self, $master, $type) = @_;
	
	# create %connected set with just the master
	# value of hash key is inconsequential
	my %connected;
	$connected{$master} = ( );
	
	# create %pool set with all modules
	# value of hash key is inconsequential
	my %pool;
	@pool{keys (%{$self->{modules}})} = ( );
	
	my $dirty = 1;
	while ($dirty) {
		# %pool = difference (%pool, %connected)
		delete @pool{ keys %connected };
		
		$dirty = 0;
		
		foreach my $name (keys %pool) {
			my $mod = $self->getModule ($name);
			my %mod_masters;
			@mod_masters{ $mod->getMasters ($type) } = ( );
			
			# if intersection (%masters, %connected) is not empty
			delete @mod_masters{
				grep ( ! exists $connected{ $_ },
					keys %mod_masters) };
			
			if (scalar(keys(%mod_masters)) > 0) {
				$connected{$name} = ( );
				$dirty = 1;
			}
		}
	}
	
	delete $connected{$master};

	return sort module_comparison keys (%connected); 
}

sub getClockFreq () {
	my ($self) = @_;
	
	my $wsa = $self->{root}->getSection ('WIZARD_SCRIPT_ARGUMENTS', '');
	$wsa or return;
	
	my $result = $wsa->getAssignment ('clock_freq');
	return $result;
}

# This is not really a class method... more of a helper function really...
sub module_comparison {
	if ($module_order{$a} > $module_order{$b}) {
		return 1;
	} elsif ($module_order{$a} < $module_order{$b}) {
		return -1;
	} else {
		return 0;
	}
}


1;
