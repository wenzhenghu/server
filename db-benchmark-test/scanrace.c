/* Scan the bench.tokudb/bench.db over and over. */

#include <toku_portability.h>
#include <assert.h>
#include <db.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef TOKUDB
#error requires tokudb
#endif

static const char *pname;
static long limitcount=-1;
static u_int32_t cachesize = 4*1024*1024;

#define STRINGIFY2(s) #s
#define STRINGIFY(s) STRINGIFY2(s)
const char *dbdir = "./bench."  STRINGIFY(DIRSUF); /* DIRSUF is passed in as a -D argument to the compiler. */


static void parse_args (int argc, const char *argv[]) {
    pname=argv[0];
    argc--;
    argv++;
    while (argc>0) {
	if (strcmp(*argv, "--count")==0)            {
	    char *end;
	    argv++; argc--;
	    errno=0; limitcount=strtol(*argv, &end, 10); assert(errno==0);
	    printf("Limiting count to %ld\n", limitcount);
        } else if (strcmp(*argv, "--cachesize")==0 && argc>0) {
            char *end;
            argv++; argc--;
            cachesize=(u_int32_t)strtol(*argv, &end, 10);
	} else if (strcmp(*argv, "--env") == 0) {
	    argv++; argc--;
	    if (argc <= 0) goto print_usage;
	    dbdir = *argv;
	} else {
	print_usage:
	    fprintf(stderr, "Usage:\n%s\n", pname);
	    fprintf(stderr, "  --count <count>     read the first COUNT rows and then  stop.\n");
            fprintf(stderr, "  --cachesize <n>     set the env cachesize to <n>\n");
	    fprintf(stderr, "  --env DIR\n");
            exit(1);
	}
	argc--;
	argv++;
    }
}


DB_ENV *env;
DB *db;
DB_TXN *tid=0;

static int env_open_flags_yesx = DB_CREATE|DB_PRIVATE|DB_INIT_MPOOL|DB_INIT_TXN|DB_INIT_LOG|DB_INIT_LOCK;

char *dbfilename = "bench.db";

static void scanrace_setup (void) {
    int r;
    r = db_env_create(&env, 0);                                                           assert(r==0);
    r = env->set_cachesize(env, 0, cachesize, 1);                                         assert(r==0);
    r = env->open(env, dbdir, env_open_flags_yesx, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);      assert(r==0);
    r = db_create(&db, env, 0);                                                           assert(r==0);
    r = env->txn_begin(env, 0, &tid, 0);                                              assert(r==0);
    r = db->open(db, tid, dbfilename, NULL, DB_BTREE, 0, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);                           assert(r==0);
    r = db->pre_acquire_read_lock(db,
				  tid,
				  db->dbt_neg_infty(), db->dbt_neg_infty(),
				  db->dbt_pos_infty(), db->dbt_pos_infty());
    assert(r==0);
}

static void scanrace_shutdown (void) {
    int r;
    r = db->close(db, 0);                                       assert(r==0);
    r = tid->commit(tid, 0);                                    assert(r==0);
    r = env->close(env, 0);                                     assert(r==0);

#if 0
    {
	extern int toku_os_get_max_rss(int64_t*);
        int64_t mrss;
        int r = toku_os_get_max_rss(&mrss);
        assert(r==0);
	printf("maxrss=%.2fMB\n", mrss/256.0);
    }
#endif
}

static double gettime (void) {
    struct timeval tv;
    int r = gettimeofday(&tv, 0);
    assert(r==0);
    return tv.tv_sec + 1e-6*tv.tv_usec;
}

struct extra_count {
    long long totalbytes;
    int rowcounter;
};

static int
counttotalbytes (DBT const *key, DBT const *data, void *extrav) {
    struct extra_count *e=extrav;
    e->totalbytes += key->size + data->size;
    e->rowcounter++;
    return 0;
}

static void scanrace_lwc (void) {
    int r;
    int counter=0;
    for (counter=0; counter<2; counter++) {
	struct extra_count e = {0,0};
	double prevtime = gettime();
	DBC *dbc;
	r = db->cursor(db, tid, &dbc, 0);                           assert(r==0);
	long rowcounter=0;
	while (0 == (r = dbc->c_getf_next(dbc, DB_PRELOCKED, counttotalbytes, &e))) {
	    if (rowcounter%128==0) { printf("."); fflush(stdout); }
	    rowcounter++;
	    if (limitcount>0 && rowcounter>=limitcount) break;
	}
	r = dbc->c_close(dbc);                                      assert(r==0);
	double thistime = gettime();
	double tdiff = thistime-prevtime;
	printf("LWC Scan %lld bytes (%d rows) in %9.6fs at %9fMB/s\n", e.totalbytes, e.rowcounter, tdiff, 1e-6*e.totalbytes/tdiff);
    }
}
  
int main (int argc, const char *argv[]) {

    parse_args(argc,argv);

    scanrace_setup();
    scanrace_lwc();
    scanrace_shutdown();

#if defined __linux__ && __linux__
    char fname[256];
    sprintf(fname, "/proc/%d/status", toku_os_getpid());
    FILE *f = fopen(fname, "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof line, f)) {
            int n;
            if (sscanf(line, "VmPeak: %d", &n) || sscanf(line, "VmHWM: %d", &n) || sscanf(line, "VmRSS: %d", &n))
                fputs(line, stdout);
        }
        fclose(f);
    }
#endif
    return 0;
}
