package altera_avalon_pio;

require PTF::SystemPTF;
require PTF::SystemPTF::Module;
use strict;

sub run {
	my ($system, @pio_names) = @_;

	print "#ifndef __ASSEMBLY__\n";
	print "#include <asm/pio_struct.h>\n";
	print "#endif\n\n";
	
	foreach my $pio_name (@pio_names) {
		my $module = $system->getModule ($pio_name);

		# get all the relevant information
		my $base_address = $module->getBaseAddress ();
		$base_address = hex ($base_address) | 0x80000000;
		my $irq = $module->getIRQ ();

		print "/* Casting base addresses to the appropriate structure */\n";

		# undefine all the old symbols first
		print "#undef na_${pio_name}\n";
		if (defined ($irq)) {
			print "#undef na_${pio_name}_irq\n";
			print "\n";
		}

		# define base address
		$base_address = sprintf ("%#010x", $base_address);
		printf ("%-41s %30s\n", "#define na_${pio_name}", 
			"((np_pio*) ${base_address})");

		# define irq
		if (defined ($irq)) {
			printf ("%-41s %30s\n", "#define na_${pio_name}_irq", 
				$irq);
		}

		print "\n";
	}
}

1;
