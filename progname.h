#ifndef __IUTIL_PROGNAME_H__
#define __IUTIL_PROGNAME_H__

#ifndef HAVE_PROGNAME
#if defined( linux ) || defined( __APPLE__ )
#define HAVE_PROGNAME
#else
#undef HAVE_PROGNAME
#endif
#endif /* HAVE_PROGNAME */

#ifdef HAVE_PROGNAME
#define setprogname( V )   /**/ ;
#else
void setprogname( const char *a );
#endif

#endif /* __IUTIL_PROGNAME_H__ */

