// pmsg.c
/************ periodic msg shits ************/
//TODO : improve deletion for busy messages... maybe have caller copy data instead of claim/release?

#include <stddef.h>
#include <string.h>

#include "timers.h"
#include "pmsg.h"
#include "msg.h"
#include "stypes.h"
#include "utils.h"

//access to ->flags NEEDS to be atomic
struct pmsg_desc {
	u32 flags;
		#define PMSG_ENABLED	(1<<31)	//data valid & sendable. set by pmsg_add(id), cleared by pmsg_del(id)
		#define PMSG_NEW	(1<<30)	//freshly added (set in pmsg_add; cleared in pmsg_work)
		#define PMSG_TXQ	(1<<26)	//msg is queued for TX ( pmsg_unq() after tx)
		#define PMSG_DELQ	(1<<25)	//msg is queued for deletion (pmsg_del(id))
		#define	PMSG_BUSY	(1<<24)	//msg is in use (pmsg_claim(), pmsg_release())
		#define PMSG_PROTOSHIFT	20	//enum msgproto = (flags & PROTOMASK) >> PROTOSHIFT
		#define PMSG_PROTOMASK	(0x3 << PMSG_PROTOSHIFT)
		#define PMSG_LENSHIFT	16	//msg len = (flags & LENMASK)>>LENSHIFT
		#define PMSG_LENMASK	(0xF << PMSG_LENSHIFT)
		#if ((PMSG_LENMASK >> PMSG_LENSHIFT) < PMSG_MAX_SIZE)
			#error	bad PMSG MAXLEN !
		#endif
		#define PMSG_PERMASK	0xffff	//period (ms) = flags & PMSG_PERMASK
	u8 data[PMSG_MAX_SIZE];
};

void pmsg_work(void);

// PMSG ids = 0 to (PMSG_MAXNUM -1)
static struct pmsg_desc pmsg_table[PMSG_MAXNUM];	//all possible periodic msgs.


/*************/
/* technique 1 : maintain countdowns per PMSG, int on next soonest, and set PMSG_TXQ flag.
 *
 */

//clear pmsg table
void pmsg_init(void) {

	for (uint i=0; i< ARRAY_SIZE(pmsg_table); i++) {
		pmsg_table[i].flags = 0;
	}
	return;
}

//PMSG_IRQH : semi-low prio INT on TMRx overflow/expiry.
void PMSG_IRQH(void) {
	TIM_Cmd(PMSG_TMR, DISABLE);	//cancel in case it was a forced int
	TIM_ClearFlag(PMSG_TMR, TIM_FLAG_Update);
	NVIC_ClearPendingIRQ(IRQN_PMSG);
	pmsg_work();
	return;
}

//worker, called ONLY from IRQH handler (TMR expiry). Tolerates polling but not recursion
void pmsg_work(void) {
	static u16 pmsg_countdown[ARRAY_SIZE(pmsg_table)];	//countdown=0 means disabled
	static u32 last_run=0;	//frclock @ last countdown update.
	u16 delta_ms;		//ms elapsed since last countdown update
	u16 newmin;	//finding next "soonest" msg
	u32 pflags;
	u32 lock;

	delta_ms = (frclock - last_run) / frclock_conv;	//OK cast if <65536 ms !

	newmin = (u16) -1;

	lock=sys_SDI();	//yuck, but no choice
	for (uint i=0; i< ARRAY_SIZE(pmsg_table); i++) {
		pflags = pmsg_table[i].flags;
		if (((pflags & PMSG_ENABLED) == 0) ||
			(pflags & PMSG_BUSY)) continue;
		if (pflags & PMSG_NEW) {
			//this makes sure new msgs will be queued as soon as added
			pmsg_countdown[i]=0;
			pmsg_table[i].flags &= ~PMSG_NEW;
		}
		if (pmsg_countdown[i] <= delta_ms) {
			//queue + reload all due msgs
			pmsg_table[i].flags |= PMSG_TXQ;
			pmsg_countdown[i] = (u16) (pflags & PMSG_PERMASK);
		} else {
			//decrement others
			pmsg_countdown[i] -= delta_ms;
		}
		// find next soonest && enabled msgid.
		if (pmsg_countdown[i] <= newmin) {
			newmin = pmsg_countdown[i];
		}
	}	//for pmsg_table
	last_run = frclock;
	sys_RI(lock);

	pmsg_setint(newmin);	//reload + restart tmr
	return;
}

//attempt to add a msg in slot 'id'. NOT re-entrant!
//ret 0 if ok
int pmsg_add(uint id, enum msgproto mp, u16 per, uint len, u8 *data) {
	u32 pflags;

	assert((len > 0) && (mp < MP_INVALID) &&
			(len <= PMSG_MAX_SIZE) && (data != NULL));

	pflags = pmsg_table[id].flags;
	if (pflags & PMSG_ENABLED)  {
		DBGM("msgid exists", id);
		return -1;	//msgid exists !?
	}

	memcpy(pmsg_table[id].data, data, len);
	pflags = PMSG_ENABLED | PMSG_NEW | (mp << PMSG_PROTOSHIFT) |
			(len << PMSG_LENSHIFT) | per ;
	pmsg_table[id].flags = pflags;
	pmsg_setint(0);	//force reparse
	return 0;
}

//delete or queue for deletion a pmsg; return -1 if queued, 0 if ok
//for use by USB command dispatch
int pmsg_del(uint id) {
	u32 lock;
	u32 pflags;
	int rv;
	if (id >= ARRAY_SIZE(pmsg_table)) return 0;
	lock=sys_SDI();
	pflags = pmsg_table[id].flags;
	if ((pflags & PMSG_ENABLED) ==0) {
		//already disabled == deleted
		rv = 0;
	} else if (pflags & PMSG_BUSY) {
		//queue deletion and signal business
		pmsg_table[id].flags = pflags | PMSG_DELQ;
		rv = -1;
	} else {
		//enabled and not busy : delete !
		pmsg_table[id].flags = 0;
		rv = 0;
	}
	sys_RI(lock);
	return rv;
}


//clear BUSY flag, delete message if queued for del; always succeed
void pmsg_release(uint id) {
	u32 lock;
	u32 pflags;
	assert(id < ARRAY_SIZE(pmsg_table));
	lock=sys_SDI();
	pflags = pmsg_table[id].flags;
	if (pflags & PMSG_DELQ) {
		pflags = 0;
		//XXX signal delete completion ?
	}
	pflags &= ~PMSG_BUSY;
	pmsg_table[id].flags = pflags;
	sys_RI(lock);
	return;
}

//clear TXQ flag: always succeed
void pmsg_unq(uint id) {
	u32 lock;
	assert(id < ARRAY_SIZE(pmsg_table));
	lock=sys_SDI();
	pmsg_table[id].flags &= ~PMSG_TXQ;
	sys_RI(lock);
	return;
}

//find,claim, get info for a pmsg with proto <mprot> and is queued for TX
// rets ptr to data and sets len, pmid if success. ret NULL if no msg claimed.
//TODO : remove claim / release mechanism; copy data to worker's buffer instead ?
u8 * pmsg_claim(enum msgproto mprot, uint *len, uint *pmid) {
	u32 lock;
	uint id;
	u32 pflags;
	u8 * rv;

	assert((len != NULL) && (pmid != NULL));

	lock=sys_SDI();
	for (id=0; id < ARRAY_SIZE(pmsg_table); id++) {
		pflags = pmsg_table[id].flags;
		if (((pflags & PMSG_ENABLED) ==0) ||
			((pflags & PMSG_TXQ) == 0) || (pflags & PMSG_BUSY)) {
			continue;
		}
		if ( ((pflags & PMSG_PROTOMASK) >> PMSG_PROTOSHIFT) == mprot) {
			pmsg_table[id].flags = pflags | PMSG_BUSY;
			sys_RI(lock);
			rv = pmsg_table[id].data;
			*len = (pflags & PMSG_LENMASK) >> PMSG_LENSHIFT;
			*pmid = id;
			return rv;
		}
	}	//for (pmsg)
	sys_RI(lock);
	return NULL;
}
