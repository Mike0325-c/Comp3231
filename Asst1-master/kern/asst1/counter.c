#include "opt-synchprobs.h"
#include "counter.h"
#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <test.h>
#include <thread.h>
#include <synch.h>


/*
 * Declare the counter variable that all threads increment or decrement
 * via the interface provided here.
 *
 * Declaring it "volatile" instructs the compiler to always (re)read the
 * variable from memory and not to optimise by keeping the value in a process 
 * register and avoid memory references.
 *
 * NOTE: The volatile declaration is actually not needed for the provided code 
 * as the variable is only loaded once in each function.
 */

static volatile int the_counter;
struct lock *count_lock;

/*
 * ********************************************************************
 * INSERT ANY GLOBAL VARIABLES YOU REQUIRE HERE
 * ********************************************************************
 */


void counter_increment(void)
{
	lock_acquire(count_lock);
        the_counter = the_counter + 1;
        lock_release(count_lock);
        
}

void counter_decrement(void)
{
	lock_acquire(count_lock);
        the_counter = the_counter - 1;
        lock_release(count_lock);
}

int counter_initialise(int val)
{
        the_counter = val;

        /*
         * ********************************************************************
         * INSERT ANY INITIALISATION CODE YOU REQUIRE HERE
         * ********************************************************************
         */
         
         count_lock = lock_create("count_lock");

         if (count_lock == NULL) {
                return ENOMEM;
         }
        
        /*
         * Return 0 to indicate success
         * Return non-zero to indicate error.
         * e.g. 
         * return ENOMEM
         * indicates an allocation failure to the caller 
         */
        
        return 0;
}

int counter_read_and_destroy(void)
{
        /*
         * **********************************************************************
         * INSERT ANY CLEANUP CODE YOU REQUIRE HERE
         * **********************************************************************
         */
	lock_destroy(count_lock);
        return the_counter;
}
