#include "progname.h"

#ifndef HAVE_PROGNAME

#include <string.h>

const char *__progname = "";


void setprogname( const char *argv0 )
{
    const char *p;

    if( argv0 == NULL ) return ;

    p = strrchr( argv0, '/');
    if( p == NULL ) 
        __progname = strdup( argv0 );
    else
        __progname = strdup( (p+1) );

    return;
}




#endif
