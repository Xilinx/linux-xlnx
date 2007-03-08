####################################################################
#
#    This file was generated using Parse::Yapp version 1.05.
#
#        Don't edit this file, use source file instead.
#
#             ANY CHANGE MADE HERE WILL BE LOST !
#
####################################################################
package PTFParser;
use vars qw ( @ISA );
use strict;

@ISA= qw ( Parse::Yapp::Driver );
#Included Parse/Yapp/Driver.pm file----------------------------------------
{
#
# Module Parse::Yapp::Driver
#
# This module is part of the Parse::Yapp package available on your
# nearest CPAN
#
# Any use of this module in a standalone parser make the included
# text under the same copyright as the Parse::Yapp module itself.
#
# This notice should remain unchanged.
#
# (c) Copyright 1998-2001 Francois Desarmenien, all rights reserved.
# (see the pod text in Parse::Yapp module for use and distribution rights)
#

package Parse::Yapp::Driver;

require 5.004;

use strict;

use vars qw ( $VERSION $COMPATIBLE $FILENAME );

$VERSION = '1.05';
$COMPATIBLE = '0.07';
$FILENAME=__FILE__;

use Carp;

#Known parameters, all starting with YY (leading YY will be discarded)
my(%params)=(YYLEX => 'CODE', 'YYERROR' => 'CODE', YYVERSION => '',
			 YYRULES => 'ARRAY', YYSTATES => 'ARRAY', YYDEBUG => '');
#Mandatory parameters
my(@params)=('LEX','RULES','STATES');

sub new {
    my($class)=shift;
	my($errst,$nberr,$token,$value,$check,$dotpos);
    my($self)={ ERROR => \&_Error,
				ERRST => \$errst,
                NBERR => \$nberr,
				TOKEN => \$token,
				VALUE => \$value,
				DOTPOS => \$dotpos,
				STACK => [],
				DEBUG => 0,
				CHECK => \$check };

	_CheckParams( [], \%params, \@_, $self );

		exists($$self{VERSION})
	and	$$self{VERSION} < $COMPATIBLE
	and	croak "Yapp driver version $VERSION ".
			  "incompatible with version $$self{VERSION}:\n".
			  "Please recompile parser module.";

        ref($class)
    and $class=ref($class);

    bless($self,$class);
}

sub YYParse {
    my($self)=shift;
    my($retval);

	_CheckParams( \@params, \%params, \@_, $self );

	if($$self{DEBUG}) {
		_DBLoad();
		$retval = eval '$self->_DBParse()';#Do not create stab entry on compile
        $@ and die $@;
	}
	else {
		$retval = $self->_Parse();
	}
    $retval
}

sub YYData {
	my($self)=shift;

		exists($$self{USER})
	or	$$self{USER}={};

	$$self{USER};
	
}

sub YYErrok {
	my($self)=shift;

	${$$self{ERRST}}=0;
    undef;
}

sub YYNberr {
	my($self)=shift;

	${$$self{NBERR}};
}

sub YYRecovering {
	my($self)=shift;

	${$$self{ERRST}} != 0;
}

sub YYAbort {
	my($self)=shift;

	${$$self{CHECK}}='ABORT';
    undef;
}

sub YYAccept {
	my($self)=shift;

	${$$self{CHECK}}='ACCEPT';
    undef;
}

sub YYError {
	my($self)=shift;

	${$$self{CHECK}}='ERROR';
    undef;
}

sub YYSemval {
	my($self)=shift;
	my($index)= $_[0] - ${$$self{DOTPOS}} - 1;

		$index < 0
	and	-$index <= @{$$self{STACK}}
	and	return $$self{STACK}[$index][1];

	undef;	#Invalid index
}

sub YYCurtok {
	my($self)=shift;

        @_
    and ${$$self{TOKEN}}=$_[0];
    ${$$self{TOKEN}};
}

sub YYCurval {
	my($self)=shift;

        @_
    and ${$$self{VALUE}}=$_[0];
    ${$$self{VALUE}};
}

sub YYExpect {
    my($self)=shift;

    keys %{$self->{STATES}[$self->{STACK}[-1][0]]{ACTIONS}}
}

sub YYLexer {
    my($self)=shift;

	$$self{LEX};
}


#################
# Private stuff #
#################


sub _CheckParams {
	my($mandatory,$checklist,$inarray,$outhash)=@_;
	my($prm,$value);
	my($prmlst)={};

	while(($prm,$value)=splice(@$inarray,0,2)) {
        $prm=uc($prm);
			exists($$checklist{$prm})
		or	croak("Unknow parameter '$prm'");
			ref($value) eq $$checklist{$prm}
		or	croak("Invalid value for parameter '$prm'");
        $prm=unpack('@2A*',$prm);
		$$outhash{$prm}=$value;
	}
	for (@$mandatory) {
			exists($$outhash{$_})
		or	croak("Missing mandatory parameter '".lc($_)."'");
	}
}

sub _Error {
	print "Parse error.\n";
}

sub _DBLoad {
	{
		no strict 'refs';

			exists(${__PACKAGE__.'::'}{_DBParse})#Already loaded ?
		and	return;
	}
	my($fname)=__FILE__;
	my(@drv);
	open(DRV,"<$fname") or die "Report this as a BUG: Cannot open $fname";
	while(<DRV>) {
                	/^\s*sub\s+_Parse\s*{\s*$/ .. /^\s*}\s*#\s*_Parse\s*$/
        	and     do {
                	s/^#DBG>//;
                	push(@drv,$_);
        	}
	}
	close(DRV);

	$drv[0]=~s/_P/_DBP/;
	eval join('',@drv);
}

#Note that for loading debugging version of the driver,
#this file will be parsed from 'sub _Parse' up to '}#_Parse' inclusive.
#So, DO NOT remove comment at end of sub !!!
sub _Parse {
    my($self)=shift;

	my($rules,$states,$lex,$error)
     = @$self{ 'RULES', 'STATES', 'LEX', 'ERROR' };
	my($errstatus,$nberror,$token,$value,$stack,$check,$dotpos)
     = @$self{ 'ERRST', 'NBERR', 'TOKEN', 'VALUE', 'STACK', 'CHECK', 'DOTPOS' };

#DBG>	my($debug)=$$self{DEBUG};
#DBG>	my($dbgerror)=0;

#DBG>	my($ShowCurToken) = sub {
#DBG>		my($tok)='>';
#DBG>		for (split('',$$token)) {
#DBG>			$tok.=		(ord($_) < 32 or ord($_) > 126)
#DBG>					?	sprintf('<%02X>',ord($_))
#DBG>					:	$_;
#DBG>		}
#DBG>		$tok.='<';
#DBG>	};

	$$errstatus=0;
	$$nberror=0;
	($$token,$$value)=(undef,undef);
	@$stack=( [ 0, undef ] );
	$$check='';

    while(1) {
        my($actions,$act,$stateno);

        $stateno=$$stack[-1][0];
        $actions=$$states[$stateno];

#DBG>	print STDERR ('-' x 40),"\n";
#DBG>		$debug & 0x2
#DBG>	and	print STDERR "In state $stateno:\n";
#DBG>		$debug & 0x08
#DBG>	and	print STDERR "Stack:[".
#DBG>					 join(',',map { $$_[0] } @$stack).
#DBG>					 "]\n";


        if  (exists($$actions{ACTIONS})) {

				defined($$token)
            or	do {
				($$token,$$value)=&$lex($self);
#DBG>				$debug & 0x01
#DBG>			and	print STDERR "Need token. Got ".&$ShowCurToken."\n";
			};

            $act=   exists($$actions{ACTIONS}{$$token})
                    ?   $$actions{ACTIONS}{$$token}
                    :   exists($$actions{DEFAULT})
                        ?   $$actions{DEFAULT}
                        :   undef;
        }
        else {
            $act=$$actions{DEFAULT};
#DBG>			$debug & 0x01
#DBG>		and	print STDERR "Don't need token.\n";
        }

            defined($act)
        and do {

                $act > 0
            and do {        #shift

#DBG>				$debug & 0x04
#DBG>			and	print STDERR "Shift and go to state $act.\n";

					$$errstatus
				and	do {
					--$$errstatus;

#DBG>					$debug & 0x10
#DBG>				and	$dbgerror
#DBG>				and	$$errstatus == 0
#DBG>				and	do {
#DBG>					print STDERR "**End of Error recovery.\n";
#DBG>					$dbgerror=0;
#DBG>				};
				};


                push(@$stack,[ $act, $$value ]);

					$$token ne ''	#Don't eat the eof
				and	$$token=$$value=undef;
                next;
            };

            #reduce
            my($lhs,$len,$code,@sempar,$semval);
            ($lhs,$len,$code)=@{$$rules[-$act]};

#DBG>			$debug & 0x04
#DBG>		and	$act
#DBG>		and	print STDERR "Reduce using rule ".-$act." ($lhs,$len): ";

                $act
            or  $self->YYAccept();

            $$dotpos=$len;

                unpack('A1',$lhs) eq '@'    #In line rule
            and do {
                    $lhs =~ /^\@[0-9]+\-([0-9]+)$/
                or  die "In line rule name '$lhs' ill formed: ".
                        "report it as a BUG.\n";
                $$dotpos = $1;
            };

            @sempar =       $$dotpos
                        ?   map { $$_[1] } @$stack[ -$$dotpos .. -1 ]
                        :   ();

            $semval = $code ? &$code( $self, @sempar )
                            : @sempar ? $sempar[0] : undef;

            splice(@$stack,-$len,$len);

                $$check eq 'ACCEPT'
            and do {

#DBG>			$debug & 0x04
#DBG>		and	print STDERR "Accept.\n";

				return($semval);
			};

                $$check eq 'ABORT'
            and	do {

#DBG>			$debug & 0x04
#DBG>		and	print STDERR "Abort.\n";

				return(undef);

			};

#DBG>			$debug & 0x04
#DBG>		and	print STDERR "Back to state $$stack[-1][0], then ";

                $$check eq 'ERROR'
            or  do {
#DBG>				$debug & 0x04
#DBG>			and	print STDERR 
#DBG>				    "go to state $$states[$$stack[-1][0]]{GOTOS}{$lhs}.\n";

#DBG>				$debug & 0x10
#DBG>			and	$dbgerror
#DBG>			and	$$errstatus == 0
#DBG>			and	do {
#DBG>				print STDERR "**End of Error recovery.\n";
#DBG>				$dbgerror=0;
#DBG>			};

			    push(@$stack,
                     [ $$states[$$stack[-1][0]]{GOTOS}{$lhs}, $semval ]);
                $$check='';
                next;
            };

#DBG>			$debug & 0x04
#DBG>		and	print STDERR "Forced Error recovery.\n";

            $$check='';

        };

        #Error
            $$errstatus
        or   do {

            $$errstatus = 1;
            &$error($self);
                $$errstatus # if 0, then YYErrok has been called
            or  next;       # so continue parsing

#DBG>			$debug & 0x10
#DBG>		and	do {
#DBG>			print STDERR "**Entering Error recovery.\n";
#DBG>			++$dbgerror;
#DBG>		};

            ++$$nberror;

        };

			$$errstatus == 3	#The next token is not valid: discard it
		and	do {
				$$token eq ''	# End of input: no hope
			and	do {
#DBG>				$debug & 0x10
#DBG>			and	print STDERR "**At eof: aborting.\n";
				return(undef);
			};

#DBG>			$debug & 0x10
#DBG>		and	print STDERR "**Dicard invalid token ".&$ShowCurToken.".\n";

			$$token=$$value=undef;
		};

        $$errstatus=3;

		while(	  @$stack
			  and (		not exists($$states[$$stack[-1][0]]{ACTIONS})
			        or  not exists($$states[$$stack[-1][0]]{ACTIONS}{error})
					or	$$states[$$stack[-1][0]]{ACTIONS}{error} <= 0)) {

#DBG>			$debug & 0x10
#DBG>		and	print STDERR "**Pop state $$stack[-1][0].\n";

			pop(@$stack);
		}

			@$stack
		or	do {

#DBG>			$debug & 0x10
#DBG>		and	print STDERR "**No state left on stack: aborting.\n";

			return(undef);
		};

		#shift the error token

#DBG>			$debug & 0x10
#DBG>		and	print STDERR "**Shift \$error token and go to state ".
#DBG>						 $$states[$$stack[-1][0]]{ACTIONS}{error}.
#DBG>						 ".\n";

		push(@$stack, [ $$states[$$stack[-1][0]]{ACTIONS}{error}, undef ]);

    }

    #never reached
	croak("Error in driver logic. Please, report it as a BUG");

}#_Parse
#DO NOT remove comment

1;

}
#End of include--------------------------------------------------


#line 1 "PTFParser.yp"
#
# Altera PTF file parser
#
# Copyright (c) 2004 Microtronix Datacom Ltd.
#

package PTFParser;

use PTF::PTFSection;

#global variables should go here.

#my $line = 0;		# for error messages
#my @sectionStack = ();	# used to keep track of ptf sections
#my $root;

my $fh;

sub new {
        my($class)=shift;
        ref($class)
    and $class=ref($class);

    my($self)=$class->SUPER::new( yyversion => '1.05',
                                  yystates =>
[
	{#State 0
		ACTIONS => {
			'IDENTIFIER' => 1
		},
		GOTOS => {
			'section' => 2,
			'section_title' => 3
		}
	},
	{#State 1
		ACTIONS => {
			'IDENTIFIER' => 4,
			'STRING_LITERAL' => 6,
			'NUMBER' => 7
		},
		DEFAULT => -3,
		GOTOS => {
			'section_name' => 5
		}
	},
	{#State 2
		ACTIONS => {
			'' => 8
		}
	},
	{#State 3
		ACTIONS => {
			"{" => 9
		}
	},
	{#State 4
		DEFAULT => -4
	},
	{#State 5
		DEFAULT => -2
	},
	{#State 6
		DEFAULT => -6
	},
	{#State 7
		DEFAULT => -5
	},
	{#State 8
		DEFAULT => 0
	},
	{#State 9
		ACTIONS => {
			'IDENTIFIER' => 11,
			'HIERARCHICAL_NAME' => 13
		},
		DEFAULT => -7,
		GOTOS => {
			'assignment_name' => 10,
			'assignment' => 12,
			'section_element' => 14,
			'section' => 15,
			'section_title' => 3
		}
	},
	{#State 10
		ACTIONS => {
			"=" => 16
		}
	},
	{#State 11
		ACTIONS => {
			'IDENTIFIER' => 4,
			'STRING_LITERAL' => 6,
			'NUMBER' => 7,
			"=" => -11
		},
		DEFAULT => -3,
		GOTOS => {
			'section_name' => 5
		}
	},
	{#State 12
		ACTIONS => {
			'IDENTIFIER' => 11,
			'HIERARCHICAL_NAME' => 13
		},
		DEFAULT => -7,
		GOTOS => {
			'assignment_name' => 10,
			'assignment' => 12,
			'section_element' => 17,
			'section' => 15,
			'section_title' => 3
		}
	},
	{#State 13
		DEFAULT => -12
	},
	{#State 14
		ACTIONS => {
			"}" => 18
		}
	},
	{#State 15
		ACTIONS => {
			'IDENTIFIER' => 11,
			'HIERARCHICAL_NAME' => 13
		},
		DEFAULT => -7,
		GOTOS => {
			'assignment_name' => 10,
			'assignment' => 12,
			'section_element' => 19,
			'section' => 15,
			'section_title' => 3
		}
	},
	{#State 16
		ACTIONS => {
			'STRING_LITERAL' => 20,
			'NUMBER' => 22
		},
		GOTOS => {
			'assignment_value' => 21
		}
	},
	{#State 17
		DEFAULT => -8
	},
	{#State 18
		DEFAULT => -1
	},
	{#State 19
		DEFAULT => -9
	},
	{#State 20
		DEFAULT => -13
	},
	{#State 21
		ACTIONS => {
			";" => 23
		}
	},
	{#State 22
		DEFAULT => -14
	},
	{#State 23
		DEFAULT => -10
	}
],
                                  yyrules  =>
[
	[#Rule 0
		 '$start', 2, undef
	],
	[#Rule 1
		 'section', 4,
sub
#line 20 "PTFParser.yp"
{ 
			my $sectionStack = $_[0]->YYData->{sectionStack};
			pop @{$sectionStack}; 
		}
	],
	[#Rule 2
		 'section_title', 2,
sub
#line 26 "PTFParser.yp"
{ 
			my $section = PTFSection->new (type => $_[1], name => $_[2]); 
			my $sectionStack = $_[0]->YYData->{sectionStack};
			
			if (scalar(@{$sectionStack}) == 0) {
				$_[0]->YYData->{root} = $section;
			} else {
				my $parent = $sectionStack->[$#{$sectionStack}];
				$parent->addSection ($section);
			}

			push @{$sectionStack}, $section;
		}
	],
	[#Rule 3
		 'section_name', 0, undef
	],
	[#Rule 4
		 'section_name', 1, undef
	],
	[#Rule 5
		 'section_name', 1, undef
	],
	[#Rule 6
		 'section_name', 1, undef
	],
	[#Rule 7
		 'section_element', 0, undef
	],
	[#Rule 8
		 'section_element', 2, undef
	],
	[#Rule 9
		 'section_element', 2, undef
	],
	[#Rule 10
		 'assignment', 4,
sub
#line 52 "PTFParser.yp"
{ 
			my $sectionStack = $_[0]->YYData->{sectionStack};
			my $parent= $sectionStack->[$#{$sectionStack}]; 
			$parent->addAssignment ($_[1], $_[3]); 
		}
	],
	[#Rule 11
		 'assignment_name', 1, undef
	],
	[#Rule 12
		 'assignment_name', 1, undef
	],
	[#Rule 13
		 'assignment_value', 1, undef
	],
	[#Rule 14
		 'assignment_value', 1, undef
	]
],
                                  @_);
    bless($self,$class);
}

#line 67 "PTFParser.yp"


sub _Error {
# TODO: update this error function to be more useful
        exists $_[0]->YYData->{ERRMSG}
    and do {
        print $_[0]->YYData->{ERRMSG};
        delete $_[0]->YYData->{ERRMSG};
        return;
    };
    print "Syntax error on line $_[0]->YYData->{line}.\n";
}

sub _Lexer {
	my($parser)=shift;

	if (! $parser->YYData->{INPUT}) {
		if ($parser->YYData->{INPUT} = <$fh>) {
			$parser->YYData->{line} += 1;
		} else {
			return ('', undef);
		}
	}

	$parser->YYData->{INPUT} and
		$parser->YYData->{INPUT} =~ s/^\s*//;

	while (1) {
		
		# skip blank lines
		if ($parser->YYData->{INPUT} =~ s/^[ \t\r\n]*$//) {
			if ($parser->YYData->{INPUT} = <$fh>) {
				$parser->YYData->{line} += 1;
			} else {
				return ('', undef);
			}
			$parser->YYData->{INPUT} and
				$parser->YYData->{INPUT} =~ s/^\s*//;
			next;
		}	
		
		# Skip comments
		if ($parser->YYData->{INPUT} =~ s/^#.*//) {
			if ($parser->YYData->{INPUT} = <$fh>) {
				$parser->YYData->{line} += 1;
			} else {
				return ('', undef);
			}
			$parser->YYData->{INPUT} and
				$parser->YYData->{INPUT} =~ s/^\s*//;
			next;
		}	

		# Don't continue if the line length is 0;
		if (length $parser->YYData->{INPUT} == 0) {
			if ($parser->YYData->{INPUT} = <$fh>) {
				$parser->YYData->{line} += 1;
			} else {
				return ('', undef);
			}
			$parser->YYData->{INPUT} and
				$parser->YYData->{INPUT} =~ s/^\s*//;
			next;
		}

		# tokenize input
    		$parser->YYData->{INPUT} =~ s/^([a-zA-Z_][a-zA-Z_0-9\/]*)//
			and return('IDENTIFIER',$1);
		$parser->YYData->{INPUT} =~ s/^"([^"\\]*(\\.[^"\\]*)*)"//
			and return('STRING_LITERAL',$1);
		$parser->YYData->{INPUT} =~ s/^"([^"\\]*(\\.[^"\\]*)*)//
			and do {
				my $literal = $1;

				do {
					if ($parser->YYData->{INPUT} = <$fh>) {
						$parser->YYData->{line} += 1;
					} else {
						return ('', undef);
					}

					$parser->YYData->{INPUT} =~ s/([^"\\]*(\\.[^"\\]*)*)"//
						and do {
							$literal .= $1;
							return ('STRING_LITERAL', $literal);
						};

					$parser->YYData->{INPUT} =~ s/([^"\\]*(\\.[^"\\]*)*)// 
						and $literal .= $1;
				} while (1);
			};
		$parser->YYData->{INPUT} =~ s/^([0-9]+)//
			and return('NUMBER',$1);
		$parser->YYData->{INPUT} =~ s/^([\$]{1,2}[a-zA-Z0-9 \/_]+)//
			and return('HIERARCHICAL_NAME',$1);
		$parser->YYData->{INPUT} =~ s/^(.)//
			and return($1,$1);
	}
}

sub readPTF {
	my $self = shift;
	my $filename = shift;

	# store information for later use
	$self->YYData->{line} = 0;
	$self->YYData->{sectionStack} = [];
	undef $self->YYData->{root};
	
	if (-e $filename) {
		open (PTFFILE, $filename);
		$fh = \*PTFFILE;
	} else {
		$fh = \*STDIN;
	}
	
	$self->YYParse (
		yylex => \&_Lexer,
		yyerror => \&_Error,
		);
		
	if (-e $filename) {
		close PTFFILE;
	}

	return $self->YYData->{root};
}

1;
