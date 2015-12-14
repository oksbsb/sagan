/* $Id$ */
/*
** Copyright (C) 2009-2015 Quadrant Information Security <quadrantsec.com>
** Copyright (C) 2009-2015 Champ Clark III <cclark@quadrantsec.com>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License Version 2 as
** published by the Free Software Foundation.  You may not use, modify or
** distribute this program under any other version of the GNU General
** Public License.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

/* sagan-ipc.c
 *
 * This allows Sagan to share data with other Sagan processes. This is for
 * Inter-process communications (IPC).
 *
 */

#include <stdio.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <inttypes.h>
#include <unistd.h>
#include <string.h>

#include "version.h"
#include "sagan.h"
#include "sagan-defs.h"
#include "sagan-config.h"
#include "sagan-ipc.h"
#include "sagan-flowbit.h"

#ifdef HAVE_CONFIG_H
#include "config.h"             /* From autoconf */
#endif

struct _Sagan_IPC_Counters *counters_ipc;
struct _Sagan_IPC_Flowbit *flowbit_ipc;

struct _SaganConfig *config;

struct thresh_by_src_ipc *threshbysrc_ipc;
struct thresh_by_dst_ipc *threshbydst_ipc;
struct thresh_by_username_ipc *threshbyusername_ipc;

struct after_by_src_ipc *afterbysrc_ipc;
struct after_by_dst_ipc *afterbydst_ipc;
struct after_by_username_ipc *afterbyusername_ipc;

struct _SaganDebug *debug;

/*****************************************************************************
 * Sagan_IPC_Check_Object - If "counters" have been reset,   we want to
 * recreate the other objects (hence the unlink).  This function tests for
 * this case
 *****************************************************************************/

void Sagan_IPC_Check_Object(char *tmp_object_check, sbool new_counters, char *object_name)
{

    struct stat object_check;

    if ( ( stat(tmp_object_check, &object_check) == 0 ) && new_counters == 1 )
        {
            if ( unlink(tmp_object_check) == -1 )
                {
                    Sagan_Log(S_ERROR, "[%s, line %d] Could not unlink %s memory object! [%s]", __FILE__, __LINE__, object_name, strerror(errno));
                }

            Sagan_Log(S_NORMAL, "* Stale %s memory object found & unlinked.", object_name);
        }
}

/*****************************************************************************
 * Sagan_IPC_Init - Create (if needed) or map to an IPC object.
 *****************************************************************************/

void Sagan_IPC_Init(void)
{

    /* If we have a "new" counters shared memory object,  but other "old" data,  we need to remove
     * the "old" data!  The counters need to stay in sync with the other data objects! */

    sbool new_counters = 0;
    sbool new_object = 0;
    int i;

    char tmp_object_check[255];

    /* For convert 32 bit IP to octet */

    struct in_addr ip_addr_src;
    struct in_addr ip_addr_dst;

    Sagan_Log(S_NORMAL, "Initializing shared memory objects.");
    Sagan_Log(S_NORMAL, "---------------------------------------------------------------------------");

    /* Init counters first.  Need to track all other share memory objects */

    snprintf(tmp_object_check, sizeof(tmp_object_check) - 1, "%s/%s", config->ipc_directory, COUNTERS_IPC_FILE);

    if ((config->shm_counters = open(tmp_object_check, (O_CREAT | O_EXCL | O_RDWR), (S_IREAD | S_IWRITE))) > 0 )
        {
            Sagan_Log(S_NORMAL, "+ Counters shared object (new).");
            new_counters = 1;

        }

    else if ((config->shm_counters = open(tmp_object_check, (O_CREAT | O_RDWR), (S_IREAD | S_IWRITE))) < 0 )
        {
            Sagan_Log(S_ERROR, "[%s, line %d] Cannot open() for counters. [%s]", __FILE__, __LINE__, strerror(errno)); // strerror(errno));
        }
    else
        {
            Sagan_Log(S_NORMAL, "- Counters shared object (reload)");
        }


    if ( ftruncate(config->shm_counters, sizeof(_Sagan_IPC_Counters)) != 0 )
        {
            Sagan_Log(S_ERROR, "[%s, line %d] Failed to ftruncate counters. [%s]", __FILE__, __LINE__, strerror(errno));
        }

    if (( counters_ipc = mmap(0, sizeof(_Sagan_IPC_Counters), (PROT_READ | PROT_WRITE), MAP_SHARED, config->shm_counters, 0)) == MAP_FAILED )
        {
            Sagan_Log(S_ERROR, "[%s, line %d] Error allocating memory for counters object! [%s]", __FILE__, __LINE__, strerror(errno));
        }

    /* Flowbit memory object */

    snprintf(tmp_object_check, sizeof(tmp_object_check) - 1, "%s/%s", config->ipc_directory, FLOWBIT_IPC_FILE);

    Sagan_IPC_Check_Object(tmp_object_check, new_counters, "flowbit");

    if ((config->shm_flowbit = open(tmp_object_check, (O_CREAT | O_EXCL | O_RDWR), (S_IREAD | S_IWRITE))) > 0 )
        {
            Sagan_Log(S_NORMAL, "+ Flowbit shared object (new).");
            new_object=1;
        }

    else if ((config->shm_flowbit = open(tmp_object_check, (O_CREAT | O_RDWR), (S_IREAD | S_IWRITE))) < 0 )
        {
            Sagan_Log(S_ERROR, "[%s, line %d] Cannot open() for flowbit (%s)", __FILE__, __LINE__, strerror(errno));
        }

    if ( ftruncate(config->shm_flowbit, sizeof(_Sagan_IPC_Flowbit) * config->max_flowbits ) != 0 )
        {
            Sagan_Log(S_ERROR, "[%s, line %d] Failed to ftruncate flowbit. [%s]", __FILE__, __LINE__, strerror(errno));
        }

    if (( flowbit_ipc = mmap(0, sizeof(_Sagan_IPC_Flowbit) * config->max_flowbits, (PROT_READ | PROT_WRITE), MAP_SHARED, config->shm_flowbit, 0)) == MAP_FAILED )
        {
            Sagan_Log(S_ERROR, "[%s, line %d] Error allocating memory for flowbit object! [%s]", __FILE__, __LINE__, strerror(errno));
        }

    if ( new_object == 0)
        {
            Sagan_Log(S_NORMAL, "- Flowbit shared object reloaded (%d flowbits loaded / max: %d).", counters_ipc->flowbit_count, config->max_flowbits);
        }

    new_object = 0;

    if ( debug->debugipc && counters_ipc->flowbit_count >= 1 )
        {

            Sagan_Log(S_DEBUG, "");
            Sagan_Log(S_DEBUG, "*** Flowbits ***");
            Sagan_Log(S_DEBUG, "-----------------------------------------------------------------------------------------------");
            Sagan_Log(S_DEBUG, "%-2s| %-25s| %-16s| %-16s| %-20s| %s", "S", "Flowbit name", "SRC IP", "DST IP", "Date added/modified", "Expire");
            Sagan_Log(S_DEBUG, "-----------------------------------------------------------------------------------------------");


            for (i= 0; i < counters_ipc->flowbit_count; i++ )
                {

                    ip_addr_src.s_addr = htonl(flowbit_ipc[i].ip_src);
                    ip_addr_dst.s_addr = htonl(flowbit_ipc[i].ip_dst);

                    if ( flowbit_ipc[i].flowbit_state == 1 )
                        {
                            Sagan_Log(S_DEBUG, "%-2d| %-25s| %-16s| %-16s| %-20s| %d", flowbit_ipc[i].flowbit_state, flowbit_ipc[i].flowbit_name, inet_ntoa(ip_addr_src), inet_ntoa(ip_addr_dst), Sagan_u32_Time_To_Human(flowbit_ipc[i].flowbit_expire), flowbit_ipc[i].expire );
                        }

                }
            Sagan_Log(S_DEBUG, "");
        }

    /* Threshold by source */

    snprintf(tmp_object_check, sizeof(tmp_object_check) - 1, "%s/%s", config->ipc_directory, THRESH_BY_SRC_IPC_FILE);

    Sagan_IPC_Check_Object(tmp_object_check, new_counters, "thresh_by_src");

    if ((config->shm_thresh_by_src = open(tmp_object_check, (O_CREAT | O_EXCL | O_RDWR), (S_IREAD | S_IWRITE))) > 0 )
        {
            Sagan_Log(S_NORMAL, "+ Thresh_by_src shared object (new).");
            new_object=1;
        }

    else if ((config->shm_thresh_by_src = open(tmp_object_check, (O_CREAT | O_RDWR), (S_IREAD | S_IWRITE))) < 0 )
        {
            Sagan_Log(S_ERROR, "[%s, line %d] Cannot open() for thresh_by_src (%s)", __FILE__, __LINE__, strerror(errno));
        }

    if ( ftruncate(config->shm_thresh_by_src, sizeof(thresh_by_src_ipc) * config->max_threshold_by_src ) != 0 )
        {
            Sagan_Log(S_ERROR, "[%s, line %d] Failed to ftruncate thresh_by_src. [%s]", __FILE__, __LINE__, strerror(errno));
        }

    if (( threshbysrc_ipc = mmap(0, sizeof(thresh_by_src_ipc) * config->max_threshold_by_src, (PROT_READ | PROT_WRITE), MAP_SHARED, config->shm_thresh_by_src, 0)) == MAP_FAILED )
        {
            Sagan_Log(S_ERROR, "[%s, line %d] Error allocating memory for thresh_by_src object! [%s]", __FILE__, __LINE__, strerror(errno));
        }

    if ( new_object == 0)
        {
            Sagan_Log(S_NORMAL, "- Thresh_by_src shared object reloaded (%d sources loaded / max: %d).", counters_ipc->thresh_count_by_src, config->max_threshold_by_src);
        }

    new_object = 0;

    if ( debug->debugipc && counters_ipc->thresh_count_by_src >= 1 )
        {

            Sagan_Log(S_DEBUG, "");
            Sagan_Log(S_DEBUG, "*** Threshold by source ***");
            Sagan_Log(S_DEBUG, "--------------------------------------------------------------------------------------");
            Sagan_Log(S_DEBUG, "%-16s| %-11s| %-20s| %-11s| %s", "SRC IP", "Counter","Date added/modified", "SID", "Expire" );
            Sagan_Log(S_DEBUG, "--------------------------------------------------------------------------------------");

            for ( i = 0; i < counters_ipc->thresh_count_by_src; i++)
                {
                    ip_addr_src.s_addr = htonl(threshbysrc_ipc[i].ipsrc);
                    Sagan_Log(S_DEBUG, "%-16s| %-11d| %-20s| %-11s| %d", inet_ntoa(ip_addr_src), threshbysrc_ipc[i].count, Sagan_u32_Time_To_Human(threshbysrc_ipc[i].utime), threshbysrc_ipc[i].sid, threshbysrc_ipc[i].expire);
                }

            Sagan_Log(S_DEBUG, "");
        }

    /* Threshold by destination */

    snprintf(tmp_object_check, sizeof(tmp_object_check) - 1, "%s/%s", config->ipc_directory, THRESH_BY_DST_IPC_FILE);

    Sagan_IPC_Check_Object(tmp_object_check, new_counters, "thresh_by_dst");

    if ((config->shm_thresh_by_dst = open(tmp_object_check, (O_CREAT | O_EXCL | O_RDWR), (S_IREAD | S_IWRITE))) > 0 )
        {
            Sagan_Log(S_NORMAL, "+ Thresh_by_dst shared object (new).");
            new_object=1;
        }

    else if ((config->shm_thresh_by_dst = open(tmp_object_check, (O_CREAT | O_RDWR), (S_IREAD | S_IWRITE))) < 0 )
        {
            Sagan_Log(S_ERROR, "[%s, line %d] Cannot open() for thresh_by_dst (%s)", __FILE__, __LINE__, strerror(errno));
        }

    if ( ftruncate(config->shm_thresh_by_dst, sizeof(thresh_by_dst_ipc) * config->max_threshold_by_dst) != 0 )
        {
            Sagan_Log(S_ERROR, "[%s, line %d] Failed to ftruncate thresh_by_dst. [%s]", __FILE__, __LINE__, strerror(errno));
        }

    if (( threshbydst_ipc = mmap(0, sizeof(thresh_by_dst_ipc) * config->max_threshold_by_dst, (PROT_READ | PROT_WRITE), MAP_SHARED, config->shm_thresh_by_dst, 0)) == MAP_FAILED )
        {
            Sagan_Log(S_ERROR, "[%s, line %d] Error allocating memory for thresh_by_dst object! [%s]", __FILE__, __LINE__, strerror(errno));
        }

    if ( new_object == 0)
        {
            Sagan_Log(S_NORMAL, "- Thresh_by_dst shared object reloaded (%d destinations loaded / max: %d).", counters_ipc->thresh_count_by_dst, config->max_threshold_by_dst);
        }

    new_object = 0;

    if ( debug->debugipc && counters_ipc->thresh_count_by_dst >= 1 )
        {

            Sagan_Log(S_DEBUG, "");
            Sagan_Log(S_DEBUG, "*** Threshold by destination ***");
            Sagan_Log(S_DEBUG, "--------------------------------------------------------------------------------------");
            Sagan_Log(S_DEBUG, "%-16s| %-11s| %-20s| %-11s| %s", "DST IP", "Counter","Date added/modified", "SID", "Expire" );
            Sagan_Log(S_DEBUG, "--------------------------------------------------------------------------------------");

            for ( i = 0; i < counters_ipc->thresh_count_by_dst; i++)
                {
                    ip_addr_dst.s_addr = htonl(threshbydst_ipc[i].ipdst);
                    Sagan_Log(S_DEBUG, "%-16s| %-11d| %-20s| %-11s| %d", inet_ntoa(ip_addr_dst), threshbydst_ipc[i].count, Sagan_u32_Time_To_Human(threshbydst_ipc[i].utime), threshbydst_ipc[i].sid, threshbydst_ipc[i].expire);

                }

            Sagan_Log(S_DEBUG, "");
        }


    /* Threshold by username */

    snprintf(tmp_object_check, sizeof(tmp_object_check) - 1, "%s/%s", config->ipc_directory, THRESH_BY_USERNAME_IPC_FILE);

    Sagan_IPC_Check_Object(tmp_object_check, new_counters, "thresh_by_username");

    if ((config->shm_thresh_by_username = open(tmp_object_check, (O_CREAT | O_EXCL | O_RDWR), (S_IREAD | S_IWRITE))) > 0 )
        {
            Sagan_Log(S_NORMAL, "+ Thresh_by_username shared object (new).");
            new_object=1;
        }

    else if ((config->shm_thresh_by_username = open(tmp_object_check, (O_CREAT | O_RDWR), (S_IREAD | S_IWRITE))) < 0 )
        {
            Sagan_Log(S_ERROR, "[%s, line %d] Cannot open() for thresh_by_username (%s)", __FILE__, __LINE__, strerror(errno));
        }

    if ( ftruncate(config->shm_thresh_by_username, sizeof(thresh_by_username_ipc) * config->max_threshold_by_username ) != 0 )
        {
            Sagan_Log(S_ERROR, "[%s, line %d] Failed to ftruncate thresh_by_username. [%s]", __FILE__, __LINE__, strerror(errno));
        }

    if (( threshbyusername_ipc = mmap(0, sizeof(thresh_by_username_ipc) * config->max_threshold_by_username, (PROT_READ | PROT_WRITE), MAP_SHARED, config->shm_thresh_by_username, 0)) == MAP_FAILED )
        {
            Sagan_Log(S_ERROR, "[%s, line %d] Error allocating memory for thresh_by_username object! [%s]", __FILE__, __LINE__, strerror(errno));
        }

    if ( new_object == 0 )
        {
            Sagan_Log(S_NORMAL, "- Thresh_by_username shared object reloaded (%d usernames loaded / max: %d).", counters_ipc->thresh_count_by_username, config->max_threshold_by_username);
        }

    new_object = 0;

    if ( debug->debugipc && counters_ipc->thresh_count_by_username >= 1 )
        {
            Sagan_Log(S_DEBUG, "");
            Sagan_Log(S_DEBUG, "*** Threshold by username ***");
            Sagan_Log(S_DEBUG, "--------------------------------------------------------------------------------------");
            Sagan_Log(S_DEBUG, "%-16s| %-11s| %-20s| %-11s| %s", "Username", "Counter","Date added/modified", "SID", "Expire" );
            Sagan_Log(S_DEBUG, "--------------------------------------------------------------------------------------");

            for ( i = 0; i < counters_ipc->thresh_count_by_username; i++)
                {
                    Sagan_Log(S_DEBUG, "%-16s| %-11d| %-20s| %-11s| %d", threshbyusername_ipc[i].username, threshbyusername_ipc[i].count, Sagan_u32_Time_To_Human(threshbyusername_ipc[i].utime), threshbyusername_ipc[i].sid, threshbyusername_ipc[i].expire);
                }
        }

    /* After by source */

    snprintf(tmp_object_check, sizeof(tmp_object_check) - 1, "%s/%s", config->ipc_directory, AFTER_BY_SRC_IPC_FILE);

    Sagan_IPC_Check_Object(tmp_object_check, new_counters, "after_by_src");

    if ((config->shm_after_by_src = open(tmp_object_check, (O_CREAT | O_EXCL | O_RDWR), (S_IREAD | S_IWRITE))) > 0 )
        {
            Sagan_Log(S_NORMAL, "+ After_by_src shared object (new).");
            new_object=1;
        }

    else if ((config->shm_after_by_src = open(tmp_object_check, (O_CREAT | O_RDWR), (S_IREAD | S_IWRITE))) < 0 )
        {
            Sagan_Log(S_ERROR, "[%s, line %d] Cannot open() for after_by_src (%s)", __FILE__, __LINE__, strerror(errno));
        }

    if ( ftruncate(config->shm_after_by_src, sizeof(after_by_src_ipc) * config->max_after_by_src ) != 0 )
        {
            Sagan_Log(S_ERROR, "[%s, line %d] Failed to ftruncate after_by_src. [%s]", __FILE__, __LINE__, strerror(errno));
        }

    if (( afterbysrc_ipc = mmap(0, sizeof(after_by_src_ipc) * config->max_after_by_src, (PROT_READ | PROT_WRITE), MAP_SHARED, config->shm_after_by_src, 0)) == MAP_FAILED )
        {
            Sagan_Log(S_ERROR, "[%s, line %d] Error allocating memory for after_by_src object! [%s]", __FILE__, __LINE__, strerror(errno));
        }

    if ( new_object == 0 )
        {
            Sagan_Log(S_NORMAL, "- After_by_src shared object reloaded (%d sources loaded / max: %d).", counters_ipc->after_count_by_src, config->max_after_by_src);
        }

    new_object = 0;

    if ( debug->debugipc && counters_ipc->after_count_by_src >= 1 )
        {

            Sagan_Log(S_DEBUG, "");
            Sagan_Log(S_DEBUG, "*** After by source ***");
            Sagan_Log(S_DEBUG, "--------------------------------------------------------------------------------------");
            Sagan_Log(S_DEBUG, "%-16s| %-11s| %-20s| %-11s| %s", "SRC IP", "Counter","Date added/modified", "SID", "Expire" );
            Sagan_Log(S_DEBUG, "--------------------------------------------------------------------------------------");

            for ( i = 0; i < counters_ipc->after_count_by_src; i++ )
                {
                    ip_addr_src.s_addr = htonl(afterbysrc_ipc[i].ipsrc);
                    Sagan_Log(S_DEBUG, "%-16s| %-11d| %-20s| %-11s| %d", inet_ntoa(ip_addr_src), afterbysrc_ipc[i].count, Sagan_u32_Time_To_Human(afterbysrc_ipc[i].utime), afterbysrc_ipc[i].sid, afterbysrc_ipc[i].expire);
                }

            Sagan_Log(S_DEBUG, "");
        }

    /* After by destination */

    snprintf(tmp_object_check, sizeof(tmp_object_check) - 1, "%s/%s", config->ipc_directory, AFTER_BY_DST_IPC_FILE);

    Sagan_IPC_Check_Object(tmp_object_check, new_counters, "after_by_dst");

    if ((config->shm_after_by_dst = open(tmp_object_check, (O_CREAT | O_EXCL | O_RDWR), (S_IREAD | S_IWRITE))) > 0 )
        {
            Sagan_Log(S_NORMAL, "+ After_by_dst shared object (new).");
            new_object=1;
        }

    else if ((config->shm_after_by_dst = open(tmp_object_check, (O_CREAT | O_RDWR), (S_IREAD | S_IWRITE))) < 0 )
        {
            Sagan_Log(S_ERROR, "[%s, line %d] Cannot open() for after_by_dst (%s)", __FILE__, __LINE__, strerror(errno));
        }

    if ( ftruncate(config->shm_after_by_dst, sizeof(after_by_dst_ipc) * config->max_after_by_dst) != 0 )
        {
            Sagan_Log(S_ERROR, "[%s, line %d] Failed to ftruncate after_by_dst. [%s]", __FILE__, __LINE__, strerror(errno));
        }

    if (( afterbydst_ipc = mmap(0, sizeof(after_by_dst_ipc) * config->max_after_by_dst, (PROT_READ | PROT_WRITE), MAP_SHARED, config->shm_after_by_dst, 0)) == MAP_FAILED )
        {
            Sagan_Log(S_ERROR, "[%s, line %d] Error allocating memory for after_by_src object! [%s]", __FILE__, __LINE__, strerror(errno));
        }

    if ( new_object == 0 )
        {
            Sagan_Log(S_NORMAL, "- After_by_dst shared object reloaded (%d destinations loaded / max: %d).", counters_ipc->after_count_by_dst, config->max_after_by_dst);
        }

    new_object = 0;

    if ( debug->debugipc && counters_ipc->after_count_by_dst >= 1 )
        {
            Sagan_Log(S_DEBUG, "");
            Sagan_Log(S_DEBUG, "*** After by destination ***");
            Sagan_Log(S_DEBUG, "--------------------------------------------------------------------------------------");
            Sagan_Log(S_DEBUG, "%-16s| %-11s| %-20s| %-11s| %s", "DST IP", "Counter","Date added/modified", "SID", "Expire" );
            Sagan_Log(S_DEBUG, "--------------------------------------------------------------------------------------");

            for ( i = 0; i < counters_ipc->after_count_by_dst; i++)
                {
                    ip_addr_dst.s_addr = htonl(afterbydst_ipc[i].ipdst);
                    Sagan_Log(S_DEBUG, "%-16s| %-11d| %-20s| %-11s| %d", inet_ntoa(ip_addr_dst), afterbydst_ipc[i].count, Sagan_u32_Time_To_Human(afterbydst_ipc[i].utime), afterbydst_ipc[i].sid, afterbydst_ipc[i].expire);
                }

            Sagan_Log(S_DEBUG, "");
        }

    /* After by username */

    snprintf(tmp_object_check, sizeof(tmp_object_check) - 1, "%s/%s", config->ipc_directory, AFTER_BY_USERNAME_IPC_FILE);

    Sagan_IPC_Check_Object(tmp_object_check, new_counters, "after_by_username");

    if ((config->shm_after_by_username = open(tmp_object_check, (O_CREAT | O_EXCL | O_RDWR), (S_IREAD | S_IWRITE))) > 0 )
        {
            Sagan_Log(S_NORMAL, "+ After_by_username shared object (new).");
            new_object=1;
        }

    else if ((config->shm_after_by_username = open(tmp_object_check, (O_CREAT | O_RDWR), (S_IREAD | S_IWRITE))) < 0 )
        {
            Sagan_Log(S_ERROR, "[%s, line %d] Cannot open() for after_by_username (%s)", __FILE__, __LINE__, strerror(errno));
        }

    if ( ftruncate(config->shm_after_by_username, sizeof(after_by_username_ipc) * config->max_after_by_username ) != 0 )
        {
            Sagan_Log(S_ERROR, "[%s, line %d] Failed to ftruncate after_by_username. [%s]", __FILE__, __LINE__, strerror(errno));
        }

    if (( afterbyusername_ipc = mmap(0, sizeof(after_by_username_ipc) * config->max_after_by_username, (PROT_READ | PROT_WRITE), MAP_SHARED, config->shm_after_by_username, 0)) == MAP_FAILED )
        {
            Sagan_Log(S_ERROR, "[%s, line %d] Error allocating memory for after_by_src object! [%s]", __FILE__, __LINE__, strerror(errno));
        }

    if ( new_object == 0 )
        {
            Sagan_Log(S_NORMAL, "- After_by_username shared object reloaded (%d usernames loaded / max: %d).", counters_ipc->after_count_by_username, config->max_after_by_username);
        }

    new_object = 0;

    if ( debug->debugipc && counters_ipc->after_count_by_username >= 1 )
        {
            Sagan_Log(S_DEBUG, "");
            Sagan_Log(S_DEBUG, "*** After by username ***");
            Sagan_Log(S_DEBUG, "--------------------------------------------------------------------------------------");
            Sagan_Log(S_DEBUG, "%-16s| %-11s| %-20s| %-11s| %s", "Username", "Counter","Date added/modified", "SID", "Expire" );
            Sagan_Log(S_DEBUG, "--------------------------------------------------------------------------------------");

            for ( i = 0; i < counters_ipc->after_count_by_username; i++)
                {
                    Sagan_Log(S_DEBUG, "%-16s| %-11d| %-20s| %-11s| %d", afterbyusername_ipc[i].username, afterbyusername_ipc[i].count, Sagan_u32_Time_To_Human(afterbyusername_ipc[i].utime), afterbyusername_ipc[i].sid, afterbyusername_ipc[i].expire);
                }

            Sagan_Log(S_DEBUG, "");
        }

}