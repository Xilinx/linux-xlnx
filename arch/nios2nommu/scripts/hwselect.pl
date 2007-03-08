# This script generates arch/nios2nommu/hardware.mk based on user input

# usage:
#
#  [SOPC Builder]$ perl hwselect.pl <ptf file path> <target file path>
#

use PTF::SystemPTF;
use strict;
use integer;

my $ptf_filename;
my $target_filename;
my $index;
my $system;

#
# Subroutine: Prompt user for an answer
#

sub request_answer {
	my ($min, $max) = @_;
	my $answer;

	do {
		print "Selection: ";
		$answer = <STDIN>;
		if (! ($answer >= $min && $answer <= $max)) {
			print "Invalid response, please try again.\n";
		}
	} until $answer >= $min && $answer <= $max;

	return $answer;
}

#
# Check for correct number of args
#

if (scalar (@ARGV) != 2) {
	print STDERR "ERROR: Invalid number of parameters.\n";
	exit;
} else {
	$ptf_filename = $ARGV[0];
	$target_filename = $ARGV[1];
}

#
# Check to see if the specified file exists
#

if (! -e $ptf_filename) {
	print STDERR "ERROR: Could not open SYSTEM ptf file.\n";
	exit;
}

#
# startup the parser.
#
$system = SystemPTF->new (filename => $ptf_filename);
if (!$system) {
	print STDERR "ERROR: Specified file is not a SYSTEM ptf file.\n";
	exit;
}

#
# Grab listing of Nios II processors and force user to select one:
# 

print "\n--- Please select which CPU you wish to build the kernel against:\n\n";

my @cpulist = $system->getCPUList ('altera_nios2');
my %cpuinfo;

$index = 1;
foreach my $cpu (@cpulist) {
	my $cpu_module = $system->getCPU ($cpu);
	if ($cpu_module->isEnabled ()) {
		my $class = $cpu_module->getClass();
		my $type = $cpu_module->getWSAAssignment('cpu_selection');
		my $version = $cpu_module->getVersion();

		print "($index) $cpu - Class: $class Type: $type Version: $version\n";
	}
	$index += 1;
}

print "\n";

my $cpu_selection = $cpulist[request_answer (1, $index - 1) - 1];

#
# Grab list of memory devices that $cpu_selection is hooked up to:
#
my @modulelist = $system->getSlaveModules ($cpu_selection);
my %cfiinfo;
my %meminfo;
foreach my $module_name (@modulelist) {
	my $module = $system->getModule ($module_name);
	my $class = $module->getClass ();

	if ($module->isEnabled ()) {	
		if ($class eq 'altera_avalon_cfi_flash') {
			$cfiinfo{$module_name}{class} = $class;
			$cfiinfo{$module_name}{size} = $module->getSize();
		} 

		if ($module->isMemoryDevice()) {
			$meminfo{$module_name}{class} = $class;
			$meminfo{$module_name}{size} = $module->getSize();
		}
	}
}

#
# Select an upload device:
#
print "\n--- Please select a device to upload the kernel to:\n\n";

$index = 1;
foreach my $name (keys (%cfiinfo)) {
	my $size = hex ($cfiinfo{$name}{size});
	print "($index) $name\n\tClass: $cfiinfo{$name}{class}\n\tSize: $size bytes\n\n";
	$index += 1;
}

my @cfilist = keys (%cfiinfo);
my $cfi_selected = $cfilist[request_answer (1, $index - 1) - 1];

delete $meminfo{$cfi_selected};

#
# Select program memory to execute kernel from:
# 
print "\n--- Please select a device to execute kernel from:\n\n";

$index = 1;
foreach my $name (keys (%meminfo)) {
	my $size = hex ($meminfo{$name}{size});
	print "($index) $name\n\tClass: $meminfo{$name}{class}\n\tSize: $size bytes\n\n";
	$index += 1;
}

my @memlist = keys (%meminfo);
my $mem_selected = $memlist[request_answer (1, $index - 1) - 1];

print "\n--- Summary using\n\n";
print "PTF: $ptf_filename\n";
print "CPU:                            $cpu_selection\n";
print "Device to upload to:            $cfi_selected\n";
print "Program memory to execute from: $mem_selected\n";

#
# Write settings out to Makefile fragment
#
open (HWMK, ">$target_filename") ||
	die "Could not write to $target_filename";

print HWMK "SYSPTF = $ptf_filename\n";
print HWMK "CPU = $cpu_selection\n";
print HWMK "UPLMEM = $cfi_selected\n";
print HWMK "EXEMEM = $mem_selected\n";

close (HWMK);

print "\n--- Settings written to $target_filename\n\n";