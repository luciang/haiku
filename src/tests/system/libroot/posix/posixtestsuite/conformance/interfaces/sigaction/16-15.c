/*
* Copyright (c) 2005, Bull S.A..  All rights reserved.
* Created by: Sebastien Decugis

* This program is free software; you can redistribute it and/or modify it
* under the terms of version 2 of the GNU General Public License as
* published by the Free Software Foundation.
*
* This program is distributed in the hope that it would be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*
* You should have received a copy of the GNU General Public License along
* with this program; if not, write the Free Software Foundation, Inc., 59
* Temple Place - Suite 330, Boston MA 02111-1307, USA.


* This sample test aims to check the following assertions:
*
* If SA_RESTART is set in sa_flags, interruptible function interrupted by signal
* shall restart silently.

* The steps are:
* -> create a child thread 
* -> child registers a handler for SIGTTIN with SA_RESTART, then waits for the semaphore
* -> parent kills the child with SIGTTIN, then post the semaphore.

* The test fails if the sem_wait function returns EINTR

*Note:
This test uses sem_wait to check if EINTR is returned. As the function is not required to
fail with EINTR, the test may return PASS and the feature not be correct (false positive).
Anyway, a false negative status cannot be returned.

*/


/* We are testing conformance to IEEE Std 1003.1, 2003 Edition */
#define _POSIX_C_SOURCE 200112L

/* This test tests for an XSI feature */
#define _XOPEN_SOURCE 600

/******************************************************************************/
/*************************** standard includes ********************************/
/******************************************************************************/
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <semaphore.h>
#include <signal.h>
#include <errno.h>

/******************************************************************************/
/***************************   Test framework   *******************************/
/******************************************************************************/
#include "testfrmw.h"
#include "testfrmw.c" 
/* This header is responsible for defining the following macros:
 * UNRESOLVED(ret, descr);  
 *    where descr is a description of the error and ret is an int 
 *   (error code for example)
 * FAILED(descr);
 *    where descr is a short text saying why the test has failed.
 * PASSED();
 *    No parameter.
 * 
 * Both three macros shall terminate the calling process.
 * The testcase shall not terminate in any other maneer.
 * 
 * The other file defines the functions
 * void output_init()
 * void output(char * string, ...)
 * 
 * Those may be used to output information.
 */

/******************************************************************************/
/**************************** Configuration ***********************************/
/******************************************************************************/
#ifndef VERBOSE
#define VERBOSE 1
#endif

#define SIGNAL SIGTTIN

/******************************************************************************/
/***************************    Test case   ***********************************/
/******************************************************************************/

volatile sig_atomic_t caught = 0;
sem_t sem;

/* Handler function */
void handler( int signo )
{
	printf( "Caught signal %d\n", signo );
	caught++;
}

/* Thread function */
void * threaded ( void * arg )
{
	int ret = 0;

	ret = sem_wait( &sem );

	if ( ret != 0 )
	{
		if ( errno == EINTR )
		{
			FAILED( "The function returned EINTR while SA_RESTART is set" );
		}
		else
		{
			UNRESOLVED( errno, "sem_wait failed" );
		}
	}

	return NULL;
}

/* main function */
int main()
{
	int ret;
	pthread_t child;


	struct sigaction sa;

	/* Initialize output */
	output_init();

	/* Set the signal handler */
	sa.sa_flags = SA_RESTART;
	sa.sa_handler = handler;
	ret = sigemptyset( &sa.sa_mask );

	if ( ret != 0 )
	{
		UNRESOLVED( ret, "Failed to empty signal set" );
	}

	/* Install the signal handler for SIGNAL */
	ret = sigaction( SIGNAL, &sa, 0 );

	if ( ret != 0 )
	{
		UNRESOLVED( ret, "Failed to set signal handler" );
	}

	/* Initialize the semaphore */
	ret = sem_init( &sem, 0, 0 );

	if ( ret != 0 )
	{
		UNRESOLVED( ret, "Failed to init a semaphore" );
	}

	/* Create the child thread */
	ret = pthread_create( &child, NULL, threaded, NULL );

	if ( ret != 0 )
	{
		UNRESOLVED( ret, "Failed to create a child thread" );
	}

	/* Let the child thread enter the wait routine...
	  we use sched_yield as there is no certain way to test that the child
	  is waiting for the semaphore... */
	sched_yield();

	sched_yield();

	sched_yield();

	/* Ok, now kill the child */
	ret = pthread_kill( child, SIGNAL );

	if ( ret != 0 )
	{
		UNRESOLVED( ret, "Failed to kill the child thread" );
	}

	/* wait that the child receives the signal */
	while ( !caught )
		sched_yield();

	/* Now let the child run and terminate */
	ret = sem_post( &sem );

	if ( ret != 0 )
	{
		UNRESOLVED( errno, "Failed to post the semaphore" );
	}

	ret = pthread_join( child, NULL );

	if ( ret != 0 )
	{
		UNRESOLVED( ret, "Failed to join the thread" );
	}

	/* terminate */
	ret = sem_destroy( &sem );

	if ( ret != 0 )
	{
		UNRESOLVED( ret, "Failed to destroy the semaphore" );
	}


	/* Test passed */
#if VERBOSE > 0

	output( "Test passed\n" );

#endif

	PASSED;
}
