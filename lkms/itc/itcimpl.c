#include "itcimpl.h"
#include <linux/delay.h>	    /* for msleep() */
#include <linux/jiffies.h>	    /* for time_before(), time_after() */
#include <linux/interrupt.h>	    /* for interrupt functions.. */
#include <linux/gpio.h>		    /* for gpio functions.. */

#include "ruleset.h"                /* get macros and constants */

/*#define REPORT_LOCK_STATE_CHANGES*/

DECLARE_WAIT_QUEUE_HEAD(userWaitQ); /* For user context sleeping during lock negotiations */

DEFINE_SPINLOCK(mdLock);             /* For protecting access to md */

void setState(ITCInfo * itc, ITCState newState) ; // FORWARD
void updateState(ITCInfo * itc, bool timeout) ;   // FORWARD

bool isActiveWithin(ITCInfo * itc, int jiffyCount)
{
  return time_after(itc->lastActive + jiffyCount, jiffies);
}

/* GENERATE STATE ENTRY COUNTERS */
#define RS(forState,ef,output,...) 0,
u32 timesStateEntered[STATE_COUNT] = {
#include "RULES.h"
};
#undef RS


/* GENERATE STATE NAMES */
#define RS(forState,ef,output,...) #forState,
const char * stateNames[STATE_COUNT] = { 
#include "RULES.h"
};
#undef RS

const char * getStateName(ITCState s) {
  if (s >= STATE_COUNT) return "<INVALID>";
  return stateNames[s];
 }

/* GENERATE STATE->output table */
#define RS(forState,ef,output,...) OUTPUT_VALUE_##output,
const u8 outputsForState[STATE_COUNT] = {
#include "RULES.h"
};
#undef RS

typedef ITCState ITCEntryFunction(struct itcInfo*,unsigned stateInput);

/* GENERATE ENTRY FUNCTION DECLARATIONS */
#define RSE(forState,output,...) extern ITCEntryFunction entryFunction_##forState;
#define RSN(forState,output,...) 
#include "RULES.h"
#undef RSE
#undef RSN

/* GENERATE STATE->entry function pointers */
#define RSE(forState,output,...) entryFunction_##forState,
#define RSN(forState,output,...) NULL,
ITCEntryFunction *(entryFuncsForState[STATE_COUNT]) = {
#include "RULES.h"
};
#undef RSE
#undef RSN

/* GENERATE PER-STATE RULESETS */
#define RS(forState,ef,output,...) const Rule ruleSet_##forState[] = { __VA_ARGS__ };
#include "RULES.h"
#undef RS

/* GENERATE STATE->RULESET DISPATCH TABLE */
#define RS(forState,ef,output,...) ruleSet_##forState,
const Rule *(ruleSetDispatchTable[STATE_COUNT]) = {
#include "RULES.h"
};
#undef RS

#define XX(DC,fr,p1,p2,p3,p4) {  	                           \
    { p1, GPIOF_IN|GPIOF_EXPORT_DIR_FIXED,          #DC "_IRQLK"}, \
    { p2, GPIOF_IN|GPIOF_EXPORT_DIR_FIXED,          #DC "_IGRLK"}, \
    { p3, GPIOF_OUT_INIT_LOW|GPIOF_EXPORT_DIR_FIXED,#DC "_ORQLK"}, \
    { p4, GPIOF_OUT_INIT_LOW|GPIOF_EXPORT_DIR_FIXED,#DC "_OGRLK"}, },
static struct gpio pins[DIR_COUNT][4] = { DIRDATAMACRO() };
#undef XX

ModuleData md = {
  .moduleLastActive = 0,
  .userRequestTime = 0,
  .userLockset = 0,
  .userRequestActive = 0,

#define XX(DC,fr,p1,p2,p3,p4) { .direction = DIR_##DC, .isFred = fr, .pins = pins[DIR_##DC] },
  .itcInfo = { DIRDATAMACRO() }
#undef XX
};

#define XX(DC,fr,p1,p2,p3,p4) #DC,
static const char * dirnames[DIR_COUNT] = { DIRDATAMACRO() };
#undef XX

const char * itcDirName(ITCDir d)
{
  if (d > DIR_MAX) return "(Illegal)";
  return dirnames[d];
}
  
void itcIteratorInitialize(ITCIterator * itr, ITCIteratorUseCount avguses) {
  int i;
  BUG_ON(!itr);
  BUG_ON(avguses <= 0);
  BUG_ON((avguses<<1) <= 0);
  itr->m_averageUsesPerShuffle = avguses;
  for (i = 0; i < DIR_COUNT; ++i) itr->m_indices[i] = i;
  itcIteratorShuffle(itr);
}

void itcIteratorShuffle(ITCIterator * itr) {
  ITCDir i;
  BUG_ON(!itr);
  itr->m_usesRemaining = 
    prandom_u32_max(itr->m_averageUsesPerShuffle<<1) + 1; /* max is double avg, given uniform random */

  for (i = DIR_MAX; i > 0; --i) {
    int j = prandom_u32_max(i+1); /* generates 0..DIR_MAX down to 0..1 */
    if (i != j) {
      ITCDir tmp = itr->m_indices[i];
      itr->m_indices[i] = itr->m_indices[j];
      itr->m_indices[j] = tmp;
    }
  }

  printk(KERN_DEBUG "ITC %p iterator order is %d%d%d%d%d%d for next %d uses\n",
         itr,
         itr->m_indices[0],
         itr->m_indices[1],
         itr->m_indices[2],
         itr->m_indices[3],
         itr->m_indices[4],
         itr->m_indices[5],
         itr->m_usesRemaining
         );
}

static irq_handler_t itc_irq_edge_handler(ITCInfo * itc, unsigned pin, unsigned value, unsigned int irq)
{
  itc->interruptsTaken++;
  if (unlikely(value == itc->pinStates[pin])) {
    if (pin < 2)
      itc->edgesMissed[pin]++;
  } else
    itc->pinStates[pin] = value;
  updateState(itc,false);
  return (irq_handler_t) IRQ_HANDLED;
}

#define XX(DC,fr,p1,p2,p3,p4) ZZ(DC,_IRQLK) ZZ(DC,_IGRLK)
#define ZZ(DC,suf)                                                                                  \
static irq_handler_t itc_irq_handler##DC##suf(unsigned int irq, void *dev_id, struct pt_regs *regs) \
{                                                                                                   \
  return itc_irq_edge_handler(&md.itcInfo[DIR_##DC],                                                \
                              PIN##suf,                                                             \
                              gpio_get_value(md.itcInfo[DIR_##DC].pins[PIN##suf].gpio),             \
			      irq);	                                                            \
}
DIRDATAMACRO()
#undef ZZ
#undef XX

void itcInitStructure(ITCInfo * itc)
{
  int i;

  itc->interruptsTaken = 0;
  itc->edgesMissed[0] = 0;
  itc->edgesMissed[1] = 0;
  itc->lastActive = jiffies;
  itc->lastReported = jiffies-1;

  // Init to opposite of pin states to help edge interrupts score right?
  itc->pinStates[PIN_IRQLK] = !gpio_get_value(itc->pins[PIN_IRQLK].gpio);
  itc->pinStates[PIN_IGRLK] = !gpio_get_value(itc->pins[PIN_IGRLK].gpio);

  // Set up initial state
  setState(itc,sFAILED);

  // Clear state counters after setting initial state
  for (i = 0; i < STATE_COUNT; ++i)
    itc->enteredCount[i] = 0;

  // Set up magic wait counters for decelerating connection attempts
  itc->magicWaitTimeouts = 0;
  itc->magicWaitTimeoutLimit = 1;

}

void itcInitStructures(void) {

  /////
  /// First do global (full tile) inits

  int err;
  unsigned count = ARRAY_SIZE(pins)*ARRAY_SIZE(pins[0]);

  // Init the user context iterator, with frequent shuffling
  itcIteratorInitialize(&md.userContextIterator, 25);


  printk(KERN_INFO "ITC allocating %d pins\n", count);

#if 0
  err = gpio_request_array(&pins[0][0], count);
  if (err) {
    printk(KERN_ALERT "ITC failed to allocate %d pin(s): %d\n", count, err);
  } else {
    printk(KERN_INFO "ITC allocated %d pins\n", count); 
  }
#else
  { /* Initialize gpios individually to get individual failures */
    unsigned n;
    for(n = 0; n < count; ++n) {
      struct gpio *g = &pins[0][0]+n;
      err = gpio_request_array(g, 1);
      if (err) {
        printk(KERN_INFO "ITC failed to allocate pin%3d: %d\n", g->gpio, err);
      } else {
        printk(KERN_INFO "ITC allocated pin%3d for %s\n", g->gpio, g->label); 
      }
    }
  }
#endif

  /////
  /// Now do local (per-ITC) inits
  {
    ITCDir i;
    for (i = DIR_MIN; i <= DIR_MAX; ++i) {
      BUG_ON(i != md.itcInfo[i].direction);  /* Assert we inited directions properly */
      itcInitStructure(&md.itcInfo[i]);
    }
  }

  /// Now install irq handlers for everybody

#define ZZ(DC,suf) { 				                              \
    ITCInfo * itc = &md.itcInfo[DIR_##DC];                                    \
    const struct gpio * gp = &itc->pins[PIN##suf];                            \
    int result;                                                               \
    IRQNumber in = gpio_to_irq(gp->gpio);                                     \
    result = request_irq(in,                                                  \
			 (irq_handler_t) itc_irq_handler##DC##suf,            \
			 IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,          \
			 gp->label,                                           \
			 NULL);                                               \
    if (result)                                                               \
      printk(KERN_INFO "ITC %s: irq#=%d, result=%d\n", gp->label, in, result);\
    else                                                                      \
      printk(KERN_INFO "ITC %s: OK irq#=%d for gpio=%d\n", gp->label, in, gp->gpio); \
  }
#define XX(DC,fr,p1,p2,p3,p4) ZZ(DC,_IRQLK) ZZ(DC,_IGRLK)
    DIRDATAMACRO()
#undef ZZ
#undef XX
}

void itcExitStructure(ITCInfo * itc)
{
  const char * dn = itcDirName(itc->direction);
  free_irq(gpio_to_irq(itc->pins[PIN_IRQLK].gpio),NULL);
  free_irq(gpio_to_irq(itc->pins[PIN_IGRLK].gpio),NULL);
  printk(KERN_INFO "ITC exit %s\n", dn);
}

void itcExitStructures(void) {

  /////
  /// First do global (full tile) cleanup

  unsigned count = ARRAY_SIZE(pins)*ARRAY_SIZE(pins[0]);
  unsigned i;

  gpio_free_array(&pins[0][0], count);
  printk(KERN_INFO "ITC freed %d pins\n", count); 

  /////
  /// Now do local (per-itc) cleanup

  for (i = DIR_MIN; i <= DIR_MAX; ++i) {
    itcExitStructure(&md.itcInfo[i]);
  }
}

ssize_t itcInterpretCommandByte(u8 cmd)
{
  unsigned long flags;

  if (cmd & 0xc0)  // We only support cmd==0 at present
    return -EINVAL;

  // First set up our request
  spin_lock_irqsave(&mdLock, flags);     // Grab mdLock
  md.userRequestTime = jiffies;          // CRITICAL SECTION: mdLock
  md.userLockset = cmd;                  // CRITICAL SECTION: mdLock
  md.userRequestActive = 1;              // CRITICAL SECTION: mdLock
  spin_unlock_irqrestore(&mdLock, flags); // Free mdLock

  // Now kick each direction, to make sure they see our requests
  for (itcIteratorStart(&md.userContextIterator);
       itcIteratorHasNext(&md.userContextIterator);
       ) {
    ITCDir idx = itcIteratorGetNext(&md.userContextIterator);
    ITCInfo * itc = &md.itcInfo[idx];
    
    spin_lock_irqsave(&mdLock, flags);      // Grab mdLock
    updateState(itc,false);                 // CRITICAL SECTION: mdLock
    spin_unlock_irqrestore(&mdLock, flags); // Free mdLock
  }

  // Now wait until our request has been handled

  while (md.userRequestActive) {
    wait_event_interruptible(userWaitQ, !md.userRequestActive);
  }          

  return 0;      // 'Operation Worked'..
}

int itcGetCurrentLockInfo(u8 * buffer, int len)
{
  ITCIterator * itr = &md.userContextIterator;
  int i;

  if (len < 0) return -EINVAL;
  if (len == 0) return 0;

  if (len > 6) len = 6;

  for (i = 0; i < len; ++i) buffer[i] = 0;
  
  for (itcIteratorStart(itr); itcIteratorHasNext(itr);) {
    ITCDir idx = itcIteratorGetNext(itr);
    ITCInfo * itc = &md.itcInfo[idx];
    u8 bit = 1<<idx;
    switch (len) {
    default:
    case 6: if (itc->state == sRESET) buffer[5] |= bit;
    case 5: if (itc->state == sFAILED) buffer[4] |= bit;
    case 4: if (itc->state == sIDLE) buffer[3] |= bit;
    case 3: if (itc->state == sGIVE) buffer[2] |= bit;
    case 2: if (itc->state == sTAKE ||
                itc->state == sRACE ||
                itc->state == sSYNC01) buffer[1] |= bit;
    case 1: if (itc->state == sTAKEN) buffer[0] |= bit;
    case 0: break;
    }
  }

  return len;
}


void make_reports(void)
{
  int i;
  /*  md.moduleLastActive = jiffies;

  printk(KERN_INFO "ITC timeout %lu\n", md.moduleLastActive);
  */
  for (i = DIR_MIN; i <= DIR_MAX; ++i) {
    /*
    const int killIDLEJiffies = HZ*60;
    if (!isActiveWithin(&md.itcInfo[i], killIDLEJiffies)) {
      printk(KERN_INFO "failing %s\n", itcDirName(md.itcInfo[i].direction));
      setState(&md.itcInfo[i],sFAILED);
      continue;
    }
    */
    if (md.itcInfo[i].lastReported == md.itcInfo[i].lastActive) continue;
#ifdef REPORT_LOCK_STATE_CHANGES
    printk(KERN_INFO "ITC %s(%s): o%d%d i%d%d, f%lu, r%lu, at%lu, ac%lu, gr%lu, co%lu, it%lu, emQ%lu, emG%lu\n",
	   itcDirName(md.itcInfo[i].direction),
	   getStateName(md.itcInfo[i].state),
	   md.itcInfo[i].pinStates[PIN_ORQLK],
	   md.itcInfo[i].pinStates[PIN_OGRLK],
	   md.itcInfo[i].pinStates[PIN_IRQLK],
	   md.itcInfo[i].pinStates[PIN_IGRLK],
	   md.itcInfo[i].enteredCount[sFAILED],
	   md.itcInfo[i].enteredCount[sRESET],
	   md.itcInfo[i].enteredCount[sTAKE],
	   md.itcInfo[i].enteredCount[sTAKEN],
	   md.itcInfo[i].enteredCount[sGIVE],
	   md.itcInfo[i].enteredCount[sRACE],
	   md.itcInfo[i].interruptsTaken,
	   md.itcInfo[i].edgesMissed[0],
	   md.itcInfo[i].edgesMissed[1]
	   );
#endif
    md.itcInfo[i].lastReported = md.itcInfo[i].lastActive;
  }
}

void setState(ITCInfo * itc, ITCState newState) {
  if (itc->state != newState) {
#ifdef REPORT_LOCK_STATE_CHANGES
    printk(KERN_INFO "ITC %s: %s->%s o%d%d i%d%d\n",
           itcDirName(itc->direction),
           getStateName(itc->state),
           getStateName(newState),
           itc->pinStates[PIN_ORQLK],
           itc->pinStates[PIN_OGRLK],
           itc->pinStates[PIN_IRQLK],
           itc->pinStates[PIN_IGRLK]);
#endif
    itc->pinStates[PIN_ORQLK] = (outputsForState[newState]>>1)&1;
    itc->pinStates[PIN_OGRLK] = (outputsForState[newState]>>0)&1;
    itc->lastActive = md.moduleLastActive = jiffies;

    ++itc->enteredCount[newState];
    itc->state = newState;
  }
  gpio_set_value(itc->pins[PIN_ORQLK].gpio,itc->pinStates[PIN_ORQLK]);
  gpio_set_value(itc->pins[PIN_OGRLK].gpio,itc->pinStates[PIN_OGRLK]);
}

void updateState(ITCInfo * itc,bool istimeout) {
  unsigned stateInput;
  ITCState nextState = sFAILED;
  const Rule * rulep;
  unsigned activeTry = 0;
  unsigned activeFree = 0;

  if (md.userRequestActive) {
    unsigned isTake = (md.userLockset>>itc->direction)&1;
    activeTry = isTake;
    activeFree = !isTake;
  }

  stateInput =
    RULE_BITS(
	      itc->pinStates[PIN_IRQLK],
	      itc->pinStates[PIN_IGRLK],
	      itc->isFred,
	      activeTry,
	      activeFree,
              istimeout
	      );

  rulep = ruleSetDispatchTable[itc->state];
  while (1) {
    if ((stateInput & rulep->mask) == rulep->bits) {
      nextState = rulep->newstate;

      if (entryFuncsForState[nextState]) /* Have a custom state entry function? */
        /* We do. Let it maybe update our destination */
        nextState = (*entryFuncsForState[nextState])(itc,stateInput); 

      break;
    }
    if (rulep->endmarker) break;
    ++rulep;
  }

  setState(itc,nextState);
}

void refreshInputs(ITCInfo* itc)
{
  unsigned irq = gpio_get_value(itc->pins[PIN_IRQLK].gpio);
  unsigned igr = gpio_get_value(itc->pins[PIN_IGRLK].gpio);
  BUG_ON(!itc);
  if (irq != itc->pinStates[PIN_IRQLK] ||
      igr != itc->pinStates[PIN_IGRLK]) {

    printk(KERN_INFO "ITC %s i%d%d -> i%d%d\n",
           itcDirName(itc->direction),
           itc->pinStates[PIN_IRQLK],
           itc->pinStates[PIN_IGRLK],
           irq,
           igr
           );

    itc->pinStates[PIN_IRQLK] = irq;
    itc->pinStates[PIN_IGRLK] = igr;
  }
}

void updateStates(ITCIterator * itr, bool istimeout) {
  for (itcIteratorStart(itr); itcIteratorHasNext(itr);) {
    ITCDir idx = itcIteratorGetNext(itr);
    ITCInfo * itc = &md.itcInfo[idx];
    refreshInputs(itc);
    updateState(itc,istimeout);
  }
}

////CUSTOM STATE ENTRY FUNCTIONS
ITCState entryFunction_sWAIT(ITCInfo * itc,unsigned stateInput) {
  BUG_ON(!itc);
  /*
  printk(KERN_INFO "ITC %s i%d%d from %s efWAIT %08x, to %u, tol %u\n",
         itcDirName(itc->direction),
         itc->pinStates[PIN_IRQLK],
         itc->pinStates[PIN_IGRLK],
         getStateName(itc->state),
         stateInput,
         itc->magicWaitTimeouts,
         itc->magicWaitTimeoutLimit
         );
  */
  
  /* If we're timing-out in sWAIT, check magic counters */
  if (itc->state == sWAIT && (stateInput & BINP_TIMEOUT)) {
    /* If we've exhausted our patience.. */
    if (++itc->magicWaitTimeouts > itc->magicWaitTimeoutLimit) {
      /* ..be twice as patient next time, up to a limit..*/
      if (itc->magicWaitTimeoutLimit < (1<<10))
        itc->magicWaitTimeoutLimit <<= 1;
      /* ..and try failing instead of waiting.. */
      itc->magicWaitTimeouts = 0;
      return sFAILED;
    }
  }
  return sWAIT;
}

ITCState entryFunction_sFAILED(ITCInfo * itc,unsigned stateInput) {
  BUG_ON(!itc);
  /* Getting to sFAILED from anywhere _but_ sWAIT seems a pathetic
     reason to have hope, but let's believe in love after love..*/
  if (itc->state != sWAIT) {
    itc->magicWaitTimeouts = 0;
    itc->magicWaitTimeoutLimit = 1;
  }
  return sFAILED;
}

/** @brief The ITC main timing loop
 *
 *  @param arg A void pointer available to pass data to the thread
 *  @return returns 0 if successful
 */
int itcThreadRunner(void *arg) {
  const int jiffyTimeout = HZ/10;

  ITCIterator idxItr;
  itcIteratorInitialize(&idxItr,2500); // init with rare shuffling

  printk(KERN_INFO "itcThreadRunner: Started\n");
  while(!kthread_should_stop()) {    // Returns true when kthread_stop() is called
    set_current_state(TASK_RUNNING);

    if (md.userRequestActive && time_before(md.userRequestTime + jiffyTimeout/10, jiffies)) {
#ifdef REPORT_LOCK_STATE_CHANGES
      printk(KERN_INFO "itcThreadRunner: Clearing userRequestActive\n");
#endif
      md.userRequestActive = 0;
      wake_up_interruptible(&userWaitQ);
    }

    if (time_before(md.moduleLastActive + jiffyTimeout, jiffies)) {
      updateStates(&idxItr,true);
      md.moduleLastActive = jiffies;
    }

    make_reports();
    set_current_state(TASK_INTERRUPTIBLE);
    msleep(300);
  }
  printk(KERN_INFO "itcThreadRunner: Stopping by request\n");
  return 0;
}

void itcImplInit(void)
{
  itcInitStructures();
  printk(KERN_INFO "itcImplInit@jiffies=%lu\n",jiffies);
}


void itcImplExit(void) {
  itcExitStructures();
}
