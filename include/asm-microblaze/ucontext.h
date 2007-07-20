#ifndef __MICROBLAZE_UCONTEXT_H__
#define __MICROBLAZE_UCONTEXT_H__

#include <asm/sigcontext.h>

struct ucontext {
	unsigned long	  uc_flags;
	struct ucontext  *uc_link;
	stack_t		  uc_stack;
	struct sigcontext uc_mcontext;
	sigset_t	  uc_sigmask;	/* mask last for extensibility */
};

#endif /* __MICROBLAZE_UCONTEXT_H__ */
