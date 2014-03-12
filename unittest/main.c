/*****************************************************************************
 *
 *  $Id: main.c,v 6a6dec6fc806 2012/09/19 17:46:58 fp $
 *
 *  Copyright (C) 2007-2009  Florian Pose, Ingenieurgemeinschaft IgH
 *
 *  This file is part of the IgH EtherCAT Master.
 *
 *  The IgH EtherCAT Master is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License version 2, as
 *  published by the Free Software Foundation.
 *
 *  The IgH EtherCAT Master is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 *  Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the IgH EtherCAT Master; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *  ---
 *
 *  The license mentioned above concerns the source code only. Using the
 *  EtherCAT technology and brand is only permitted in compliance with the
 *  industrial property and similar rights of Beckhoff Automation GmbH.
 *
 ****************************************************************************/

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

/****************************************************************************/

#include "ecrt.h"

/****************************************************************************/

// Application parameters
#define FREQUENCY 100
#define PRIORITY 0

/****************************************************************************/

// EtherCAT
static ec_master_t *master = NULL;
static ec_master_state_t master_state = {};

static ec_domain_t *domain1 = NULL;
static ec_domain_state_t domain1_state = {};

static ec_slave_config_t *sc_ana_in = NULL;
static ec_slave_config_state_t sc_ana_in_state = {};

// Timer
static unsigned int sig_alarms = 0;
static unsigned int user_alarms = 0;

/****************************************************************************/

// process data
static uint8_t *domain1_pd = NULL;

#define BusCouplerPos  0, 3
#define DigOutSlavePos 0, 0

//#define Beckhoff_EK1100 0x00000002, 0x044c2c52
#define Beckhoff_EK1100 0x00000002, 0x04562c52
#define Beckhoff_EL1008 0x00000002, 0x03f03052
#define Beckhoff_EL2004 0x00000002, 0x07d43052
#define Beckhoff_EL2008 0x00000002, 0x07d83052
#define Beckhoff_EL2032 0x00000002, 0x07f03052
#define Beckhoff_EL3152 0x00000002, 0x0c503052
#define Beckhoff_EL3102 0x00000002, 0x0c1e3052
#define Beckhoff_EL4102 0x00000002, 0x10063052

// offsets for PDO entries
static unsigned int off_dig_in[1];
static unsigned int off_dig_out[2];


/*****************************************************************************/
// Digital in ------------------------
static ec_pdo_entry_info_t el1008_channels[] = {
    {0x6000, 1, 1},
    {0x6010, 1, 1},
    {0x6020, 1, 1},
    {0x6030, 1, 1},
    {0x6040, 1, 1},
    {0x6050, 1, 1},
    {0x6060, 1, 1},
    {0x6070, 1, 1},
};

static ec_pdo_info_t el1008_pdos[] = {
    {0x1a00, 1, &el1008_channels[0]},
    {0x1a01, 1, &el1008_channels[1]},
    {0x1a02, 1, &el1008_channels[2]},
    {0x1a03, 1, &el1008_channels[3]},
    {0x1a04, 1, &el1008_channels[4]},
    {0x1a05, 1, &el1008_channels[5]},
    {0x1a06, 1, &el1008_channels[6]},
    {0x1a07, 1, &el1008_channels[7]}
};

static ec_sync_info_t el1008_syncs[] = {
    {2, EC_DIR_OUTPUT},
    {3, EC_DIR_INPUT, 8, el1008_pdos},
    {0xff}
};

// Digital out ------------------------
static ec_pdo_entry_info_t el2008_channels[] = {
    {0x7000, 1, 1},
    {0x7010, 1, 1},
    {0x7020, 1, 1},
    {0x7030, 1, 1},
    {0x7040, 1, 1},
    {0x7050, 1, 1},
    {0x7060, 1, 1},
    {0x7070, 1, 1},
};

static ec_pdo_info_t el2008_pdos[] = {
    {0x1600, 1, &el2008_channels[0]},
    {0x1601, 1, &el2008_channels[1]},
    {0x1602, 1, &el2008_channels[2]},
    {0x1603, 1, &el2008_channels[3]},
    {0x1604, 1, &el2008_channels[4]},
    {0x1605, 1, &el2008_channels[5]},
    {0x1606, 1, &el2008_channels[6]},
    {0x1607, 1, &el2008_channels[7]}
};

static ec_sync_info_t el2008_syncs[] = {
    {0, EC_DIR_OUTPUT, 8, el2008_pdos},
    {1, EC_DIR_INPUT},
    {0xff}
};

/*****************************************************************************/

void check_domain1_state(void)
{
    ec_domain_state_t ds;

    ecrt_domain_state(domain1, &ds);

    if (ds.working_counter != domain1_state.working_counter)
        printf("Domain1: WC %u.\n", ds.working_counter);
    if (ds.wc_state != domain1_state.wc_state)
        printf("Domain1: State %u.\n", ds.wc_state);

    domain1_state = ds;
}

/*****************************************************************************/

void check_master_state(void)
{
    ec_master_state_t ms;

    ecrt_master_state(master, &ms);

    if (ms.slaves_responding != master_state.slaves_responding)
        printf("%u slave(s).\n", ms.slaves_responding);
    if (ms.al_states != master_state.al_states)
        printf("AL states: 0x%02X.\n", ms.al_states);
    if (ms.link_up != master_state.link_up)
        printf("Link is %s.\n", ms.link_up ? "up" : "down");

    master_state = ms;
}

/*****************************************************************************/

void check_slave_config_states(void)
{
    ec_slave_config_state_t s;

    ecrt_slave_config_state(sc_ana_in, &s);

    if (s.al_state != sc_ana_in_state.al_state)
        printf("AnaIn: State 0x%02X.\n", s.al_state);
    if (s.online != sc_ana_in_state.online)
        printf("AnaIn: %s.\n", s.online ? "online" : "offline");
    if (s.operational != sc_ana_in_state.operational)
        printf("AnaIn: %soperational.\n",
                s.operational ? "" : "Not ");

    sc_ana_in_state = s;
}

/****************************************************************************/

void cyclic_task()
{
    static unsigned int counter = 10;
    static uint8_t outputValue = 0;
    static int numAsyncCycles = 0;
    uint8_t inputValue = 0;
    static uint8_t error = 0;

    // receive process data
    ecrt_master_receive(master);
    ecrt_domain_process(domain1);

    // check process data state (optional)
    check_domain1_state();
    

	inputValue = EC_READ_U8(domain1_pd + off_dig_in[0]);
	
	if(inputValue != outputValue) {
		numAsyncCycles++;
	} else {
		numAsyncCycles = 0;
	}
	
	if(numAsyncCycles > 2) {
		if(error != 0xff) {
			error++;
		}
	}
	
	
    if (counter) {
        counter--;
    } else {
        counter = 5; //update delay
        
        

        // calculate new process data
        outputValue++;

        // check for master state (optional)
        check_master_state();

        // check for islave configuration state(s) (optional)
        check_slave_config_states();
    }

    // write process data
    EC_WRITE_U8(domain1_pd + off_dig_out[1], outputValue);
    EC_WRITE_U8(domain1_pd + off_dig_out[0], error);

    // send process data
    ecrt_domain_queue(domain1);
    ecrt_master_send(master);
}

/****************************************************************************/

void signal_handler(int signum) {
    switch (signum) {
        case SIGALRM:
            sig_alarms++;
            break;
    }
}

/****************************************************************************/

int Init_EL2008(uint16_t position)
{
    ec_slave_config_t *sc;
    if (!(sc = ecrt_master_slave_config(master, 0, position, Beckhoff_EL2008))) {
        fprintf(stderr, "Failed to get EL2008 configuration #%u.\n", position);
        return -1;
    }
    if (ecrt_slave_config_pdos(sc, EC_END, el2008_syncs)) {
        fprintf(stderr, "Failed to configure PDOs #%u.\n", position);
        return -1;
    }
    if (0 > (off_dig_out[position] = ecrt_slave_config_reg_pdo_entry(sc, 0x7000, 1, domain1, NULL))) {
		fprintf(stderr, "Failed to configure reg PDOs #%u.\n", position);
		return -1;
	}
    fprintf(stderr, "EL2008 #%u configured offset: %d.\n", position, off_dig_out[position]);
    return 0;
}

int main(int argc, char **argv)
{
    ec_slave_config_t *sc;
    struct sigaction sa;
    struct itimerval tv;
    uint16_t i;

    master = ecrt_request_master(0);
    if (!master)
        return -1;

    domain1 = ecrt_master_create_domain(master);
    if (!domain1)
        return -1;

    printf("Configuring PDOs...\n");
    if (!(sc_ana_in = ecrt_master_slave_config(master, 0, 2, Beckhoff_EL1008))) {
        fprintf(stderr, "Failed to get digital in configuration.\n");
        return -1;
    }
    if (ecrt_slave_config_pdos(sc_ana_in, EC_END, el1008_syncs)) {
        fprintf(stderr, "Failed to configure PDOs.\n");
        return -1;
    }
    if (0 > (off_dig_in[0] = ecrt_slave_config_reg_pdo_entry(sc_ana_in, 0x6000, 1, domain1, NULL))) {
		fprintf(stderr, "Failed to configure reg PDOs.\n");
		return -1;
	}
    printf("EL1008 configured.\n");

    for(i = 0; i < 2; ++i) {
        if(Init_EL2008(i)) {
            fprintf(stderr, "Failed to initialize EL2008 #%u.\n", i);
            return -1;
        }
    }

    // Create configuration for bus coupler
    sc = ecrt_master_slave_config(master, BusCouplerPos, Beckhoff_EK1100);
    if (!sc)
        return -1;
    fprintf(stderr, "EK1100 configured.\n");

    printf("Activating master...\n");
    if (ecrt_master_activate(master))
        return -1;

    if (!(domain1_pd = ecrt_domain_data(domain1))) {
        return -1;
    }

#if PRIORITY
    pid_t pid = getpid();
    if (setpriority(PRIO_PROCESS, pid, -19))
        fprintf(stderr, "Warning: Failed to set priority: %s\n",
                strerror(errno));
#endif

    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGALRM, &sa, 0)) {
        fprintf(stderr, "Failed to install signal handler!\n");
        return -1;
    }

    printf("Starting timer...\n");
    tv.it_interval.tv_sec = 0;
    tv.it_interval.tv_usec = 1000000 / FREQUENCY;
    tv.it_value.tv_sec = 0;
    tv.it_value.tv_usec = 1000;
    if (setitimer(ITIMER_REAL, &tv, NULL)) {
        fprintf(stderr, "Failed to start timer: %s\n", strerror(errno));
        return 1;
    }

    printf("Started.\n");
    while (1) {
        pause();

#if 0
        struct timeval t;
        gettimeofday(&t, NULL);
        printf("%u.%06u\n", t.tv_sec, t.tv_usec);
#endif

        while (sig_alarms != user_alarms) {
            cyclic_task();
            user_alarms++;
        }
    }

    return 0;
}

/****************************************************************************/
