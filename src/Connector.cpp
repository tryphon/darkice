/*------------------------------------------------------------------------------

   Copyright (c) 2000 Tyrell Corporation. All rights reserved.

   Tyrell DarkIce

   File     : Connector.cpp
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

#include "Exception.h"
#include "Connector.h"


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
Connector :: init ( Source          * source )        throw ( Exception )
{
    this->source   = source;
    this->sinks    = 0;
    this->numSinks = 0;
}


/*------------------------------------------------------------------------------
 *  De-initialize the object
 *----------------------------------------------------------------------------*/
void
Connector :: strip ( void )                             throw ( Exception )
{
    source = 0;

    if ( sinks ) {
        unsigned int    u;

        for ( u = 0; u < numSinks; ++u ) {
            sinks[u] = 0;
        }

        delete[] sinks;
    }
}


/*------------------------------------------------------------------------------
 *  Constructor
 *----------------------------------------------------------------------------*/
Connector :: Connector (    const Connector &   connector ) throw ( Exception )
{
    unsigned int    u;

    init( connector.source.get());

    for ( u = 0; u < connector.numSinks; ++u ) {
        attach( connector.sinks[u].get() );
    }
}


/*------------------------------------------------------------------------------
 *  Assignment operator
 *----------------------------------------------------------------------------*/
Connector &
Connector :: operator= (    const Connector &   connector ) throw ( Exception )
{
    if ( this != &connector ) {
        unsigned int    u;

        /* first free everything */
        strip();

        /* then fill in */
        init( connector.source.get() );

        for ( u = 0; u < connector.numSinks; ++u ) {
            attach( connector.sinks[u].get() );
        }
    }

    return *this;
}


/*------------------------------------------------------------------------------
 *  Attach a sink to the connector
 *----------------------------------------------------------------------------*/
void
Connector :: attach (   Sink              * sink )          throw ( Exception )
{
    if ( !sinks ) {
        
        numSinks = 1;
        sinks    = new Ref<Sink>[1];
        sinks[0] = sink;

    } else {

        unsigned int    u;
        Ref<Sink>     * s = new Ref<Sink>[numSinks + 1];
        
        for ( u = 0; u < numSinks; ++u ) {
            s[u] = sinks[u].get();
        }

        s[numSinks] = sink;
        delete[] sinks;
        sinks = s;
        ++numSinks;
    }
}


/*------------------------------------------------------------------------------
 *  Detach a sink to the connector
 *----------------------------------------------------------------------------*/
bool
Connector :: detach (   Sink              * sink )          throw ( Exception )
{
    if ( numSinks == 0 ) {
        
        return false;

    } else if ( numSinks == 1 ) {

        sinks[0] = 0;
        delete[] sinks;
        sinks    = 0;
        --numSinks;

        return true;

    } else {
    
        unsigned int    u;
        unsigned int    v;
        unsigned int    ix;
        Ref<Sink>     * s;

        ix = numSinks;
        for ( u = 0; u < numSinks; ++u ) {

            if ( sinks[u].get() == sink ) {
                ix = u;
                break;
            }
        }

        if ( ix == numSinks ) {
            return false;
        }

        s = new Ref<Sink>[numSinks - 1];
        for ( u = 0, v = 0; u < numSinks; ++u ) {

            if ( u != ix ) {
                s[v++] = sinks[u];
            }
        }

        sinks[ix] = 0;
        delete[] sinks;
        sinks = s;
        --numSinks;

        return true;
    }
}


/*------------------------------------------------------------------------------
 *  Open the source and all the sinks if needed
 *----------------------------------------------------------------------------*/
bool
Connector :: open ( void )                          throw ( Exception )
{
    unsigned int        u;

    if ( !source->isOpen() ) {
        if ( !source->open() ) {
            return false;
        }
    }

    for ( u = 0; u < numSinks; ++u ) {
        if ( !sinks[u]->isOpen() ) {
            if ( !sinks[u]->open() ) {
                break;
            }
        }
    }

    /* if not all could be opened, close those that were */
    if ( u < numSinks ) {
        unsigned int        v;

        for ( v = 0; v < u; ++v ) {
            sinks[v]->close();
        }

        source->close();

        return false;
    }

    return true;
}


/*------------------------------------------------------------------------------
 *  Transfer some data from the source to the sink
 *----------------------------------------------------------------------------*/
unsigned int
Connector :: transfer ( unsigned long       bytes,
                        unsigned int        bufSize,
                        unsigned int        sec,
                        unsigned int        usec )      throw ( Exception )
{
    unsigned int    u;
    unsigned long   b;

    if ( numSinks == 0 ) {
        return 0;
    }

    if ( bufSize == 0 ) {
        return 0;
    }

    unsigned char * buf = new unsigned char[bufSize];

    reportEvent( 6, "Connector :: tranfer, bytes", bytes);
    
    for ( b = 0; !bytes || b < bytes; ) {
        unsigned int    d = 0;
        unsigned int    e = 0;

        if ( source->canRead( sec, usec) ) {
            d = source->read( buf, bufSize);

            /* check for EOF */
            if ( d == 0 ) {
                reportEvent( 3, "Connector :: transfer, EOF");
                break;
            }

            for ( u = 0; u < numSinks; ++u ) {

                if ( sinks[u]->canWrite( sec, usec) ) {
                    try {
                        e = sinks[u]->write( buf, d);
                    } catch ( Exception     & e ) {
                        sinks[u]->close();
                        detach( sinks[u].get() );

                        reportEvent( 4,
                              "Connector :: transfer, sink removed, remaining",
                              numSinks);

                        if ( numSinks == 0 ) {
                            reportEvent( 4,
                                        "Connector :: transfer, no more sinks");
                            delete[] buf;
                            return b;
                        }
                        /* with the call to detach, numSinks gets 1 lower,
                         * and the next sink comes to sinks[u] */
                        --u;
                    }
                }
            }
            
            b += d;
        } else {
            reportEvent( 3, "Connector :: transfer, can't read");
            break;
        }
    }

    delete[] buf;
    return b;
}


/*------------------------------------------------------------------------------
 *  Close the source and all the sinks if needed
 *----------------------------------------------------------------------------*/
void
Connector :: close ( void )                         throw ( Exception )
{
    unsigned int        u;
    
    source->close();
    for ( u = 0; u < numSinks; ++u ) {
        sinks[u]->close();
    }
}



/*------------------------------------------------------------------------------
 
  $Source$

  $Log$
  Revision 1.9  2002/08/02 17:59:17  darkeye
  bug fix: when the last server dropped connection, darkice crashed

  Revision 1.8  2002/07/20 16:37:06  darkeye
  added fault tolerance in case a server connection is dropped

  Revision 1.7  2002/05/28 12:35:41  darkeye
  code cleanup: compiles under gcc-c++ 3.1, using -pedantic option

  Revision 1.6  2001/08/26 20:44:30  darkeye
  removed external command-line encoder support
  replaced it with a shared-object support for lame with the possibility
  of static linkage

  Revision 1.5  2001/08/26 08:43:13  darkeye
  added support for unlimited time encoding

  Revision 1.4  2000/11/15 18:37:37  darkeye
  changed the transferable number of bytes to unsigned long

  Revision 1.3  2000/11/15 18:08:43  darkeye
  added multiple verbosity-level event reporting and verbosity command
  line option

  Revision 1.2  2000/11/13 18:46:50  darkeye
  added kdoc-style documentation comments

  Revision 1.1.1.1  2000/11/05 10:05:49  darkeye
  initial version

  
------------------------------------------------------------------------------*/

