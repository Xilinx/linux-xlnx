package PTFSection;

use strict;

# Fields:
#  type = type of PTF Section
#  name = name of PTF Section (can be blank)
#  sections = array of section references
#  assignments = hash of assignments

sub new {
	my $invocant = shift;
	my $class = ref($invocant) || $invocant;
	my $self = {
		@_,
		sections => [],
		assignments => {},
	};
	bless ($self, $class);
	return $self;
}

sub addSection {
	my ($self, $section) = @_;
	push @{$self->{sections}}, $section;
}

sub getSections {
	my ($self, $type) = @_;
	
	if (! $type) {
		return @{$self->{sections}};
	}
	
	my @matchedSections;
	foreach my $section (@{$self->{sections}}) {
		if ($section->type eq $type) {
			push @matchedSections, $section;
		}
	}
	
	return @matchedSections;
}

sub getSection {
	my ($self, $type, $name) = @_;
	
	if (! $name) {
		$name = "";
	}
	
	foreach my $section (@{$self->{sections}}) {
		if ($section->type eq $type and $section->name eq $name) {
			return $section;
		}
	}

}

sub addAssignment {
	my ($self, $name, $value) = @_;
	$self->{assignments}{$name} = $value;
}

sub getAssignment {
	my ($self, $name) = @_;
	return $self->{assignments}{$name};
}

sub type {
	my $self = shift;
	return $self->{type};
}

sub name {
	my $self = shift;
	return $self->{name};
}


1;
