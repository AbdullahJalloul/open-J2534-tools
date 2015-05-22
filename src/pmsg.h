#ifndef PMSG_H
#define PMSG_H

// pmsg.h
// stuff for periodicmsgs; {txworker*, USB commands} need these

/*
7.2.7 periodic msgs : max len=12B including hdr "or CAN id"; prioritize PTwm; respect
bus idle (P3min etc), "generate txdone (15765) indications and loopback msgs if enabled)" ??.
For 15765, periodic could tx during multi-frame tx/rx.
Support >= 10 periodic msgs : ~ 10*(4B (timing+proto/flags) + 12B)
Period = [5, 65535]ms.
Techniques :
	- 3: int every X ms, check every msgtimer expiry, queue msg... prob=overhead
	- 2: sort msgs, set tmr for next due - less overhead. ==> flawed !
	-> 1: maintain "countdown" for each pmsg, int on next "most soonest"
*/

#include "stypes.h"
#include "msg.h"

// PMSG ids = 0 to (PMSG_MAXNUM -1)
#define PMSG_MAXNUM 10	//J2534-1 , 7.2.7

/* func protos */

// PMSG_IRQH : semi-low prio INT on TMRx overflow/expiry.
void PMSG_IRQH(void);

//delete or queue for deletion a pmsg; return -1 if queued, 0 if ok
//for use by USB command dispatch
int pmsg_del(uint id);

//find,claim, get info for a pmsg with proto <mprot> and is queued for TX
// rets ptr to data and sets len if success, NULL if no msg claimed.
u8 * pmsg_claim(enum msgproto mprot, uint *len, uint *pmid);

//clear BUSY flag, delete message if queued for del; always succed
void pmsg_release(uint id);

//clear TXQ flag: always succeed
void pmsg_unq(uint id);

#endif	//PMSG_H
