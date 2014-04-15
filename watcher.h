/*
  watcher : watch and rebooting command

  usage : watcher [ -h ] [ -t #.# ] [ -s # ] [ -f # ] command arg1 arg2 ...

    -h         : show help
    -t #t.#s   : if command terminate #t count in #s second,
                 send log message. ( default is 10count / 10sec )
    -f #       : set syslog facility LOCAL# ( # = 0-7 )
    -l logfile : write stdout/stderr message to logfile.
    -p pidfile : write PID to logfile.
    -s #       : set sleep time # second.
    --         : end marker.
 */
#include <stdio.h>
#include <time.h>

#define DEFAULT_REGION 10 /* 10sec */
#define DEFAULT_COUNT  10 /* 10count  */
#define DEFAULT_SLEEP  30 /* 30sec */


struct watcher_conf {
    struct {
        time_t  region;
        int     count ;
    } alert;
    struct {
        int facility;
        int level;  
    } syslog;

    int uid ; int gid ;

    time_t  sleeptime ;

    char  *logfile  ;
    char  *pidfile  ;
    char  *progname ;
    int    argc;
    char  *argv[4];
};

struct watcher_state {
    int    wstatus;
    int    length; /* count x 3 */
    int    last_slot;
    time_t crashtimes[1]; 
};

