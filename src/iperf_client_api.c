/*
 * Copyright (c) 2009-2014, The Regents of the University of California,
 * through Lawrence Berkeley National Laboratory (subject to receipt of any
 * required approvals from the U.S. Dept. of Energy).  All rights reserved.
 *
 * This code is distributed under a BSD style license, see the LICENSE file
 * for complete information.
 */

#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/uio.h>
#include <arpa/inet.h>

#include "iperf.h"
#include "iperf_api.h"
#include "iperf_util.h"
#include "locale.h"
#include "net.h"
#include "timer.h"


int
iperf_create_streams(struct iperf_test *test)
{
    int i, s;
    struct iperf_stream *sp;

    for (i = 0; i < test->num_streams; ++i) {

        if ((s = test->protocol->connect(test)) < 0)
            return -1;

	if (test->sender)
	    FD_SET(s, &test->write_set);
	else
	    FD_SET(s, &test->read_set);
	if (s > test->max_fd) test->max_fd = s;

        sp = iperf_new_stream(test, s);
        if (!sp)
            return -1;

        /* Perform the new stream callback */
        if (test->on_new_stream)
            test->on_new_stream(sp);
    }

    return 0;
}

static void
test_timer_proc(TimerClientData client_data, struct timeval *nowP)
{
    struct iperf_test *test = client_data.p;

    test->timer = NULL;
    test->done = 1;
}

static void
client_stats_timer_proc(TimerClientData client_data, struct timeval *nowP)
{
    struct iperf_test *test = client_data.p;

    if (test->done)
        return;
    if (test->stats_callback)
	test->stats_callback(test);
}

static void
client_reporter_timer_proc(TimerClientData client_data, struct timeval *nowP)
{
    struct iperf_test *test = client_data.p;

    if (test->done)
        return;
    if (test->reporter_callback)
	test->reporter_callback(test);
}

static int
create_client_timers(struct iperf_test * test)
{
    struct timeval now;
    TimerClientData cd;

    if (gettimeofday(&now, NULL) < 0) {
	i_errno = IEINITTEST;
	return -1;
    }
    cd.p = test;
    test->timer = test->stats_timer = test->reporter_timer = NULL;
    if (test->duration != 0) {
	test->done = 0;
        test->timer = tmr_create(&now, test_timer_proc, cd, ( test->duration + test->omit ) * SEC_TO_US, 0);
        if (test->timer == NULL) {
            i_errno = IEINITTEST;
            return -1;
	}
    } 
    if (test->stats_interval != 0) {
        test->stats_timer = tmr_create(&now, client_stats_timer_proc, cd, test->stats_interval * SEC_TO_US, 1);
        if (test->stats_timer == NULL) {
            i_errno = IEINITTEST;
            return -1;
	}
    }
    if (test->reporter_interval != 0) {
        test->reporter_timer = tmr_create(&now, client_reporter_timer_proc, cd, test->reporter_interval * SEC_TO_US, 1);
        if (test->reporter_timer == NULL) {
            i_errno = IEINITTEST;
            return -1;
	}
    }
    return 0;
}

static void
client_omit_timer_proc(TimerClientData client_data, struct timeval *nowP)
{
    struct iperf_test *test = client_data.p;

    test->omit_timer = NULL;
    test->omitting = 0;
    iperf_reset_stats(test);
    if (test->verbose && !test->json_output && test->reporter_interval == 0)
        iprintf(test, "%s", report_omit_done);

    /* Reset the timers. */
    if (test->stats_timer != NULL)
        tmr_reset(nowP, test->stats_timer);
    if (test->reporter_timer != NULL)
        tmr_reset(nowP, test->reporter_timer);
}

static int
create_client_omit_timer(struct iperf_test * test)
{
    struct timeval now;
    TimerClientData cd;

    if (test->omit == 0) {
	test->omit_timer = NULL;
        test->omitting = 0;
    } else {
	if (gettimeofday(&now, NULL) < 0) {
	    i_errno = IEINITTEST;
	    return -1;
	}
	test->omitting = 1;
	cd.p = test;
	test->omit_timer = tmr_create(&now, client_omit_timer_proc, cd, test->omit * SEC_TO_US, 0);
	if (test->omit_timer == NULL) {
	    i_errno = IEINITTEST;
	    return -1;
	}
    }
    return 0;
}

int
iperf_handle_message_client(struct iperf_test *test)
{
    int rval;
    int32_t err;

    /*!!! Why is this read() and not Nread()? */
    if ((rval = read(test->ctrl_sck, (char*) &test->state, sizeof(signed char))) <= 0) {
        if (rval == 0) {
            i_errno = IECTRLCLOSE;
            return -1;
        } else {
            i_errno = IERECVMESSAGE;
            return -1;
        }
    }

    switch (test->state) {
        case PARAM_EXCHANGE:
            if (iperf_exchange_parameters(test) < 0)
                return -1;
            if (test->on_connect)
                test->on_connect(test);
            break;
        case CREATE_STREAMS:
            if (iperf_create_streams(test) < 0)
                return -1;
            break;
        case TEST_START:
            if (iperf_init_test(test) < 0)
                return -1;
            if (create_client_timers(test) < 0)
                return -1;
            if (create_client_omit_timer(test) < 0)
                return -1;
	    if (!test->reverse)
		if (iperf_create_send_timers(test) < 0)
		    return -1;
            break;
        case TEST_RUNNING:
            break;
        case EXCHANGE_RESULTS:
            if (iperf_exchange_results(test) < 0)
                return -1;
            break;
        case DISPLAY_RESULTS:
            if (test->on_test_finish)
                test->on_test_finish(test);
            iperf_client_end(test);
            break;
        case IPERF_DONE:
            break;
        case SERVER_TERMINATE:
            i_errno = IESERVERTERM;

	    /*
	     * Temporarily be in DISPLAY_RESULTS phase so we can get
	     * ending summary statistics.
	     */
	    signed char oldstate = test->state;
	    cpu_util(test->cpu_util);
	    test->state = DISPLAY_RESULTS;
	    test->reporter_callback(test);
	    test->state = oldstate;
            return -1;
        case ACCESS_DENIED:
            i_errno = IEACCESSDENIED;
            return -1;
        case SERVER_ERROR:
            if (Nread(test->ctrl_sck, (char*) &err, sizeof(err), Ptcp) < 0) {
                i_errno = IECTRLREAD;
                return -1;
            }
	    i_errno = ntohl(err);
            if (Nread(test->ctrl_sck, (char*) &err, sizeof(err), Ptcp) < 0) {
                i_errno = IECTRLREAD;
                return -1;
            }
            errno = ntohl(err);
            return -1;
        default:
            i_errno = IEMESSAGE;
            return -1;
    }

    return 0;
}



/* iperf_connect -- client to server connection function */
int
iperf_connect(struct iperf_test *test)
{
    FD_ZERO(&test->read_set);
    FD_ZERO(&test->write_set);

    make_cookie(test->cookie);

    /* Create and connect the control channel */
    if (test->ctrl_sck < 0)
	test->ctrl_sck = netdial(test->settings->domain, Ptcp, test->bind_address, test->server_hostname, test->server_port);
    if (test->ctrl_sck < 0) {
        i_errno = IECONNECT;
        return -1;
    }

    if (Nwrite(test->ctrl_sck, test->cookie, COOKIE_SIZE, Ptcp) < 0) {
        i_errno = IESENDCOOKIE;
        return -1;
    }

    FD_SET(test->ctrl_sck, &test->read_set);
    if (test->ctrl_sck > test->max_fd) test->max_fd = test->ctrl_sck;

    return 0;
}


int
iperf_client_end(struct iperf_test *test)
{
    struct iperf_stream *sp;

    /* Close all stream sockets */
    SLIST_FOREACH(sp, &test->streams, streams) {
        close(sp->socket);
    }

    /* show final summary */
    test->reporter_callback(test);

    if (iperf_set_send_state(test, IPERF_DONE) != 0)
        return -1;

    return 0;
}


static jmp_buf sigend_jmp_buf;

static void
sigend_handler(int sig)
{
    longjmp(sigend_jmp_buf, 1);
}


typedef enum { cm_select, cm_itimer } cm_t;

static int sigalrm_triggered;

static void
sigalrm_handler(int sig)
{
    sigalrm_triggered = 1;
}

int
iperf_run_client(struct iperf_test * test)
{
    cm_t concurrency_model;
    int startup;
    int result = 0;
    fd_set read_set, write_set;
    struct timeval now;
    struct timeval* timeout = NULL;
    struct itimerval itv;

    /* Termination signals. */
    iperf_catch_sigend(sigend_handler);
    if (setjmp(sigend_jmp_buf))
	iperf_got_sigend(test);

    if (test->affinity != -1)
	if (iperf_setaffinity(test->affinity) != 0)
	    return -1;

    if (test->json_output)
	if (iperf_json_start(test) < 0)
	    return -1;

    if (test->json_output) {
	cJSON_AddItemToObject(test->json_start, "version", cJSON_CreateString(version));
	cJSON_AddItemToObject(test->json_start, "system_info", cJSON_CreateString(get_system_info()));
    } else if (test->verbose) {
	iprintf(test, "%s\n", version);
	iprintf(test, "%s", "");
	fflush(stdout);
	system("uname -a");
    }

    /* Start the client and connect to the server */
    if (iperf_connect(test) < 0)
        return -1;

    /* Begin calculating CPU utilization */
    cpu_util(NULL);

    startup = 1;
    concurrency_model = cm_select;	/* always start in select mode */
    while (test->state != IPERF_DONE) {

	if (concurrency_model == cm_select) {
	    memcpy(&read_set, &test->read_set, sizeof(fd_set));
	    memcpy(&write_set, &test->write_set, sizeof(fd_set));
	    (void) gettimeofday(&now, NULL);
	    timeout = tmr_timeout(&now);
	    result = select(test->max_fd + 1, &read_set, &write_set, NULL, timeout);
	    if (result < 0 && errno != EINTR) {
		i_errno = IESELECT;
		return -1;
	    }
	    if (result > 0) {
		if (FD_ISSET(test->ctrl_sck, &read_set)) {
		    if (iperf_handle_message_client(test) < 0) {
			return -1;
		    }
		    FD_CLR(test->ctrl_sck, &read_set);
		}
	    }
	}

	if (test->state == TEST_RUNNING) {

	    /* Is this our first time really running? */
	    if (startup) {
	        startup = 0;
		/* Decide which concurrency model to use for the real test.
		** SIGALRM is less overhead but there are a bunch of cases
		** where it either won't work or is ill-advised.
		*/
		if (test->may_use_sigalrm && test->settings->rate == 0 &&
		    (test->stats_interval == 0 || test->stats_interval > 0.2) &&
		    (test->reporter_interval == 0 || test->reporter_interval > 0.2) &&
		    (test->omit == 0 || test->omit > 0.2) &&
		    ! test->reverse) {
		    concurrency_model = cm_itimer;
		    test->multisend = 1;
		    signal(SIGALRM, sigalrm_handler);
		    sigalrm_triggered = 0;
		    itv.it_interval.tv_sec = 0;
		    itv.it_interval.tv_usec = 100000;
		    itv.it_value.tv_sec = 0;
		    itv.it_value.tv_usec = 100000;
		    (void) setitimer(ITIMER_REAL, &itv, NULL);
		}

	    }

	    if (test->reverse) {
		// Reverse mode. Client receives.
		if (iperf_recv(test, &read_set) < 0)
		    return -1;
	    } else {
		// Regular mode. Client sends.
		if (iperf_send(test, concurrency_model == cm_itimer ? NULL : &write_set) < 0)
		    return -1;
	    }

	    if ((concurrency_model == cm_select &&
	         (result == 0 ||
		  (timeout != NULL && timeout->tv_sec == 0 && timeout->tv_usec == 0))) ||
	        (concurrency_model == cm_itimer && sigalrm_triggered)) {
		/* Run the timers. */
		(void) gettimeofday(&now, NULL);
		tmr_run(&now);
	        if (concurrency_model == cm_itimer)
		    sigalrm_triggered = 0;
	    }

	    /* Is the test done yet? */
	    if ((!test->omitting) &&
	        ((test->duration != 0 && test->done) ||
	         (test->settings->bytes != 0 && test->bytes_sent >= test->settings->bytes) ||
	         (test->settings->blocks != 0 && test->blocks_sent >= test->settings->blocks))) {
		/* Yes, done!  Send TEST_END. */
		test->done = 1;
		cpu_util(test->cpu_util);
		test->stats_callback(test);
		if (iperf_set_send_state(test, TEST_END) != 0)
		    return -1;
		/* If we were doing setitimer(), go back to select() for the end. */
		if (concurrency_model == cm_itimer) {
		    itv.it_interval.tv_sec = 0;
		    itv.it_interval.tv_usec = 0;
		    itv.it_value.tv_sec = 0;
		    itv.it_value.tv_usec = 0;
		    (void) setitimer(ITIMER_REAL, &itv, NULL);
		    concurrency_model = cm_select;
		}
	    }
	}
	// If we're in reverse mode, continue draining the data
	// connection(s) even if test is over.  This prevents a
	// deadlock where the server side fills up its pipe(s)
	// and gets blocked, so it can't receive state changes
	// from the client side.
	else if (test->reverse && test->state == TEST_END) {
	    if (iperf_recv(test, &read_set) < 0)
		return -1;
	}
    }

    if (test->json_output) {
	if (iperf_json_finish(test) < 0)
	    return -1;
    } else {
	iprintf(test, "\n");
	iprintf(test, "%s", report_done);
    }

    return 0;
}
