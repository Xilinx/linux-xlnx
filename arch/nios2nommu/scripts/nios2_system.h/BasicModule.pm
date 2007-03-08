package BasicModule;

require PTF::SystemPTF;
require PTF::SystemPTF::Module;
use strict;

# Description: Prints an error message to stdout.  This should prefix each line
#              with "#error " so that it can be properly read by the C 
#              pre-processor.
# Args: $module_name: name of module that was is required by driver
#       $class_name: name of device class that module belongs to.
sub print_error_name_used {
	my ($class, $module_name, $class_name) = @_;

	print "#error The kernel requires that the $class->required_class_name device be named as $module_name.\n";
	print "#error The current hardware has $module_name defined as a(n) $class_name device.\n";
	print "#error This will cause the kernel to fail.\n";
	print "#error Please rename the current $module_name device to something else in SOPC Builder.\n";
}

# Description: This casts the base address to a specific data type
#              By default, it does not cast the base address to
#              anything.
sub base_address_cast {
	my ($class, $port_name) = @_;
	return;
}

# Description: This sub-routine prints out a prefix that is shown only once
#              before any translations take place.
sub print_prefix {
	my ($class, $system) = @_;
	printf ("\n");
}

# Description: Prints a set of lines to stdout that re-defines all symbols
#              related to $module_name to the symbols required by the driver.
#              Typically starts off with "#undefine ..." statements followed
#              by one or more "#define statements".
# Args: $required_module_name: the module name that's expected by the kernel.
#       $module_name: the name of the module that was found in the PTF file.
sub translate {
	my ($class, $system, $required_module_name, $module_name) = @_;
	
	# get the necessary info about the module
	my $module = $system->getModule ($module_name);
	my @port_names = $module->getPorts ();
	
	my $boolean_base_address_cast = 0;
	if (scalar (@port_names) > 1) {
		foreach my $port_name (@port_names) {
			my $cast = $class->base_address_cast ($port_name);
			if (defined ($cast)) {
				$boolean_base_address_cast = 1;
				last;
			}
		}
	} else {
		my $cast = $class->base_address_cast;
		if (defined ($cast)) {
			$boolean_base_address_cast = 1;
		}
	}

	if ($module_name eq $required_module_name && 
			!$boolean_base_address_cast) {
		printf ("/* No translation necessary for $module_name */\n\n");
		return;
	}

	# undefine the original entries
	print "/* Redefining $module_name -> $required_module_name */\n";
	if (scalar (@port_names) == 1) {
		my $irq = $module->getIRQ ();
		print "#undef na_" . $module_name . "\n";
		if (defined ($irq)) {
			print "#undef na_" . $module_name . "_irq\n";
		}
		print "\n";
	} else {
		foreach my $port_name (@port_names) {
			print "#undef na_" . $module_name . "_" . 
				$port_name . "\n";
			my $irq = $module->getIRQ ($port_name);
			if (defined ($irq)) {
				print "#undef na_" . $module_name . "_" .
					$port_name . "_irq\n";
			}
			print "\n";
		}
	}
	
	if (scalar (@port_names) == 1) {
		# set up a string to pass to printf that will output the correct
		# #define base address statement.

		# turn on the high bit for the base address to bypass cache.
		my $base_address = $module->getBaseAddress ();
		$base_address = hex ($base_address) | 0x80000000;
	
		my $cast = $class->base_address_cast;
		$class->print_define_line ($required_module_name, 
			undef, "addr", $cast, $base_address);
	
		# print out an IRQ define statement if necessary
		my $irq = $module->getIRQ ();
		if (defined ($irq)) {
			$class->print_define_line ($required_module_name, 
				undef, "irq", undef, $irq);
		}
		printf ("\n");
	} else {
		foreach my $port_name (@port_names) {
			my $cast = $class->base_address_cast ($port_name);
			my $base_address = $module->getBaseAddress ($port_name);
			$base_address = hex ($base_address) | 0x80000000;
			$class->print_define_line ($required_module_name, 
				$port_name, "addr", $cast, $base_address);

			my $irq = $module->getIRQ ($port_name);
			if (defined ($irq)) {
				$class->print_define_line (
					$required_module_name, $port_name,
					"irq", undef, $irq);
			}
			
			print "\n";
		}
	}
}

# Description: The following sub-routine prints out "undef" or "define"
#              statements based on the arguments received.
# Args: $name: "define" or "undef"
#       $port: name of port (if applicable)
#       $type: "addr" or "irq"
#       $cast: data type to cast base address to (if applicable)
#       $value: value of symbol to be defined (if applicable)
sub print_define_line {
	my ($class, $name, $port, $type, $cast, $value) = @_;

	# construct the symbol that is being used
	my $symbol .= "na_";
	$symbol .= $name;

	$symbol .= defined ($port) ? "_" . $port : "";
	$symbol .= $type eq "irq" ? "_irq" : "";
	
	my $string_value;
	if ($type eq "addr") {
		$string_value = sprintf ("%#010x", $value);
		if (defined $cast) {
			$string_value = "(($cast*) $string_value)";
		}
	} else {
		$string_value = $value;
	}
	printf ("%-41s %30s\n", "#define $symbol", $string_value);
}	

# Description: This sub-routine prints out a prefix that is shown only once
#              after any translations take place.
sub print_suffix {
	my ($class, $system) = @_;
	# intentionally left empty
}
	
# Description: The following function allows the class to further determine if
#              the module is valid.  For instance, the timer class requires
#              that the selected module does not have a fixed period.
#              This function returns true by default which basically means
#              that all modules belonging to class are valid.
sub is_module_valid {
	my ($class, $system, $module_name) = @_;
	return 1;
}

# Description: This sub-routine is required.  It is executed by the
#              "../gen_nios2_system_h.pl" script whenever any devices of type
#              $class->required_class_name are found in the PTF file.
#
#              It looks for any conflicting module names first.  If any are
#              found, "print_error_name_used" is called and this perl module
#              exits.
#
#              It then goes through the list of module names found in the PTF
#              file that are of type $class->required_class_name and maps them to the
#              list of unused names in $class->required_module_names.
#
#              Finally, it will call the "translate" sub-routine to output the
#              symbols required by the driver.
# Args: $system: a variable containing a reference to the system.ptf file that
#                provides full access to any information in the file.
#       @found_module_names: a list of module names that are of type
#                            $class->required_class_name
sub run2 {
	my ($class, $system, @found_module_names) = @_;

	# initialize a mapping of required module names to actual module names
	my %module_map;
	foreach my $module_name ($class->required_module_names) {
		$module_map{$module_name} = "";
	}

	# if the required module name is already in use in the PTF file for a
	# different device class, flag it as an error. 
	my $error_found = 0;
	foreach my $module_name ($class->required_module_names) {
		my $module = $system->getModule ($module_name);

		if (!defined ($module)) {
			next;
		}
		
		my $class_name = $module->getClass ();
		if ($class_name ne $class->required_class_name) {
			$class->print_error_name_used ($class, $module_name, $class_name);
			$error_found = 1;
		}
	}

	# if errors were found, then there's no point in continuing.
	if ($error_found == 1) {
		return;
	}

	# Run through list of modules that belong to the class and start
	# mapping each module name to the first unused required module name
	# as defined above
	FOUND_MOD_LOOP: foreach my $module_name (@found_module_names) {

		# If the module name has already been used, then continue
		# to the next one.
		foreach my $required_module_name ($class->required_module_names) {
			if ($module_map{$required_module_name} eq $module_name) {
				next FOUND_MOD_LOOP;
			}
		}

		# assertion: $module_name is not mapped yet.
		foreach my $required_module_name ($class->required_module_names) {
			if ($module_map{$required_module_name} ne "") {
				next;
			}

			if ($class->is_module_valid ($system, $module_name)) {
				$module_map{$required_module_name} = $module_name;
			}
			last;
		}
	}

	$class->print_prefix ($system);

	# Now that everything's been mapped (or as close as we're going to get
	# to it being mapped), start printing out the literal translation.
	foreach my $required_module_name ($class->required_module_names) {
		my $module_name = $module_map{$required_module_name};
		if (length ($module_name) > 0) {
			$class->translate ($system, $required_module_name, $module_name);
		}
	}

	$class->print_suffix ($system);
}

1;
