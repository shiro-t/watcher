/*
 * watcher.c : process watcher and restarter.
 *
 * Copyright(c)2001 SHIROYAMA Takayuki <shiro@installer.org>
 *
 * Version.1.0 : Dec 23, 2001.
 * Version.2.0 : Aug 21, 2003. logging support.
 * Version.2.1 : Aug 21, 2003. uid change ( only run as root. ).
 * Version.2.2 : Sep 15, 2003. fix argv copy bug.
 * Version.2.4 : May 21, 2004. make pid file.
 * Version.2.5 : Jul  5, 2004. fix leak long stdout message.
 *                             check pidfile.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Emacs; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 */
static char *__watcher_version = "2.05" ;

#include "watcher.h"
#include "progname.h"
#include <stdio.h>
#include <syslog.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>


/* default values */
static const struct watcher_conf default_conf = {
    { DEFAULT_REGION, DEFAULT_COUNT },     /* alert time  */
    { LOG_LOCAL0,     LOG_ERR },           /* syslog      */
    -1, -1,                                /* uid and gid */
    DEFAULT_SLEEP,                         /* sleeptime   */
    NULL,                                  /* progname    */
    NULL,                                  /* logfile     */
    NULL,                                  /* pidfile     */
    0,                                     /* argc        */
    "/usr/bin/true", NULL, NULL, NULL,     /* argv[0..4]  */
};

static int childpid = 0;
static int motherpid = 0;
static int sigchildflag = 0;
static int execerrcount = 0;

#ifdef DEBUG
static int debugmode  = 1;
#else
static int debugmode  = 0;
#endif
/*
 * show help message and exit. 
 *    exval : exit value
 */
void show_help( int exval )
{
    extern char *__progname;
    fprintf( stderr, "%s : watch and restarting application. version %s.\n" 
                     "\n"
                     "usage : %s [ -h ] [ -t #.# ] [ -s # ] [ -u # ] [ -g # ][ -f # ] [ -- ] command arg1 arg2 ...\n" 
                     "\t -h         : show this help ( and terminate. )\n" 
                     "\t -t #t.#s   : if command terminate #t count in #s second,\n"
                     "\t              send log message.\n"
                     "\t -k #t      : restart application each #t sec. (not implemented yet.)\n"
                     "\t -K #H:#M   : restart application every #H:#M. (not implemented yet.)\n"
                     "\t -f #       : set syslog facility LOCAL# ( # = 0-7 )\n"
                     "\t -u #       : set user  as # ( root only )\n"
                     "\t -g #       : set group as # ( root only )\n"
                     "\t -s #       : set sleep time \n"
                     "\t -l logfile : write stdout/stderr message to logfile.\n"
                     "\t -p pidfile : write PID to pidfile.\n"
                     "\t --         : end of the watcher's option.\n"
                     "\n",
                     __progname, __watcher_version,  __progname);

    exit( exval );
}

/*
 * signal handler 
 */
static void signal_handler2( int sig, struct watcher_conf *c )
{
    static struct watcher_conf *config = NULL;

    if( sig == 0 ) // config set mode
    {
        config = c;
        return ;
    }
    if( debugmode > 0 ) fprintf( stderr,"catch signal %d,"
                " current child pid = %d\n", sig, childpid );

#if defined( sun ) && defined( __svr4__ ) 
    signal( sig, signal_handler ); 
#endif
    switch( sig )
    {
    case SIGCHLD:
         sigchildflag = 1;
         return ;
         
    case SIGHUP:
    case SIGTERM:
    case SIGINT:
    default:
        kill( childpid, sig );
        if( config->pidfile != NULL ) remove( config->pidfile );
        exit( 0 );
    case SIGUSR1:
        execerrcount ++;
        if( execerrcount > 3 ) 
        {
            if( debugmode > 0 )
                fprintf( stderr, "exec fail too many, terminate.\n" );
            else
                syslog( LOG_ERR, "exec fail too many, terminate." );
            exit( 1 );
        }
    }
}

static void signal_handler( int sig )
{
    signal_handler2( sig, NULL );
}

/*
 * private method: fix val to power of 8.
 */
static inline int fixsize( int val )
{
    switch( val % 8 )
    {
    case 0: return val;
    case 1: return val + 7;
    case 2: return val + 6;
    case 3: return val + 5;
    case 4: return val + 4;
    case 5: return val + 3;
    case 6: return val + 2;
    case 7: return val + 1;
    }
    return 0; 
}

static void print_conf(FILE *fp, struct watcher_conf *conf)
{
#define NULLCHK( V ) ( ( V == NULL ) ? "(NULL)" : V )
    int i ;
    fprintf( fp, "alert.region     = %d\n", conf->alert.region );
    fprintf( fp, "alert.count      = %d\n", conf->alert.count  );
    fprintf( fp, "syslog.facility  = %d\n", conf->syslog.facility );
    fprintf( fp, "syslog.level     = %d\n", conf->syslog.level    );
    fprintf( fp, "uid/gid          = %d / %d\n", conf->uid, conf->gid  );
    fprintf( fp, "sleeptime        = %d\n", conf->sleeptime       );
    fprintf( fp, "logfile          = %s\n", NULLCHK( conf->logfile  ) );
    fprintf( fp, "pidfile          = %s\n", NULLCHK( conf->pidfile  ) );
    fprintf( fp, "progname         = %s\n", NULLCHK( conf->progname ) );

    fprintf( fp, "argc             = %d\n", conf->argc    );
    for( i = 0 ; i< conf->argc ; i++)
        fprintf( fp, "argv[%d] = %s\n", i, conf->argv[i] );


    return ;
}

/*
 * initialize the application. from arguments
 */

static int touid( const char *string )
{
    int i,len = strlen( string );

    for( i = 0 ; i < len ; i ++ )
    {
        if( !isdigit( string[i] ) )
        {
            struct passwd *p = getpwnam( string );
            if( p == NULL ) return -1;

            return p->pw_uid ;
        }
    }
    return atoi( string );
}

static int togid( const char *string )
{
    int i,len = strlen( string );

    for( i = 0 ; i < len ; i ++ )
    {
        if( !isascii( string[i] ) )
        {
            struct group *g = getgrnam( string );
            if( g == NULL ) return -1;

            return g->gr_gid ;
        }
    }
    return atoi( string );
}

static int check_pidfile( const char *pidfile )
{
    int ret;
    struct stat statbuf ;
    char buff[1024];
    FILE *fp ; 
    int  pid;
  
    if( pidfile == NULL || pidfile[0] == '\0' ) 
    {
        fprintf( stderr, "invalid pid file. '%s'\n", pidfile );
        return 0;  //ERROR!
    }
    if( getuid() ) // not root
    {
        char *p = strdup( pidfile );
        char *q = strrchr( p, '/' );

        if( q != NULL && p != q )
        {
            *q = '\0';
            if( access( p, W_OK ) < 0 )
            {
                fprintf( stderr, "parentdir '%s' not writable for me.\n", p );
                free( p );
                return 0; //ERROR!
            }
        }
        free(p);
    }

    ret = access( pidfile, F_OK );
    if( ret < 0 ) //pidfile not found, it's OK.
        return 1;
    
    ret = access( pidfile, W_OK );
    if( ret < 0 ) //pidfile not writable.
    {
        fprintf( stderr, "pid file '%s' not writable for me.\n", pidfile );
        return 0; //ERROR!
    }

    fp = fopen( pidfile, "r" );
    if( fp == NULL )
    {
        fprintf( stderr, "can't open pid file '%s', %s\n", pidfile, strerror( errno ) );
        return 0; //ERROR!
    }    

   // read 1 line
    fgets( buff, sizeof(buff) -1 , fp ); //FIXME?
    fclose( fp );

    pid = atoi( buff );
    if( pid <= 0 )
    {
        fprintf( stderr, "pid file '%s' is broken.\n", pidfile);
        return 0; //ERROR!
    }

    ret = kill( pid, 0 ); // existence check.
    if( ret < 0 ) // process not exist.
    {
        switch( errno )
        {
        default:
        case EINVAL:
            fprintf( stderr, "something wrong in checking existance process %d, %s.\n", pid, strerror( errno ) );
            return 0; 
        case ESRCH:
            fprintf( stderr, "pid file '%s' is exist, but the process %d is not alive.\n", pidfile, pid );
            return 1; 
        case EPERM:
            fprintf( stderr, "pid file '%s' is exist, the process %d is alive.\n", pidfile, pid );
            return 0;
        }
    }

    fprintf( stderr, "pid file '%s' is exist, the process %d is alive.\n", pidfile, pid );
    return 0; 
}

static const struct watcher_conf *init( int argc, char *argv[] )
{
    int c;
    struct watcher_conf  confval = default_conf;
    struct watcher_conf *conf ;
    char *p ;
    int   i ;
    int size;

    // option check
    while( (c = getopt( argc, argv, "u:g:ht:f:s:d:l:p:V")) != EOF )
    {
        switch( c )
        {
        case 't':
            if( optarg == NULL ) continue;

            p = strchr( optarg, '.' );
            if( p == NULL ) 
                confval.alert.count = atoi( optarg );
            else{
                confval.alert.region = atoi( p+1 );
                p = '\0';
                confval.alert.count = atoi( optarg );
            }
            break;
        case 'f':
            if( optarg == NULL ) continue;
            switch(  atoi( optarg ) )
            {
            default:
            case 0:    confval.syslog.facility = LOG_LOCAL0; break;
            case 1:    confval.syslog.facility = LOG_LOCAL1; break;
            case 2:    confval.syslog.facility = LOG_LOCAL2; break;
            case 3:    confval.syslog.facility = LOG_LOCAL3; break;
            case 4:    confval.syslog.facility = LOG_LOCAL4; break;
            case 5:    confval.syslog.facility = LOG_LOCAL5; break;
            case 6:    confval.syslog.facility = LOG_LOCAL6; break;
            case 7:    confval.syslog.facility = LOG_LOCAL7; break;
            }
            break;
        case 's':
            if( optarg == NULL ) continue;
            i = atoi( optarg );
            if( i < 0 ) continue;

            confval.sleeptime = i ;  /* sec */
            break;
        case 'l' : //logfile
            if( confval.logfile != NULL ) free( confval.logfile );

            confval.logfile = strdup( optarg );
            break;

        case 'p' : //pidfile
            if( confval.pidfile != NULL ) free( confval.pidfile );

            if( optarg[0] != '/' ) // not full path
            {
                char *q = getcwd( NULL, 4096 ); // size means only Soalris.
                
                p = (char *)malloc( strlen( q ) + strlen( optarg ) + 4 );
                sprintf( p, "%s/%s", q, optarg );
            }else{
                p = strdup( optarg );
            }
            if( ! check_pidfile( p ) ) exit(9);

            confval.pidfile = p; 
            break;

        case 'u' : //user id
            if( getuid() != 0 ) break;

            confval.uid  = touid( optarg ); 
            break;

        case 'g' : //user id
            if( getuid() != 0 ) break;

            confval.gid  = togid( optarg ); 
            break;

        case 'd':
            debugmode = atoi( optarg );
            break;
        case '?':
        case 'h':
            show_help(6);
        case 'V':
            fprintf( stderr,"Version %s.\n\n", __watcher_version );
            exit(0);
        }
    }
    conf = malloc( sizeof( struct watcher_conf ) + fixsize( argc +2 ) * sizeof( char * ) );
   *conf = confval;

    for( i = 0 ; optind + i < argc ; i ++ )
    {
        conf->argv[i] = strdup( argv[ optind + i ] );
    }
    conf->argv[i+1] = NULL ;
    conf->argc = i;

    if( conf->progname == NULL )
    {
        char *p = strrchr( conf->argv[0], '/' );
        conf->progname = (( p == NULL ) ? conf->argv[0] : p+1) ;
    }

    if( debugmode > 0 ) print_conf( stderr, conf );


    /* sanity checks */
    if( conf->argc  <= 0 )
    {
         fprintf( stderr,"client program not specifiled.\n" );
         exit( 2 );
    }
    if( 0 > access( conf->argv[0] , X_OK ) )
    {
         fprintf( stderr,"client program '%s' not exist.\n", conf->argv[0] );
         exit( 2 );
    }

    /* signal handlers */ 
    signal_handler2( 0, conf ); // set config
    signal( SIGHUP,  signal_handler );
    signal( SIGCHLD, signal_handler );
    signal( SIGINT,  signal_handler );
    signal( SIGTERM, signal_handler );
    signal( SIGUSR1, signal_handler );

#if defined( sun ) && defined( __svr4__ )
#define LOG_PERROR 0
#endif
    /* syslog */
       openlog( "watcher", 
            LOG_PID | LOG_NDELAY | ( debugmode > 0 ? LOG_PERROR : 0 ),
            conf->syslog.facility );

    return conf;
}


/*
 * become daemon.
 *   disconnect tty, lose session leader, and so on.
 */
static int daemonize(void )
{
    int pid;
       if( debugmode > 0 ) return 1;// OK

    setsid(); /* get session group (parent) */
    if( pid = fork() )
    {
        /* parent or error */
        if( pid > 0 ) exit( 0 );

        perror( "watcher" );
        exit( 7 );
    }
    /* child only */

    close( 0 ); close( 1 ); close( 2 );
    open( "/dev/null", O_RDONLY ); // 0
    open( "/dev/null", O_WRONLY ); // 1 
    open( "/dev/null", O_WRONLY ); // 2
    return 1;
}

/*
 * get crash time 
 */
time_t get_crashtime( const struct watcher_state *stat, int prev )
{
    if( prev >= 0 && prev < stat->length )
    {
        if( stat->last_slot > prev ) 
            return stat->crashtimes[ stat->last_slot - prev ];
        else
            return stat->crashtimes[ stat->length + stat->last_slot - prev ] ;
    }
    return -1;
}

/*
 * set crash time 
 */
void set_crashtime( struct watcher_state *stat )
{
    if( stat->last_slot +1 <  stat->length )
    {
        stat->crashtimes[ ++( stat->last_slot ) ] = time( NULL );
    }
    else
    {
        stat->last_slot=0;
        stat->crashtimes[ 0 ] = time(NULL);
    }
    if( debugmode > 0 ) fprintf( stderr,"set crash[%02d] -> %d\n", 
        stat->last_slot, stat->crashtimes[ stat->last_slot ] );
    return;
}


/*
 *
 */
static int check_state( const struct watcher_state *state, int reg, int cont )
{
    time_t c,p,d;

    if( cont < 1 || reg < 1 ) return 0; // no count.

    c =  get_crashtime( state, 0 /*current*/ );
    p =  get_crashtime( state, cont - 1 );
    d = c - p;
    if( debugmode > 0 ) fprintf( stderr,"c %d, p %d, reg %d, diff %d\n",
                                  c,p,reg, d );
    if( ( p != 0 ) &&  ( d > 0 ) && ( d <= reg ) )
    {
        if( debugmode > 0 )
            fprintf( stderr, "process down too many, sleeping.\n", cont, reg );
        else
            syslog( LOG_ERR, "process down too many, sleeping.", cont, reg );
        return -1; /* problem? */
    }
    if( execerrcount > 0 ) 
    {
        return -1;
    }
    if( WEXITSTATUS( state->wstatus ) != 0)
    {
        if( debugmode > 0 )
            fprintf( stderr, "process abnormal terminate, status = %d.\n", 
                WEXITSTATUS( state->wstatus ));
        else
            syslog( LOG_ERR, "process abnormal terminate, status = %d.", 
                WEXITSTATUS( state->wstatus ));
        return 0; /* problem? */
    }

    execerrcount = 0;

    return 0;// no-problem
}

static struct watcher_state *makestate( const struct watcher_conf *config )
{
    struct watcher_state *c;
    c = calloc( sizeof( struct watcher_state )
                +  sizeof( time_t ) * ( config->alert.count *3 ), 1 );
    if( c == NULL ) return NULL;

    c->length =  config->alert.count *3 ;
    c->wstatus = 0;
    c->last_slot = -1;// little magic

    if( debugmode > 0 )
        fprintf( stderr, "slot length = %d\n", c->length );
    return c;
}

 
static int writelog( const char *logfilename, int fd, struct stat* oldbuf, int *ffd )
{
    int    ret, siz, rop ;
    char buff[BUFSIZ]; 
    struct stat stbuf;

    memset( &stbuf, 0x00, sizeof( stbuf ) );
    memset( buff,   0x00, sizeof( buff ) );

    siz = read( fd, buff, sizeof( buff ) -1 ); // preserve last '\0' 
    if( siz <= 0 ) //error
    {
        return 0; //FIXME
    }


    if( *ffd < 0 ) 
    {
        rop = 1;
    }else{
        ret = stat( logfilename, &stbuf );
        if( ret < 0 )
        { 
            rop = 1 ;
        }else{
            if( ( stbuf.st_ino   != oldbuf->st_ino ) 
             || ( stbuf.st_mtime != oldbuf->st_mtime ) )
            {
               rop = 1;
              *oldbuf = stbuf ;
            }
        }
    }
        
    if( rop > 0 )
    {
        if( *ffd >= 0 ) close( *ffd );
       *ffd = open( logfilename, O_CREAT |O_WRONLY| O_APPEND,
                    ( oldbuf->st_mode > 0 ) ? oldbuf->st_mode : 0644 );
        if( *ffd < 0 )
        {
            if( debugmode > 0 )
                fprintf( stderr, "can't re-open '%s', reason '%s', msg '%s'\n", 
                                      logfilename, strerror( errno ), buff );
            else
                syslog( LOG_WARNING, "can't re-open '%s', reason '%s', msg '%s'", 
                                      logfilename, strerror( errno ), buff );
        }else{
            if( getuid() == 0 )
            {
                if( oldbuf->st_uid != 0 || oldbuf->st_gid != 0 ) 
                {
                    fchown( *ffd, oldbuf->st_uid, oldbuf->st_gid );
                }
            }
            write( *ffd, buff, siz );
        }
    }else{
        write( *ffd, buff, siz );
    }
    return 0; 
}

static int logging( const char *logfilename, int outh, int errh )
{
    struct stat statbuf ;
    int    logfd = -1 ;
    int    ret ;
    fd_set rfds;
    int    max ;

    ret = stat( logfilename, &statbuf );
    if( ret < 0 ) 
    {
       memset( &statbuf, 0x00, sizeof( statbuf ) );
        //FIXME
    }

    while(1)
    {
        FD_ZERO( &rfds );
        FD_SET( outh, &rfds );
        FD_SET( errh, &rfds );

        max = outh > errh ? outh:errh;

        ret = select( max+1, &rfds, NULL, NULL, NULL );
        if( !ret )
        {
            switch( errno )
            {
            case EINTR:
                 break;
            default: 
                continue ;
            }
        }
        if( FD_ISSET( outh, &rfds ) )
        {
            writelog( logfilename, outh, &statbuf, &logfd );
        } 
        if( FD_ISSET( errh, &rfds ) )
        {
            writelog( logfilename, errh, &statbuf, &logfd );
        } 
        if( sigchildflag ) 
        {
            close( logfd );
            return 0 ;
        }
     }
}

static int writepidfile( const char *pidfilename, pid_t childpid )
{
    int fd ;
    char buff[1024]; // FIXME

    fd = open( pidfilename, O_CREAT |O_WRONLY| O_TRUNC, 0644 );
    if( fd < 0 )
    {
        if( debugmode > 0 )
            fprintf( stderr, "can't open '%s', reason '%s'.\n", 
                                  pidfilename, strerror( errno ) );
        else
            syslog( LOG_WARNING, "can't open '%s', reason '%s'.", 
                                  pidfilename, strerror( errno ) );
        return 0;
    }
    if( childpid != 0 )
        sprintf( buff, "%d\n%d\n", getpid(), childpid );
    else
        sprintf( buff, "%d\n", getpid() );

    write( fd, buff, strlen( buff ) );
    close( fd ); 

    return 1;
}

#define MOTHERSIDE  0
#define CHILDSIDE   1

int main (int argc, char *argv[] )
{
    const struct watcher_conf  *config = NULL;
    struct       watcher_state *state  = NULL;
    int          logfile_flag          = 0; // no logging.
    int          outpipe[2], errpipe[2] ;
    int          ret;

    setprogname( argv[0] );

    if( !( config = init( argc, argv )  ) 
     || !( state  =  makestate( config ) )
     || !daemonize( ) ) /* initialize and daemonize */
        exit( 8 );

#if defined( __linux__ )
    // linux don't have setproctitle...
    initproctitle( argc, argv );
#endif

#if defined( sun ) && defined( __svr4__ )
    ; // do nothing. so solaris doesn't allow change title.
#else
    setproctitle( "watcher_of_%s", config->progname );
#endif
    logfile_flag = ( config->logfile != NULL ) ;


    /* main loop */
    for(;; check_state( state, config->alert.region, config->alert.count )
          ? sleep( config->sleeptime ) :  0 /* clear */)
    {
        if( debugmode > 0 ) fprintf( stderr,"Loop...\n" );

        if( logfile_flag )
        {
            int flag ;

          // create stdout pipe
            pipe( outpipe );
            flag =  fcntl( outpipe[MOTHERSIDE], F_GETFL ); 
            flag |= O_NONBLOCK ; 
            fcntl( outpipe[MOTHERSIDE], F_SETFL, &flag );

          // create stderr pipe
            pipe( errpipe );
            flag =  fcntl( errpipe[MOTHERSIDE], F_GETFL ); 
            flag |= O_NONBLOCK ; 
            fcntl( errpipe[MOTHERSIDE], F_SETFL, &flag );
        }

        sigchildflag = 0; // clear
        if(  childpid = fork() )
        {
            if( debugmode > 0 ) fprintf( stderr,"pid = %d\n", childpid );
            if( childpid < 0 ) /* error */
            {
                perror( "fork" );
                exit(0);
                ;
            }else{ /* parent */
                if( debugmode >  0 )
                    fprintf( stderr, "proccess %s [%d] execute.", 
                            config->argv[0], childpid );
                else
                    syslog( LOG_INFO, "proccess %s [%d] execute.\n", 
                            config->argv[0], childpid );

                if( logfile_flag ) // logging, async mode.
                {
                    state->wstatus  = 0; // clear
                    if( config->pidfile != NULL )
                        writepidfile(config->pidfile, childpid );
                    close( outpipe[CHILDSIDE] );
                    close( errpipe[CHILDSIDE] );
                    logging( config->logfile, outpipe[0], errpipe[0] );
                    ret = wait(  &( state->wstatus ) );
                    close( outpipe[MOTHERSIDE] );
                    close( errpipe[MOTHERSIDE] );
                    if( config->pidfile != NULL ) writepidfile(config->pidfile, 0 );
                    if( debugmode > 0 ) 
                        fprintf( stderr, "ret = %d (%d), errno = %d, status = %d, isExit = %s\n", 
                                          ret, childpid, errno, state->wstatus, 
                                          ( WIFEXITED( state->wstatus ) ) ? "YES" : "NO" );

                }else{             // no logging, sync mode.

                    do{
                        state->wstatus  = 0; // clear
                        if( config->pidfile != NULL )
                            writepidfile(config->pidfile, childpid );
                        ret = waitpid( childpid, &( state->wstatus ), 0 );
                        if( config->pidfile != NULL ) writepidfile(config->pidfile, 0 );
                        if( debugmode > 0 ) 
                            fprintf( stderr, "ret = %d (%d), errno = %d, status = %d, isExit = %s\n", 
                                              ret, childpid, errno, state->wstatus, 
                                              ( WIFEXITED( state->wstatus ) ) ? "YES" : "NO" );
                        if( ret < 0 )
                        {
                            continue; //FIXME
                        }
                    } while( ret < 0 || ! WIFEXITED( state->wstatus ) );
                }
                set_crashtime( state );
                if( debugmode > 0 )
                    fprintf( stderr, "proccess %s [%d] terminate.\n", 
                                config->progname, childpid );
                else
                    syslog( LOG_INFO, "proccess %s [%d] terminate.", 
                                config->progname, childpid );
                sleep( 1 );
            }

        }else{ // pid == 0 ,child

            if( getuid() == 0 && config->uid != -1 ) setreuid( config->uid, config->uid );
            if( getuid() == 0 && config->gid != -1 ) setregid( config->gid, config->gid );

            signal( SIGUSR1, SIG_IGN );
            if( logfile_flag ) // logging async mode.
            {
                close( outpipe[MOTHERSIDE]   ); 
                dup2(  outpipe[CHILDSIDE], 1 );
                close( outpipe[CHILDSIDE]    );

                close( errpipe[MOTHERSIDE]   );
                dup2(  errpipe[CHILDSIDE], 2 );  
                close( errpipe[CHILDSIDE]    );
            }

            execv( config->argv[0], config->argv );
            if( debugmode > 0 ) 
                perror( "child" );
            else
                syslog( LOG_ERR, "%s [%d] execute fail,  %m", config->argv[0], getpid() );
            kill( motherpid, SIGUSR1 );
            exit( 9 );/* error */
        }
    }
    exit(0);
}

