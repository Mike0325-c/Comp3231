#include "opt-synchprobs.h"
#include "kitchen.h"
#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <test.h>
#include <thread.h>
#include <synch.h>




/*
 * ********************************************************************
 * INSERT ANY GLOBAL VARIABLES YOU REQUIRE HERE
 * ********************************************************************
 */

int count = 0;
struct cv *buffer_full;
struct cv *buffer_empty;
struct lock *buffer_lock;

/*
 * initialise_kitchen: 
 *
 * This function is called during the initialisation phase of the
 * kitchen, i.e.before any threads are created.
 *
 * Initialise any global variables or create any synchronisation
 * primitives here.
 * 
 * The function returns 0 on success or non-zero on failure.
 */

int initialise_kitchen()
{
        buffer_full = cv_create("full");
        if (buffer_full == NULL) {
                return 1;
        }
        buffer_empty = cv_create("empty");
        if (buffer_empty == NULL) {
                return 1;
        }
        buffer_lock = lock_create("buffer_lock");
        if (buffer_lock == NULL) { 
                return 1;
        }
        return 0;
}

/*
 * cleanup_kitchen:
 *
 * This function is called after the dining threads and cook thread
 * have exited the system. You should deallocated any memory allocated
 * by the initialisation phase (e.g. destroy any synchronisation
 * primitives).
 */

void cleanup_kitchen()
{
        cv_destroy(buffer_empty);
        cv_destroy(buffer_full);
        lock_destroy(buffer_lock);
}


/*
 * do_cooking:
 *
 * This function is called repeatedly by the cook thread to provide
 * enough soup to dining customers. It creates soup by calling
 * cook_soup_in_pot().
 *
 * It should wait until the pot is empty before calling
 * cook_soup_in_pot().
 *
 * It should wake any dining threads waiting for more soup.
 */

void do_cooking()
{
        lock_acquire(buffer_lock);
        while (count != 0) 
        {
                cv_wait(buffer_full,buffer_lock);
        }
        count += POTSIZE_IN_SERVES;
        cook_soup_in_pot();
        cv_broadcast(buffer_empty, buffer_lock);
        lock_release(buffer_lock);
}

/*
 * fill_bowl:
 *
 * This function is called repeatedly by dining threads to obtain soup
 * to satify their hunger. Dining threads fill their bowl by calling
 * get_serving_from_pot().
 *
 * It should wait until there is soup in the pot before calling
 * get_serving_from_pot().
 *
 * get_serving_from_pot() should be called mutually exclusively as
 * only one thread can fill their bowl at a time.
 *
 * fill_bowl should wake the cooking thread if there is no soup left
 * in the pot.
 */

void fill_bowl()
{
        lock_acquire(buffer_lock);
        while (count == 0) {
                cv_wait(buffer_empty,buffer_lock);
        }
        count--;
        get_serving_from_pot();
        cv_broadcast(buffer_full, buffer_lock);
        lock_release(buffer_lock);
}
