/*------------------------------------------------------------------------------

   Copyright (c) 2000 Tyrell Corporation. All rights reserved.

   Tyrell DarkIce

   File     : FileSource.cpp
   Version  : $Revision$
   Author   : $Author$
   Location : $Source$
   
   Copyright notice:

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License  
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.
   
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of 
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
    GNU General Public License for more details.
   
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

------------------------------------------------------------------------------*/

/* ============================================================ include files */

#ifdef HAVE_CONFIG_H
#include "configure.h"
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#else
#error need unistd.h
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#else
#error need sys/types.h
#endif

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#else
#error need sys/stat.h
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#else
#error need fcntl.h
#endif

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#else
#error need sys/time.h
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#else
#error need string.h
#endif


#include "Exception.h"
#include "Util.h"
#include "FileSource.h"


/* ===================================================  local data structures */


/* ================================================  local constants & macros */

/*------------------------------------------------------------------------------
 *  File identity
 *----------------------------------------------------------------------------*/
static const char fileid[] = "$Id$";


/* ===============================================  local function prototypes */


/* =============================================================  module code */

/*------------------------------------------------------------------------------
 *  Initialize the object
 *----------------------------------------------------------------------------*/
void
FileSource :: init (    const char    * name )          throw ( Exception )
{
    fileName = Util::strDup( name);
    fileDescriptor = 0;
}


/*------------------------------------------------------------------------------
 *  De-initialize the object
 *----------------------------------------------------------------------------*/
void
FileSource :: strip ( void )                            throw ( Exception )
{
    if ( isOpen() ) {
        close();
    }
    
    delete[] fileName;
    fileDescriptor = 0;
}


/*------------------------------------------------------------------------------
 *  Copy Constructor
 *----------------------------------------------------------------------------*/
FileSource :: FileSource ( const FileSource &     fs )      throw ( Exception )
{
    init( fs.fileName);

    fileDescriptor = fs.fileDescriptor ? dup( fs.fileDescriptor) : 0;

    if ( fileDescriptor == -1 ) {
        strip();
        throw Exception( __FILE__, __LINE__, "dup failure");
    }
}


/*------------------------------------------------------------------------------
 *  Assignment operator
 *----------------------------------------------------------------------------*/
FileSource &
FileSource :: operator= ( const FileSource &     fs )       throw ( Exception )
{
    if ( this != &fs ) {
        init( fs.fileName);

        fileDescriptor = fs.fileDescriptor ? dup( fs.fileDescriptor) : 0;

        if ( fileDescriptor == -1 ) {
            strip();
            throw Exception( __FILE__, __LINE__, "dup failure");
        }
    }

    return *this;
}


/*------------------------------------------------------------------------------
 *  Check wether a file exists
 *----------------------------------------------------------------------------*/
bool
FileSource :: exists ( void ) const               throw ()
{
    struct stat     st;

    if ( stat( (const char*)fileName, &st) == -1 ) {
        return false;
    }

    return true;
}


/*------------------------------------------------------------------------------
 *  Open the source
 *----------------------------------------------------------------------------*/
bool
FileSource :: open ( void )                             throw ( Exception )
{
    if ( isOpen() ) {
        return false;
    }

    if ( (fileDescriptor = ::open( fileName, O_RDONLY)) == -1 ) {
        fileDescriptor = 0;
        return false;
    }

    return true;
}


/*------------------------------------------------------------------------------
 *  Check wether read() would return anything
 *----------------------------------------------------------------------------*/
bool
FileSource :: canRead (     unsigned int    sec,
                            unsigned int    usec )      throw ( Exception )
{
    fd_set              fdset;
    struct timeval      tv;
    int                 ret;

    if ( !isOpen() ) {
        return false;
    }

    FD_ZERO( &fdset);
    FD_SET( fileDescriptor, &fdset);
    tv.tv_sec  = sec;
    tv.tv_usec = usec;

    ret = select( fileDescriptor + 1, &fdset, NULL, NULL, &tv);
    
    if ( ret == -1 ) {
        throw Exception( __FILE__, __LINE__, "select error");
    }

    return ret > 0;
}


/*------------------------------------------------------------------------------
 *  Read from the audio source
 *----------------------------------------------------------------------------*/
unsigned int
FileSource :: read (        void          * buf,
                            unsigned int    len )       throw ( Exception )
{
    ssize_t     ret;

    if ( !isOpen() ) {
        return 0;
    }

    ret = ::read( fileDescriptor, buf, len);

    if ( ret == -1 ) {
        throw Exception( __FILE__, __LINE__, "read error");
    }

    return ret;
}


/*------------------------------------------------------------------------------
 *  Close the audio source
 *----------------------------------------------------------------------------*/
void
FileSource :: close ( void )                            throw ( Exception )
{
    if ( !isOpen() ) {
        return;
    }

    ::close( fileDescriptor);
    fileDescriptor = 0;
}


/*------------------------------------------------------------------------------
 
  $Source$

  $Log$
  Revision 1.5  2001/09/11 15:05:21  darkeye
  added Solaris support

  Revision 1.4  2001/08/26 20:44:30  darkeye
  removed external command-line encoder support
  replaced it with a shared-object support for lame with the possibility
  of static linkage

  Revision 1.3  2000/11/12 13:31:40  darkeye
  added kdoc-style documentation comments

  Revision 1.2  2000/11/05 14:08:27  darkeye
  changed builting to an automake / autoconf environment

  Revision 1.1.1.1  2000/11/05 10:05:51  darkeye
  initial version

  
------------------------------------------------------------------------------*/

