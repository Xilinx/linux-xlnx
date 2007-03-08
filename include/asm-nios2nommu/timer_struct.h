
// ----------------------------------------------
// Timer Peripheral

// Timer Registers
typedef volatile struct
	{
	int np_timerstatus;  // read only, 2 bits (any write to clear TO)
	int np_timercontrol; // write/readable, 4 bits
	int np_timerperiodl; // write/readable, 16 bits
	int np_timerperiodh; // write/readable, 16 bits
	int np_timersnapl;   // read only, 16 bits
	int np_timersnaph;   // read only, 16 bits
	} np_timer;

// Timer Register Bits
enum
	{
	np_timerstatus_run_bit    = 1, // timer is running
	np_timerstatus_to_bit     = 0, // timer has timed out

	np_timercontrol_stop_bit  = 3, // stop the timer
	np_timercontrol_start_bit = 2, // start the timer
	np_timercontrol_cont_bit  = 1, // continous mode
	np_timercontrol_ito_bit   = 0, // enable time out interrupt

	np_timerstatus_run_mask    = (1<<1), // timer is running
	np_timerstatus_to_mask     = (1<<0), // timer has timed out

	np_timercontrol_stop_mask  = (1<<3), // stop the timer
	np_timercontrol_start_mask = (1<<2), // start the timer
	np_timercontrol_cont_mask  = (1<<1), // continous mode
	np_timercontrol_ito_mask   = (1<<0)  // enable time out interrupt
	};

// Timer Routines
int nr_timer_milliseconds(void);	// Starts on first call, hogs timer1.

