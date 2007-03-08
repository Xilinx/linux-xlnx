package Module;

use PTF::PTFSection;

sub new {
    my $invocant = shift;
    my $class = ref($invocant) || $invocant;
    my $self = {
	@_,
    };
    
    # if no ptf section was passed in, then return undef
    $self->{ptf} or return;

    bless ($self, $class);
    return $self;
}

sub getClass {
	my ($self) = @_;
	
	return $self->{ptf}->getAssignment ('class');
}

sub getPorts {
	my ($self) = @_;
	
	my @port_names;
	
	my @ports = $self->{ptf}->getSections ('SLAVE');
	foreach $port (@ports) {
		push @port_names, $port->name;
	}

	return @port_names;
}

sub getPort {
	my ($self, $port_name) = @_;
	
	my $port;

	if (! $port_name) {
		# use first port found
		my @port_names = $self->getPorts ();
		$port = $self->{ptf}->getSection ('SLAVE', $port_names[0]);
	} else {
		$port = $self->{ptf}->getSection ('SLAVE', $port_name);
		if (! $port) {
			# return undef if the PTF section doesn't exist
			return;
		}
	}
	
	return $port;
}

sub getWSAAssignment {
	my ($self, $assignment) = @_;
	
	my $WSA = $self->{ptf}->getSection ('WIZARD_SCRIPT_ARGUMENTS', '');
	if (! $WSA) {
		# return undef if the WSA section doesn't exist.
		return;
	}
	
	my $result = $WSA->getAssignment ($assignment);
	
	return $result;
	
}

sub getWSAConstant {
	my ($self, $name) = @_;
	
	my $WSA = $self->{ptf}->getSection ('WIZARD_SCRIPT_ARGUMENTS', '');
	if (! $WSA) {
		# return undef if the WSA section doesn't exist.
		return;
	}
	
	my $constants = $WSA->getSection ('CONSTANTS', '');
	if (! $constants) {
		# return undef if the CONSTANTS section doesn't exist.
		return;
	}
	
	my $constant = $constants->getSection ('CONSTANT', $name);
	if (! $constant) {
		# return undef if the CONSTANT $name section doesn't exist.
		return;
	}
	
	my $result = $constant->getAssignment ('value');
	return $result;
	
}

sub isMemoryDevice {
	my ($self, $port_name) = @_;
	
	my $port = $self->getPort ($port_name);
	if (! $port) {
		# return undef if the PTF section doesn't exist
		return;
	}
	
	my $SBI = $port->getSection('SYSTEM_BUILDER_INFO', '');
	if (! $SBI) {
		# return undef if the PTF section doesn't exist
		return;
	}
	
	my $result = $SBI->getAssignment('Is_Memory_Device');
	
	return $result;
}

sub isCustomInstruction {
	my ($self, $port_name) = @_;
	
	my $port = $self->getPort ($port_name);
	if (! $port) {
		# return undef if the PTF section doesn't exist
		return;
	}
	
	my $SBI = $port->getSection('SYSTEM_BUILDER_INFO', '');
	if (! $SBI) {
		# return undef if the PTF section doesn't exist
		return;
	}
	
	my $result = $SBI->getAssignment('Is_Custom_Instruction');
	
	return $result;
}

sub getBaseAddress {
	my ($self, $port_name) = @_;
	
	my $port = $self->getPort ($port_name);
	if (! $port) {
		# return undef if the PTF section doesn't exist
		return;
	}
	
	my $SBI = $port->getSection('SYSTEM_BUILDER_INFO', '');
	if (! $SBI) {
		# return undef if the PTF section doesn't exist
		return;
	}
	
	my $result = $SBI->getAssignment('Base_Address');
	if ($result eq 'N/A') {
		return;
	}
	return $result;
}

sub getSize {
	my ($self, $port_name) = @_;
	
	my $port = $self->getPort ($port_name);
	$port or return;  #return undef if the ptf section doesn't exist
	
	my $SBI = $port->getSection ('SYSTEM_BUILDER_INFO', '');
	my $data_width = $SBI->getAssignment ('Data_Width');
	my $addr_width = $SBI->getAssignment ('Address_Width');
	
	if ($data_width == 8) {
		$size = 1 << $addr_width;
	} elsif ($data_width == 16) {
		$size = 1 << ($addr_width + 1);
	} elsif ($data_width == 32) {
		$size = 1 << ($addr_width + 2);
	} elsif ($data_width == 64) {
		$size = 1 << ($addr_width + 3);
	} elsif ($data_width == 128) {
		$size = 1 << ($addr_width + 4);
	} else {
		return;
	}
	
	$size_text = sprintf ("%#010x", $size);
	return $size_text;
}

sub getIRQ {
	my ($self, $port_name) = @_;
	
	my $port = $self->getPort ($port_name);
	if (! $port) {
		# return undef if the PTF section doesn't exist
		return;
	}
	
	my $SBI = $port->getSection('SYSTEM_BUILDER_INFO', '');
	if (! $SBI) {
		# return undef if the PTF section doesn't exist
		return;
	}
	
	my $result = $SBI->getAssignment('Has_IRQ');
	if ($result ne "1") {
		# this device has no associated IRQ
		return;
	}
	
	my @irq_masters = $SBI->getSections('IRQ_MASTER');
	return $irq_masters[0]->getAssignment('IRQ_Number');
}

sub getMasters {
	my ($self, $type) = @_;
	my %masters = ();
	
	# get list of all slave for device
	my @slaves = $self->{ptf}->getSections ('SLAVE');
	
	# get list of masters of relevant type for all slaves
	foreach my $slave (@slaves) {
		# get SBI for slave
		my $SBI = $slave->getSection ('SYSTEM_BUILDER_INFO', '');
		
		# get list of all MASTERED_BY and IRQ_MASTER sections
		my @mastered_bys = $SBI->getSections ('MASTERED_BY');
		my @irq_masters = $SBI->getSections ('IRQ_MASTER');
		
		# start adding masters to the list
		foreach my $master (@mastered_bys, @irq_masters) {
			my $section_name = $master->name;
			$section_name =~ /(.*)\/(.*)/;
			my $master_name = $1;
			my $master_type = $2;

			if (! $type) {
				$masters{$master_name} = ();
			} else {
				if ($master_type eq $type) {
					$masters{$master_name} = ();
				}
			}
			
		}
		
	}
	
	return keys (%masters);
}

sub isEnabled {
	my ($self) = @_;
	
	$sbi = $self->{ptf}->getSection('SYSTEM_BUILDER_INFO', '');
	$sbi or return;
	
	my $enabled = $sbi->getAssignment ('Is_Enabled');
	if ($enabled eq "1") {
		return 1;
	} else {
		return 0;
	}
}

1;

