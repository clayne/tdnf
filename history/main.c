/*
 * Copyright (C) 2022 VMware, Inc. All Rights Reserved.
 *
 * Licensed under the GNU Lesser General Public License v2.1 (the "License");
 * you may not use this file except in compliance with the License. The terms
 * of the License are located in the COPYING file of this distribution.
 */

#include <stdio.h>
#include <time.h>
/* for O_RDONLY */
#include <fcntl.h>
#include <getopt.h>

#include <sqlite3.h>

#include <rpm/rpmlib.h>
#include <rpm/rpmdb.h>
#include <rpm/rpmlog.h>
#include <rpm/rpmps.h>
#include <rpm/rpmts.h>
#include <rpm/rpmdb.h>

#include "history.h"

#define ERR_CMDLINE     1
#define ERR_SYSTEM      2
#define ERR_RPMTS       3

#define pr_err(fmt, ...) \
    fprintf(stderr, fmt, ##__VA_ARGS__)

#define fail(_rc, fmt, ...) { \
    rc = _rc; \
    pr_err(fmt, ##__VA_ARGS__); \
    goto error; \
}

#define check_cond(COND) if(!(COND)) { \
    pr_err("check_cond failed in %s line %d\n", \
        __FUNCTION__, __LINE__); \
    rc = -1; \
    ((void)(rc)); /* suppress "set but not used" warning */ \
    goto error; \
}

#define check_ptr(ptr) if(!(ptr)) { \
    pr_err("check_ptr failed in %s line %d\n", \
        __FUNCTION__, __LINE__); \
    rc = -1; \
    ((void)(rc)); /* suppress "set but not used" warning */ \
    goto error; \
}

#define check_rc(rc) if((rc) != 0) { \
    pr_err("check_rc failed in %s line %d\n", \
        __FUNCTION__, __LINE__); \
    goto error; \
}

#define safe_free(ptr) { if ((ptr) != NULL) { free(ptr); ptr = NULL; }}


void usage(const char *cmdname)
{
    printf("tdnf history db utility\n\n");
    printf("Usage:\n\n");
    printf("%s [-f dbfile] [-r rootdir] init|update\n", cmdname);
    printf("%s [-f dbfile] mark install|remove [pkg[...]]\n", cmdname);
    printf("\n");
    printf("Commands:\n\n");
    printf("init   - Initialize the history db.\n");
    printf("mark   - Mark a package as user installed ('install') or auto installed ('remove').\n");
    printf("update - Update the history db using the current rpm db.\n");
    printf("\n");
}

int main(int argc, char *argv[])
{
    char *db_file = HISTORY_DB_DIR"/"HISTORY_DB_FILE;
    char *rpm_root_dir = "/";
    rpmts ts = NULL;
    struct history_ctx *ctx = NULL;
    int rc = 0;

    while(1) {
        int c;

        static struct option long_options[] = {
            {"file", 1, 0, 'f'},
            {"rootdir", 1, 0, 'r'},
            {0, 0, 0, 0}
        };

        c = getopt_long(argc, argv, "f:r:",
            long_options, NULL);

        if (c == -1)
            break;

        switch(c){
        case 'f':
            db_file = optarg;
            break;
        case 'r':
            rpm_root_dir = optarg;
            break;
        case '?':
        default:
            /* If it's an error, getopt has already produced an error message. */
            usage(argv[0]);
            return 1;
        }
    }

    rpmReadConfigFiles(NULL, NULL);

    ts = rpmtsCreate();
    check_ptr(ts);

    if(rpmtsSetRootDir(ts, rpm_root_dir)) {
        fail(ERR_RPMTS, "could not set rpm root dir\n");
    }

    if(rpmtsOpenDB(ts, O_RDONLY)) {
        fail(ERR_RPMTS, "could not open rpmdb\n");
    }

    ctx = create_history_ctx(db_file);
    check_ptr(ctx);

   /*
    * Process the action(s).
    */
   if (optind < argc) {
       int argcount = 0;
       char *action = NULL;

       while (optind + argcount < argc)
           argcount++;

       /*
       * Find the action.
       */
       action = argv[optind];
       if(strcmp(action, "init") == 0 || strcmp(action, "update") == 0) {
           rc = history_sync(ctx, ts);
           check_rc(rc);
       } else if(strcmp(action, "mark") == 0) {
           char *subaction = NULL;
           int flag = 0;
           int i;

           if (argcount < 2) {
               usage(argv[0]);
               fail(ERR_CMDLINE, "expected 'remove' or 'install'\n");
           }
           subaction = argv[optind+1];
           if (strcmp(subaction, "remove") == 0) {
               flag = 1;
           } else if(strcmp(subaction, "install") == 0) {
               flag = 0;
           } else {
               usage(argv[0]);
               fail(ERR_CMDLINE, "unknown sub command '%s'\n", subaction);
           }
           for (i = optind + 2; argv[i]; i++) {
               rc = history_set_auto_flag(ctx, argv[i], flag);
               check_rc(rc);
           }
       } else {
           usage(argv[0]);
           fail(ERR_CMDLINE, "unknown command '%s'\n", action);
       }
   } else {
       usage(argv[0]);
       fail(ERR_CMDLINE, "command expected\n");
   }
error:
    if (ctx)
        destroy_history_ctx(ctx);
    if (ts) {
        rpmtsCloseDB(ts);
        rpmtsFree(ts);
    }
    exit(rc);
}
