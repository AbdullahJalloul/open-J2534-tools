
#include "stypes.h"
#include "params.h"

//struct isoparams : timings (j2534 fig. 30, p.43+); lock while updating ; move to a "params" module ?
//note : P* need to be scaled, see SET_CONFIG ioctl
struct isoparams_t isoparams = {
	.k_only = 1,
	.DATA_RATE = 10400,
	.LOOPBACK = 0,
	.PARITY = 0,	//N
	.b8 = 1,		//8
	.FIVE_BAUD_MOD = 0,	//?
	.p1max=40/2,	//(ms) max interbyte for ECU resps
	.p3min=110/2,	//(ms) min time between end of ECU resp(s) and new tester req
	.p4min=10/2,	//(ms) min interbyte for tester reqs
	.w0=300,		//(ms) bus idle before slowinit
	.w1=300,		//(ms) tmax pre syncpattern
	.w2=20,		//(ms) tmax pre KB1
	.w3=20,		//(ms) tmax KB1->KB2
	.w4=50,		//(ms) tmax KB2->~KB2 (wait before txing ~kb2). questionable default; 9141 says W4=[25,50]
	.w5=300,		//(ms) idle pre slowinit
	.tidle=300,	//(ms) bus idle pre fastinit
	.tinil=25,	//(ms) duration of TX_break of WUP
	.twup=50	//(ms) total duration of WUP
};
