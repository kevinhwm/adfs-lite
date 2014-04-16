#include "nxweb/nxweb.h"
#include <stdio.h>
#include <kclangc.h>

#include <unistd.h>

// Note: see config.h for most nxweb #defined parameters

#define NXWEB_LISTEN_HOST_AND_PORT ":8055"
#define NXWEB_LISTEN_HOST_AND_PORT_SSL ":8056"

#define NXWEB_DEFAULT_CHARSET "utf-8"
#define NXWEB_DEFAULT_INDEX_FILE "index.htm"

// All paths are relative to working directory:
#define SSL_CERT_FILE "ssl/server_cert.pem"
#define SSL_KEY_FILE "ssl/server_key.pem"
#define SSL_DH_PARAMS_FILE "ssl/dh.pem"

#define SSL_PRIORITIES "NORMAL:+VERS-TLS-ALL:+COMP-ALL:-CURVE-ALL:+CURVE-SECP256R1"

// Setup modules & handlers (this can be done from any file linked to nxweb):

extern nxweb_handler hello_handler;
extern nxweb_handler benchmark_handler;
extern nxweb_handler benchmark_handler_inworker;
extern nxweb_handler test_handler;
extern nxweb_handler sendfile_handler;

extern nxweb_handler upload_file_handler;
extern nxweb_handler download_handler;
extern nxweb_handler fetch_handler;
extern nxweb_handler delete_handler;
extern nxweb_handler delete_batch_handler;
extern nxweb_handler clean_handler;
extern nxweb_handler status_handler;
extern nxweb_handler list_handler;


KCDB* g_kcdb;
int RECORD_HEADER_LEN = 4;
int MAX_UPLOAD_SIZE = 80; 

// These are benchmarking handlers (see modules/benchmark.c):
NXWEB_SET_HANDLER(benchmark, "/benchmark-inprocess", &benchmark_handler, .priority=100);
NXWEB_SET_HANDLER(benchmark_inworker, "/benchmark-inworker", &benchmark_handler_inworker, .priority=100);
NXWEB_SET_HANDLER(test, "/test", &test_handler, .priority=900);

// This is sample handler (see modules/hello.c):
NXWEB_SET_HANDLER(hello, "/hello", &hello_handler, .priority=1000, .filters={
#ifdef WITH_ZLIB
	&gzip_filter
#endif
	});

// This is sample handler (see modules/upload.c):
NXWEB_SET_HANDLER(upload, "/upload_file", &upload_file_handler, .priority=1000);
//NXWEB_SET_HANDLER(upload_keyvalue, "/upload_data", &upload_file_handler, .priority=1000);
NXWEB_SET_HANDLER(download, "/download", &download_handler, .priority=1000);
NXWEB_SET_HANDLER(fetch, "/fetch", &fetch_handler, .priority=1000);
NXWEB_SET_HANDLER(delete, "/delete", &delete_handler, .priority=1000);
NXWEB_SET_HANDLER(deletefiles, "/deletefiles", &delete_batch_handler, .priority=1000);
NXWEB_SET_HANDLER(clean, "/clean", &clean_handler, .priority=1000);
NXWEB_SET_HANDLER(status, "/status", &status_handler, .priority=1000);
NXWEB_SET_HANDLER(list, "/list", &list_handler, .priority=1000);

// This proxies requests to backend with index 0 (see proxy setup further below):
NXWEB_SET_HANDLER(backend1, "/backend1", &nxweb_http_proxy_handler, .priority=10000, .idx=0, .uri="",
	.filters={ &file_cache_filter, &templates_filter, &ssi_filter },
	.file_cache_dir="www/cache/proxy");

// This proxies requests to backend with index 1 (see proxy setup further below):
NXWEB_SET_HANDLER(backend2, "/backend2", &nxweb_http_proxy_handler, .priority=10000, .idx=1, .uri="",
	.filters={ &file_cache_filter, &templates_filter, &ssi_filter },
	.file_cache_dir="www/cache/proxy");

// This serves static files from $(work_dir)/www/root directory:
NXWEB_SET_HANDLER(sendfile, 0, &sendfile_handler, .priority=900000,
	.filters={
	&templates_filter,
	&ssi_filter,
#ifdef WITH_IMAGEMAGICK
	&image_filter,
#endif
#ifdef WITH_ZLIB
	&gzip_filter
#endif
	}, .dir="www/root",
	.charset=NXWEB_DEFAULT_CHARSET, .index_file=NXWEB_DEFAULT_INDEX_FILE,
	.gzip_dir="www/cache/gzip", .img_dir="www/cache/img", .cache=1);


// Command-line options:
static const char* user_name=0;
static const char* group_name=0;
static int port=8055;
static int ssl_port=8056;


// Server main():

static void server_main() 
{
    // Bind listening interfaces:
    char host_and_port[32];
    snprintf(host_and_port, sizeof(host_and_port), ":%d", port);
    if (nxweb_listen(host_and_port, 4096)) return; // simulate normal exit so nxweb is not respawned
#ifdef WITH_SSL
    char ssl_host_and_port[32];
    snprintf(ssl_host_and_port, sizeof(ssl_host_and_port), ":%d", ssl_port);
    if (nxweb_listen_ssl(ssl_host_and_port, 1024, 1, SSL_CERT_FILE, SSL_KEY_FILE, SSL_DH_PARAMS_FILE, SSL_PRIORITIES)) return; 
    // simulate normal exit so nxweb is not respawned
#endif // WITH_SSL

    // Drop privileges:
    if (nxweb_drop_privileges(group_name, user_name)==-1) return;

    // Override default timers (if needed):
    // make sure the timeout less than 5 seconds. be sure to void receive SIGALRM to shutdown the db brute.
    //
    // alarm is set to be 60000000
    nxweb_set_timeout(NXWEB_TIMER_KEEP_ALIVE, 30000000);

    // Go!
    nxweb_run();

    if( g_kcdb) {
	kcdbclose( g_kcdb );
	g_kcdb = NULL;
	printf("db closed.\n");
    }
}

// Utility stuff:
static void show_help(void) 
{
    printf( "usage:    adfslite <options>\n\n"
	    " -d       run as daemon\n"
	    " -s       shutdown adfslite\n"
	    " -m mem   set memory map size in MB (default: 64)\n"
	    " -M fMax  set file max size in MB   (default: 80)\n"
	    " -w dir   set work dir    (default: ./)\n"
	    " -l file  set log file    (default: stderr or adfslite.log for daemon)\n"
	    " -p file  set pid file    (default: adfslite.pid)\n"
	    //" -u user  set process uid\n"
	    //" -g group set process gid\n"
	    " -P port  set http port\n"
	    " -x file  database file   (default: ./store.kch)\n"
#ifdef WITH_SSL
	    " -S port  set https port\n"
#endif
	    " -h       show this help\n"
	    " -v       show version\n"
	    "\n"
	    "example:  adfslite -w . -d -l adfslite.log\n\n"
	  );
}

void _dfs_exit()
{
    if( g_kcdb) {
	kcdbclose( g_kcdb );
	g_kcdb = NULL;
	printf("db closed.\n");
    }
}

int main(int argc, char** argv) 
{
    int daemon=0;
    int shutdown=0;
    const char* work_dir=0;
    const char* log_file=0;
    const char* pid_file="adfslite.pid";
    const char* dbpath="./store.kch";

    size_t mem_size = 64;

    int c;
    while ((c=getopt(argc, argv, ":hvdsm:M:w:l:p:u:g:P:x:S:"))!=-1) {
	switch (c) {
	    case 'h':
		show_help();
		return 0;
	    case 'v':
		printf( "VERSION:      adfslite - 3.2.9\n"
			"BUILD-DATE:   "__DATE__ " " __TIME__"\n" );
		return 0;
	    case 'd':
		daemon=1;
		break;
	    case 's':
		shutdown=1;
		break;
	    case 'm':
		mem_size = atoi(optarg);
		break;
	    case 'M':
		MAX_UPLOAD_SIZE = atoi(optarg);
		break;
	    case 'w':
		work_dir=optarg;
		break;
	    case 'l':
		log_file=optarg;
		break;
	    case 'p':
		pid_file=optarg;
		break;
	    case 'u':
		user_name=optarg;
		break;
	    case 'g':
		group_name=optarg;
		break;
	    case 'x':
		dbpath = optarg;
		break;
	    case 'P':
		port=atoi(optarg);
		if (port<=0) {
		    fprintf(stderr, "invalid port: %s\n\n", optarg);
		    return EXIT_FAILURE;
		}
		break;
	    case 'S':
		ssl_port=atoi(optarg);
		if (ssl_port<=0) {
		    fprintf(stderr, "invalid ssl port: %s\n\n", optarg);
		    return EXIT_FAILURE;
		}
		break;
	    case '?':
		fprintf(stderr, "unkown option: -%c\n\n", optopt);
		show_help();
		return EXIT_FAILURE;
	}
    }

    if ((argc-optind)>0) {
	fprintf(stderr, "too many arguments\n\n");
	show_help();
	return EXIT_FAILURE;
    }

    if (shutdown) {
	nxweb_shutdown_daemon(work_dir, pid_file);
	return EXIT_SUCCESS;
    }

    // using kchashdb to store files.
    g_kcdb = kcdbnew();
    char path[1024] = {0};
    mem_size *= 1024*1024;
    MAX_UPLOAD_SIZE *= 1024*1024;
    sprintf(path, "%s#apow=%d#fpow=%d#msiz=%lu#dfunit=%d", dbpath, 10, 10, mem_size, 8);

    int32_t succ = kcdbopen( g_kcdb, path, KCOWRITER|KCOCREATE);
    if( succ ) {printf("using dbpath :%s\n", dbpath );}
    else{
	printf("db open failed:%s\n", dbpath );
	exit(-1);
    }

    if (daemon) {
	if (!log_file) {log_file="alcore.log";}
	nxweb_run_daemon(work_dir, log_file, pid_file, server_main);
    }
    else { nxweb_run_normal(work_dir, log_file, pid_file, server_main); }
    return EXIT_SUCCESS;
}

