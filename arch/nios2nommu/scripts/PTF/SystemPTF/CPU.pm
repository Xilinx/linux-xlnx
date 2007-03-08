package CPU;

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

sub getVersion {
	my ($self) = @_;
	
	return $self->{ptf}->getAssignment ('class_version');
}

sub getConstant {
	my ($self, $name) = @_;
	
	# get WSA
	$wsa = $self->{ptf}->getSection('WIZARD_SCRIPT_ARGUMENTS', '');
	$wsa or return;
	
	# get constants section
	$constants = $wsa->getSection('CONSTANTS', '');
	$constants or return;
	
	# get section for specific constant
	$constant = $constants->getSection ('CONSTANT', $name);
	$constant or return;
	
	# get value of constant
	$value = $constant->getAssignment ('value');
	return $value;
}

sub getWSAAssignment {
	my ($self, $name) = @_;

	# get WSA
	$wsa = $self->{ptf}->getSection('WIZARD_SCRIPT_ARGUMENTS', '');
	$wsa or return;

	# get value of WSA Assignment
	$value = $wsa->getAssignment ($name);
	return $value;
}

sub getResetLocationOffset {
	my ($self) = @_;
	
	$wsa = $self->{ptf}->getSection('WIZARD_SCRIPT_ARGUMENTS', '');
	$wsa or return;
	
	my $location = $wsa->getAssignment ('reset_slave');
	my $offset = $wsa->getAssignment ('reset_offset');
	
	return ($location, $offset);
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
