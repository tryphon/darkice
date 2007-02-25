/*------------------------------------------------------------------------------

   Copyright (c) 2000-2007 Tyrell Corporation. All rights reserved.

   Tyrell DarkIce

   File     : DarkIce.cpp
   Version  : $Revision$
   Author   : $Author$
   Location : $HeadURL$
   

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
#include "config.h"
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#else
#error need stdlib.h
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

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#else
#error need sys/wait.h
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#else
#error need errno.h
#endif

#ifdef HAVE_SCHED_H
#include <sched.h>
#else
#error need sched.h
#endif



#include "Util.h"
#include "IceCast.h"
#include "IceCast2.h"
#include "ShoutCast.h"
#include "FileCast.h"
#include "MultiThreadedConnector.h"
#include "DarkIce.h"

#ifdef HAVE_LAME_LIB
#include "LameLibEncoder.h"
#endif

#ifdef HAVE_TWOLAME_LIB
#include "TwoLameLibEncoder.h"
#endif

#ifdef HAVE_VORBIS_LIB
#include "VorbisLibEncoder.h"
#endif

#ifdef HAVE_FAAC_LIB
#include "FaacEncoder.h"
#endif


/* ===================================================  local data structures */


/* ================================================  local constants & macros */

/*------------------------------------------------------------------------------
 *  File identity
 *----------------------------------------------------------------------------*/
static const char fileid[] = "$Id$";


/*------------------------------------------------------------------------------
 *  Make sure wait-related stuff is what we expect
 *----------------------------------------------------------------------------*/
#ifndef WEXITSTATUS
# define WEXITSTATUS(stat_val)      ((unsigned)(stat_val) >> 8)
#endif
#ifndef WIFEXITED
# define WIFEXITED(stat_val)        (((stat_val) & 255) == 0)
#endif



/* ===============================================  local function prototypes */


/* =============================================================  module code */

/*------------------------------------------------------------------------------
 *  Initialize the object
 *----------------------------------------------------------------------------*/
void
DarkIce :: init ( const Config      & config )              throw ( Exception )
{
    unsigned int             bufferSecs;
    const ConfigSection    * cs;
    const char             * str;
    unsigned int             sampleRate;
    unsigned int             bitsPerSample;
    unsigned int             channel;
    bool                     reconnect;
    const char             * device;

    // the [general] section
    if ( !(cs = config.get( "general")) ) {
        throw Exception( __FILE__, __LINE__, "no section [general] in config");
    }
    str = cs->getForSure( "duration", " missing in section [general]");
    duration = Util::strToL( str);
    str = cs->getForSure( "bufferSecs", " missing in section [general]");
    bufferSecs = Util::strToL( str);
    if (bufferSecs == 0) {
        throw Exception(__FILE__, __LINE__,
                        "setting bufferSecs to 0 not supported");
    }
    str           = cs->get( "reconnect");
    reconnect     = str ? (Util::strEq( str, "yes") ? true : false) : true;

    // real-time scheduling is enabled by default
    str = cs->get( "realtime" );
    enableRealTime = str ? (Util::strEq( str, "yes") ? true : false) : true;


    // the [input] section
    if ( !(cs = config.get( "input")) ) {
        throw Exception( __FILE__, __LINE__, "no section [input] in config");
    }
    
    str        = cs->getForSure( "sampleRate", " missing in section [input]");
    sampleRate = Util::strToL( str);
    str       = cs->getForSure( "bitsPerSample", " missing in section [input]");
    bitsPerSample = Util::strToL( str);
    str           = cs->getForSure( "channel", " missing in section [input]");
    channel       = Util::strToL( str);
    device        = cs->getForSure( "device", " missing in section [input]");

    dsp             = AudioSource::createDspSource( device,
                                                    sampleRate,
                                                    bitsPerSample,
                                                    channel );
    encConnector    = new MultiThreadedConnector( dsp.get(), reconnect );

    noAudioOuts = 0;
    configIceCast( config, bufferSecs);
    configIceCast2( config, bufferSecs);
    configShoutCast( config, bufferSecs);
    configFileCast( config);
}


/*------------------------------------------------------------------------------
 *  Look for the IceCast stream outputs in the config file
 *----------------------------------------------------------------------------*/
void
DarkIce :: configIceCast (  const Config      & config,
                            unsigned int        bufferSecs  )
                                                        throw ( Exception )
{
    // look for IceCast encoder output streams,
    // sections [icecast-0], [icecast-1], ...
    char            stream[]        = "icecast- ";
    size_t          streamLen       = Util::strLen( stream);
    unsigned int    u;

    for ( u = noAudioOuts; u < maxOutput; ++u ) {
        const ConfigSection    * cs;

        // ugly hack to change the section name to "stream0", "stream1", etc.
        stream[streamLen-1] = '0' + (u - noAudioOuts);

        if ( !(cs = config.get( stream)) ) {
            break;
        }

#if !defined HAVE_LAME_LIB && !defined HAVE_TWOLAME_LIB
        throw Exception( __FILE__, __LINE__,
                         "DarkIce not compiled with lame or twolame support, "
                         "thus can't connect to IceCast 1.x, stream: ",
                         stream);
#else

        const char                * str;

        unsigned int                sampleRate      = 0;
        unsigned int                channel         = 0;
        AudioEncoder::BitrateMode   bitrateMode;
        unsigned int                bitrate         = 0;
        double                      quality         = 0.0;
        const char                * server          = 0;
        unsigned int                port            = 0;
        const char                * password        = 0;
        const char                * mountPoint      = 0;
        const char                * remoteDumpFile  = 0;
        const char                * name            = 0;
        const char                * description     = 0;
        const char                * url             = 0;
        const char                * genre           = 0;
        bool                        isPublic        = false;
        int                         lowpass         = 0;
        int                         highpass        = 0;
        const char                * localDumpName   = 0;
        FileSink                  * localDumpFile   = 0;
        bool                        fileAddDate     = false;
        const char                * fileDateFormat  = 0;

        str         = cs->get( "sampleRate");
        sampleRate  = str ? Util::strToL( str) : dsp->getSampleRate();
        str         = cs->get( "channel");
        channel     = str ? Util::strToL( str) : dsp->getChannel();

        str         = cs->get( "bitrate");
        bitrate     = str ? Util::strToL( str) : 0;
        str         = cs->get( "quality");
        quality     = str ? Util::strToD( str) : 0.0;
        
        str         = cs->getForSure( "bitrateMode",
                                      " not specified in section ",
                                      stream);
        if ( Util::strEq( str, "cbr") ) {
            bitrateMode = AudioEncoder::cbr;
            
            if ( bitrate == 0 ) {
                throw Exception( __FILE__, __LINE__,
                                 "bitrate not specified for CBR encoding");
            }
            if ( cs->get( "quality" ) == 0 ) {
                throw Exception( __FILE__, __LINE__,
                                 "quality not specified for CBR encoding");
            }
        } else if ( Util::strEq( str, "abr") ) {
            bitrateMode = AudioEncoder::abr;

            if ( bitrate == 0 ) {
                throw Exception( __FILE__, __LINE__,
                                 "bitrate not specified for ABR encoding");
            }
        } else if ( Util::strEq( str, "vbr") ) {
            bitrateMode = AudioEncoder::vbr;

            if ( cs->get( "quality" ) == 0 ) {
                throw Exception( __FILE__, __LINE__,
                                 "quality not specified for VBR encoding");
            }
        } else {
            throw Exception( __FILE__, __LINE__,
                             "invalid bitrate mode: ", str);
        }
        
        

        server      = cs->getForSure( "server", " missing in section ", stream);
        str         = cs->getForSure( "port", " missing in section ", stream);
        port        = Util::strToL( str);
        password    = cs->getForSure("password"," missing in section ",stream);
        mountPoint  = cs->getForSure( "mountPoint",
                                      " missing in section ",
                                      stream);
        remoteDumpFile = cs->get( "remoteDumpFile");
        name        = cs->get( "name");
        description = cs->get("description");
        url         = cs->get( "url");
        genre       = cs->get( "genre");
        str         = cs->get( "public");
        isPublic    = str ? (Util::strEq( str, "yes") ? true : false) : false;
        str         = cs->get( "lowpass");
        lowpass     = str ? Util::strToL( str) : 0;
        str         = cs->get( "highpass");
        highpass    = str ? Util::strToL( str) : 0;
        str         = cs->get("fileAddDate");
        fileAddDate = str ? (Util::strEq( str, "yes") ? true : false) : false;
        fileDateFormat = cs->get("fileDateFormat");

        localDumpName = cs->get( "localDumpFile");

        // go on and create the things

        // check for and create the local dump file if needed
        if ( localDumpName != 0 ) {
            if ( fileAddDate ) {
                if (fileDateFormat == 0) {
                    localDumpName = Util::fileAddDate(localDumpName);
                }
                else {
                    localDumpName = Util::fileAddDate(  localDumpName,
                                                        fileDateFormat );
                }
            }

            localDumpFile = new FileSink( stream, localDumpName);
            if ( !localDumpFile->exists() ) {
                if ( !localDumpFile->create() ) {
                    reportEvent( 1, "can't create local dump file",
                                    localDumpName);
                    localDumpFile = 0;
                }
            }
            if ( fileAddDate ) {
                delete[] localDumpFile;
            }
        }
        // streaming related stuff
        audioOuts[u].socket = new TcpSocket( server, port);
        audioOuts[u].server = new IceCast( audioOuts[u].socket.get(),
                                           password,
                                           mountPoint,
                                           bitrate,
                                           name,
                                           description,
                                           url,
                                           genre,
                                           isPublic,
                                           remoteDumpFile,
                                           localDumpFile,
                                           bufferSecs );

        str = cs->getForSure( "format", " missing in section ", stream);

        if (!Util::strEq(str, "mp3") && !Util::strEq(str, "mp2")) {
            throw Exception( __FILE__, __LINE__,
                             "unsupported stream format: ", str);

        }

#ifdef HAVE_LAME_LIB
        if ( Util::strEq( str, "mp3") ) {
			audioOuts[u].encoder = new LameLibEncoder( audioOuts[u].server.get(),
													   dsp.get(),
													   bitrateMode,
													   bitrate,
													   quality,
													   sampleRate,
													   channel,
													   lowpass,
													   highpass );
        }
#endif
#ifdef HAVE_TWOLAME_LIB
        if ( Util::strEq( str, "mp2") ) {
			audioOuts[u].encoder = new TwoLameLibEncoder(
                                                      audioOuts[u].server.get(),
													  dsp.get(),
													  bitrateMode,
													  bitrate,
													  sampleRate,
													  channel );
        }
#endif

        encConnector->attach( audioOuts[u].encoder.get());
#endif // HAVE_LAME_LIB || HAVE_TWOLAME_LIB
    }

    noAudioOuts += u;
}


/*------------------------------------------------------------------------------
 *  Look for the IceCast2 stream outputs in the config file
 *----------------------------------------------------------------------------*/
void
DarkIce :: configIceCast2 (  const Config      & config,
                             unsigned int        bufferSecs  )
                                                        throw ( Exception )
{
    // look for IceCast2 encoder output streams,
    // sections [icecast2-0], [icecast2-1], ...
    char            stream[]        = "icecast2- ";
    size_t          streamLen       = Util::strLen( stream);
    unsigned int    u;

    for ( u = noAudioOuts; u < maxOutput; ++u ) {
        const ConfigSection    * cs;

        // ugly hack to change the section name to "stream0", "stream1", etc.
        stream[streamLen-1] = '0' + (u - noAudioOuts);

        if ( !(cs = config.get( stream)) ) {
            break;
        }

        const char                * str;

        IceCast2::StreamFormat      format;
        unsigned int                sampleRate      = 0;
        unsigned int                channel         = 0;
        AudioEncoder::BitrateMode   bitrateMode;
        unsigned int                bitrate         = 0;
        unsigned int                maxBitrate      = 0;
        double                      quality         = 0.0;
        const char                * server          = 0;
        unsigned int                port            = 0;
        const char                * password        = 0;
        const char                * mountPoint      = 0;
        const char                * name            = 0;
        const char                * description     = 0;
        const char                * url             = 0;
        const char                * genre           = 0;
        bool                        isPublic        = false;
        int                         lowpass         = 0;
        int                         highpass        = 0;
        const char                * localDumpName   = 0;
        FileSink                  * localDumpFile   = 0;
        bool                        fileAddDate     = false;
        const char                * fileDateFormat  = 0;

        str         = cs->getForSure( "format", " missing in section ", stream);
        if ( Util::strEq( str, "vorbis") ) {
            format = IceCast2::oggVorbis;
        } else if ( Util::strEq( str, "mp3") ) {
            format = IceCast2::mp3;
        } else if ( Util::strEq( str, "mp2") ) {
            format = IceCast2::mp2;
        } else if ( Util::strEq( str, "aac") ) {
            format = IceCast2::aac;
        } else {
            throw Exception( __FILE__, __LINE__,
                             "unsupported stream format: ", str);
        }
                
        str         = cs->get( "sampleRate");
        sampleRate  = str ? Util::strToL( str) : dsp->getSampleRate();
        str         = cs->get( "channel");
        channel     = str ? Util::strToL( str) : dsp->getChannel();

        // determine fixed bitrate or variable bitrate quality
        str         = cs->get( "bitrate");
        bitrate     = str ? Util::strToL( str) : 0;
        str         = cs->get( "maxBitrate");
        maxBitrate  = str ? Util::strToL( str) : 0;
        str         = cs->get( "quality");
        quality     = str ? Util::strToD( str) : 0.0;
        
        str         = cs->getForSure( "bitrateMode",
                                      " not specified in section ",
                                      stream);
        if ( Util::strEq( str, "cbr") ) {
            bitrateMode = AudioEncoder::cbr;
            
            if ( bitrate == 0 ) {
                throw Exception( __FILE__, __LINE__,
                                 "bitrate not specified for CBR encoding");
            }
        } else if ( Util::strEq( str, "abr") ) {
            bitrateMode = AudioEncoder::abr;

            if ( bitrate == 0 ) {
                throw Exception( __FILE__, __LINE__,
                                 "bitrate not specified for ABR encoding");
            }
        } else if ( Util::strEq( str, "vbr") ) {
            bitrateMode = AudioEncoder::vbr;

            if ( cs->get( "quality" ) == 0 ) {
                throw Exception( __FILE__, __LINE__,
                                 "quality not specified for VBR encoding");
            }
        } else {
            throw Exception( __FILE__, __LINE__,
                             "invalid bitrate mode: ", str);
        }

        server      = cs->getForSure( "server", " missing in section ", stream);
        str         = cs->getForSure( "port", " missing in section ", stream);
        port        = Util::strToL( str);
        password    = cs->getForSure("password"," missing in section ",stream);
        mountPoint  = cs->getForSure( "mountPoint",
                                      " missing in section ",
                                      stream);
        name        = cs->get( "name");
        description = cs->get( "description");
        url         = cs->get( "url");
        genre       = cs->get( "genre");
        str         = cs->get( "public");
        isPublic    = str ? (Util::strEq( str, "yes") ? true : false) : false;
        str         = cs->get( "lowpass");
        lowpass     = str ? Util::strToL( str) : 0;
        str         = cs->get( "highpass");
        highpass    = str ? Util::strToL( str) : 0;
        str         = cs->get( "fileAddDate");
        fileAddDate = str ? (Util::strEq( str, "yes") ? true : false) : false;
        fileDateFormat = cs->get( "fileDateFormat");
        
        localDumpName = cs->get( "localDumpFile");

        // go on and create the things

        // check for and create the local dump file if needed
        if ( localDumpName != 0 ) {
            if ( fileAddDate ) {
                if (fileDateFormat == 0) {
                    localDumpName = Util::fileAddDate(localDumpName);
                }
                else {
                    localDumpName = Util::fileAddDate(  localDumpName,
                                                        fileDateFormat );
                }
            }

            localDumpFile = new FileSink( stream, localDumpName);
            if ( !localDumpFile->exists() ) {
                if ( !localDumpFile->create() ) {
                    reportEvent( 1, "can't create local dump file",
                                    localDumpName);
                    localDumpFile = 0;
                }
            }
            if ( fileAddDate ) {
                delete[] localDumpName;
            }
        }

        // streaming related stuff
        audioOuts[u].socket = new TcpSocket( server, port);
        audioOuts[u].server = new IceCast2( audioOuts[u].socket.get(),
                                            password,
                                            mountPoint,
                                            format,
                                            bitrate,
                                            name,
                                            description,
                                            url,
                                            genre,
                                            isPublic,
                                            localDumpFile,
                                            bufferSecs );

        switch ( format ) {
            case IceCast2::mp3:
#ifndef HAVE_LAME_LIB
                throw Exception( __FILE__, __LINE__,
                                 "DarkIce not compiled with lame support, "
                                 "thus can't create mp3 stream: ",
                                 stream);
#else
                audioOuts[u].encoder = new LameLibEncoder(
                                                    audioOuts[u].server.get(),
                                                    dsp.get(),
                                                    bitrateMode,
                                                    bitrate,
                                                    quality,
                                                    sampleRate,
                                                    channel,
                                                    lowpass,
                                                    highpass );
#endif // HAVE_LAME_LIB
                break;


            case IceCast2::oggVorbis:
#ifndef HAVE_VORBIS_LIB
                throw Exception( __FILE__, __LINE__,
                                "DarkIce not compiled with Ogg Vorbis support, "
                                "thus can't Ogg Vorbis stream: ",
                                stream);
#else
                audioOuts[u].encoder = new VorbisLibEncoder(
                                                audioOuts[u].server.get(),
                                                dsp.get(),
                                                bitrateMode,
                                                bitrate,
                                                quality,
                                                sampleRate,
                                                dsp->getChannel(),
                                                maxBitrate);
#endif // HAVE_VORBIS_LIB
                break;

            case IceCast2::mp2:
#ifndef HAVE_TWOLAME_LIB
                throw Exception( __FILE__, __LINE__,
                                 "DarkIce not compiled with TwoLame support, "
                                 "thus can't create mp2 stream: ",
                                 stream);
#else
                audioOuts[u].encoder = new TwoLameLibEncoder(
                                                    audioOuts[u].server.get(),
                                                    dsp.get(),
                                                    bitrateMode,
                                                    bitrate,
                                                    sampleRate,
                                                    channel );
#endif // HAVE_TWOLAME_LIB
                break;


            case IceCast2::aac:
#ifndef HAVE_FAAC_LIB
                throw Exception( __FILE__, __LINE__,
                                "DarkIce not compiled with AAC support, "
                                "thus can't aac stream: ",
                                stream);
#else
                audioOuts[u].encoder = new FaacEncoder(
                                                audioOuts[u].server.get(),
                                                dsp.get(),
                                                bitrateMode,
                                                bitrate,
                                                quality,
                                                sampleRate,
                                                dsp->getChannel());
#endif // HAVE_FAAC_LIB
                break;

            default:
                throw Exception( __FILE__, __LINE__,
                                "Illegal stream format: ", format);
        }

        encConnector->attach( audioOuts[u].encoder.get());
    }

    noAudioOuts += u;
}


/*------------------------------------------------------------------------------
 *  Look for the ShoutCast stream outputs in the config file
 *----------------------------------------------------------------------------*/
void
DarkIce :: configShoutCast (    const Config      & config,
                                unsigned int        bufferSecs  )
                                                        throw ( Exception )
{
    // look for Shoutcast encoder output streams,
    // sections [shoutcast-0], [shoutcast-1], ...
    char            stream[]        = "shoutcast- ";
    size_t          streamLen       = Util::strLen( stream);
    unsigned int    u;

    for ( u = noAudioOuts; u < maxOutput; ++u ) {
        const ConfigSection    * cs;

        // ugly hack to change the section name to "stream0", "stream1", etc.
        stream[streamLen-1] = '0' + (u - noAudioOuts);

        if ( !(cs = config.get( stream)) ) {
            break;
        }

#ifndef HAVE_LAME_LIB
        throw Exception( __FILE__, __LINE__,
                         "DarkIce not compiled with lame support, "
                         "thus can't connect to ShoutCast, stream: ",
                         stream);
#else

        const char                * str;

        unsigned int                sampleRate      = 0;
        unsigned int                channel         = 0;
        AudioEncoder::BitrateMode   bitrateMode;
        unsigned int                bitrate         = 0;
        double                      quality         = 0.0;
        const char                * server          = 0;
        unsigned int                port            = 0;
        const char                * password        = 0;
        const char                * name            = 0;
        const char                * url             = 0;
        const char                * genre           = 0;
        bool                        isPublic        = false;
        int                         lowpass         = 0;
        int                         highpass        = 0;
        const char                * irc             = 0;
        const char                * aim             = 0;
        const char                * icq             = 0;
        const char                * localDumpName   = 0;
        FileSink                  * localDumpFile   = 0;
        bool                        fileAddDate     = false;
        const char                * fileDateFormat  = 0;

        str         = cs->get( "sampleRate");
        sampleRate  = str ? Util::strToL( str) : dsp->getSampleRate();
        str         = cs->get( "channel");
        channel     = str ? Util::strToL( str) : dsp->getChannel();

        str         = cs->get( "bitrate");
        bitrate     = str ? Util::strToL( str) : 0;
        str         = cs->get( "quality");
        quality     = str ? Util::strToD( str) : 0.0;
        
        str         = cs->getForSure( "bitrateMode",
                                      " not specified in section ",
                                      stream);
        if ( Util::strEq( str, "cbr") ) {
            bitrateMode = AudioEncoder::cbr;
            
            if ( bitrate == 0 ) {
                throw Exception( __FILE__, __LINE__,
                                 "bitrate not specified for CBR encoding");
            }
            if ( cs->get( "quality" ) == 0 ) {
                throw Exception( __FILE__, __LINE__,
                                 "quality not specified for CBR encoding");
            }
        } else if ( Util::strEq( str, "abr") ) {
            bitrateMode = AudioEncoder::abr;

            if ( bitrate == 0 ) {
                throw Exception( __FILE__, __LINE__,
                                 "bitrate not specified for ABR encoding");
            }
        } else if ( Util::strEq( str, "vbr") ) {
            bitrateMode = AudioEncoder::vbr;

            if ( cs->get( "quality" ) == 0 ) {
                throw Exception( __FILE__, __LINE__,
                                 "quality not specified for VBR encoding");
            }
        } else {
            throw Exception( __FILE__, __LINE__,
                             "invalid bitrate mode: ", str);
        }

        server      = cs->getForSure( "server", " missing in section ", stream);
        str         = cs->getForSure( "port", " missing in section ", stream);
        port        = Util::strToL( str);
        password    = cs->getForSure("password"," missing in section ",stream);
        name        = cs->get( "name");
        url         = cs->get( "url");
        genre       = cs->get( "genre");
        str         = cs->get( "public");
        isPublic    = str ? (Util::strEq( str, "yes") ? true : false) : false;
        str         = cs->get( "lowpass");
        lowpass     = str ? Util::strToL( str) : 0;
        str         = cs->get( "highpass");
        highpass    = str ? Util::strToL( str) : 0;
        irc         = cs->get( "irc");
        aim         = cs->get( "aim");
        icq         = cs->get( "icq");
        str         = cs->get("fileAddDate");
        fileAddDate = str ? (Util::strEq( str, "yes") ? true : false) : false;
        fileDateFormat = cs->get( "fileDateFormat");

        localDumpName = cs->get( "localDumpFile");

        // go on and create the things

        // check for and create the local dump file if needed
        if ( localDumpName != 0 ) {
            if ( fileAddDate ) {
                if (fileDateFormat == 0) {
                    localDumpName = Util::fileAddDate(localDumpName);
                }
                else {
                    localDumpName = Util::fileAddDate(  localDumpName,
                                                        fileDateFormat );
                }
            }

            localDumpFile = new FileSink( stream, localDumpName);
            if ( !localDumpFile->exists() ) {
                if ( !localDumpFile->create() ) {
                    reportEvent( 1, "can't create local dump file",
                                    localDumpName);
                    localDumpFile = 0;
                }
            }
            if ( fileAddDate ) {
                delete[] localDumpFile;
            }
        }

        // streaming related stuff
        audioOuts[u].socket = new TcpSocket( server, port);
        audioOuts[u].server = new ShoutCast( audioOuts[u].socket.get(),
                                             password,
                                             bitrate,
                                             name,
                                             url,
                                             genre,
                                             isPublic,
                                             irc,
                                             aim,
                                             icq,
                                             localDumpFile,
                                             bufferSecs );

        audioOuts[u].encoder = new LameLibEncoder( audioOuts[u].server.get(),
                                                   dsp.get(),
                                                   bitrateMode,
                                                   bitrate,
                                                   quality,
                                                   sampleRate,
                                                   channel,
                                                   lowpass,
                                                   highpass );

        encConnector->attach( audioOuts[u].encoder.get());
#endif // HAVE_LAME_LIB
    }

    noAudioOuts += u;
}


/*------------------------------------------------------------------------------
 *  Look for the FileCast stream outputs in the config file
 *----------------------------------------------------------------------------*/
void
DarkIce :: configFileCast (  const Config      & config )
                                                        throw ( Exception )
{
    // look for FileCast encoder output streams,
    // sections [file-0], [file-1], ...
    char            stream[]        = "file- ";
    size_t          streamLen       = Util::strLen( stream);
    unsigned int    u;

    for ( u = noAudioOuts; u < maxOutput; ++u ) {
        const ConfigSection    * cs;

        // ugly hack to change the section name to "stream0", "stream1", etc.
        stream[streamLen-1] = '0' + (u - noAudioOuts);

        if ( !(cs = config.get( stream)) ) {
            break;
        }

        const char                * str;

        const char                * format          = 0;
        AudioEncoder::BitrateMode   bitrateMode;
        unsigned int                bitrate         = 0;
        double                      quality         = 0.0;
        const char                * targetFileName  = 0;
        unsigned int                sampleRate      = 0;
        int                         lowpass         = 0;
        int                         highpass        = 0;
        bool                        fileAddDate     = false;
        const char                * fileDateFormat  = 0;

        format      = cs->getForSure( "format", " missing in section ", stream);
        if ( !Util::strEq( format, "vorbis")
          && !Util::strEq( format, "mp3")
          && !Util::strEq( format, "mp2")
          && !Util::strEq( format, "aac") ) {
            throw Exception( __FILE__, __LINE__,
                             "unsupported stream format: ", format);
        }

        str         = cs->getForSure("bitrate", " missing in section ", stream);
        bitrate     = Util::strToL( str);
        targetFileName    = cs->getForSure( "fileName",
                                            " missing in section ",
                                            stream);

        str         = cs->get( "fileAddDate");
        fileAddDate = str ? (Util::strEq( str, "yes") ? true : false) : false;
        fileDateFormat = cs->get( "fileDateFormat");

        str         = cs->get( "sampleRate");
        sampleRate  = str ? Util::strToL( str) : dsp->getSampleRate();

        str         = cs->get( "bitrate");
        bitrate     = str ? Util::strToL( str) : 0;
        str         = cs->get( "quality");
        quality     = str ? Util::strToD( str) : 0.0;
        
        str         = cs->getForSure( "bitrateMode",
                                      " not specified in section ",
                                      stream);
        if ( Util::strEq( str, "cbr") ) {
            bitrateMode = AudioEncoder::cbr;
            
            if ( bitrate == 0 ) {
                throw Exception( __FILE__, __LINE__,
                                 "bitrate not specified for CBR encoding");
            }
        } else if ( Util::strEq( str, "abr") ) {
            bitrateMode = AudioEncoder::abr;

            if ( bitrate == 0 ) {
                throw Exception( __FILE__, __LINE__,
                                 "bitrate not specified for ABR encoding");
            }
        } else if ( Util::strEq( str, "vbr") ) {
            bitrateMode = AudioEncoder::vbr;

            if ( cs->get( "quality" ) == 0 ) {
                throw Exception( __FILE__, __LINE__,
                                 "quality not specified for VBR encoding");
            }
        } else {
            throw Exception( __FILE__, __LINE__,
                             "invalid bitrate mode: ", str);
        }

        if (Util::strEq(format, "aac") && bitrateMode != AudioEncoder::abr) {
            throw Exception(__FILE__, __LINE__,
                            "currently the AAC format only supports "
                            "average bitrate mode");
        }

        str         = cs->get( "lowpass");
        lowpass     = str ? Util::strToL( str) : 0;
        str         = cs->get( "highpass");
        highpass    = str ? Util::strToL( str) : 0;

        // go on and create the things

        // the underlying file
        if ( fileAddDate ) {
            if (fileDateFormat == 0) {
                targetFileName = Util::fileAddDate( targetFileName);
            }
            else {
                targetFileName = Util::fileAddDate( targetFileName,
                                                    fileDateFormat );
            }
        }

        FileSink  * targetFile = new FileSink( stream, targetFileName);
        if ( !targetFile->exists() ) {
            if ( !targetFile->create() ) {
                throw Exception( __FILE__, __LINE__,
                                 "can't create output file", targetFileName);
            }
        }

        // streaming related stuff
        audioOuts[u].socket = 0;
        audioOuts[u].server = new FileCast( targetFile );

        if ( Util::strEq( format, "mp3") ) {
#ifndef HAVE_LAME_LIB
                throw Exception( __FILE__, __LINE__,
                                 "DarkIce not compiled with lame support, "
                                 "thus can't create mp3 stream: ",
                                 stream);
#else
                audioOuts[u].encoder = new LameLibEncoder(
                                                    audioOuts[u].server.get(),
                                                    dsp.get(),
                                                    bitrateMode,
                                                    bitrate,
                                                    quality,
                                                    sampleRate,
                                                    dsp->getChannel(),
                                                    lowpass,
                                                    highpass );
#endif // HAVE_TWOLAME_LIB
        } else if ( Util::strEq( format, "mp2") ) {
#ifndef HAVE_TWOLAME_LIB
                throw Exception( __FILE__, __LINE__,
                                "DarkIce not compiled with TwoLAME support, "
                                "thus can't create MPEG Audio Layer 2 stream: ",
                                stream);
#else
                audioOuts[u].encoder = new TwoLameLibEncoder(
                                                    audioOuts[u].server.get(),
                                                    dsp.get(),
                                                    bitrateMode,
                                                    bitrate,
                                                    sampleRate,
                                                    dsp->getChannel() );
#endif // HAVE_TWOLAME_LIB
        } else if ( Util::strEq( format, "vorbis") ) {
#ifndef HAVE_VORBIS_LIB
                throw Exception( __FILE__, __LINE__,
                                "DarkIce not compiled with Ogg Vorbis support, "
                                "thus can't Ogg Vorbis stream: ",
                                stream);
#else
                audioOuts[u].encoder = new VorbisLibEncoder(
                                                    audioOuts[u].server.get(),
                                                    dsp.get(),
                                                    bitrateMode,
                                                    bitrate,
                                                    quality,
                                                    dsp->getSampleRate(),
                                                    dsp->getChannel() );
#endif // HAVE_VORBIS_LIB
        } else if ( Util::strEq( format, "aac") ) {
#ifndef HAVE_FAAC_LIB
                throw Exception( __FILE__, __LINE__,
                                "DarkIce not compiled with AAC support, "
                                "thus can't aac stream: ",
                                stream);
#else
                audioOuts[u].encoder = new FaacEncoder(
                                                audioOuts[u].server.get(),
                                                dsp.get(),
                                                bitrateMode,
                                                bitrate,
                                                quality,
                                                sampleRate,
                                                dsp->getChannel());
#endif // HAVE_FAAC_LIB
        } else {
                throw Exception( __FILE__, __LINE__,
                                "Illegal stream format: ", format);
        }

        encConnector->attach( audioOuts[u].encoder.get());
    }

    noAudioOuts += u;
}


/*------------------------------------------------------------------------------
 *  Set POSIX real-time scheduling, if super-user
 *----------------------------------------------------------------------------*/
void
DarkIce :: setRealTimeScheduling ( void )               throw ( Exception )
{
// Only if the OS has the POSIX real-time scheduling functions implemented.
#if defined( HAVE_SCHED_GETSCHEDULER ) && defined( HAVE_SCHED_GETPARAM )
    uid_t   euid;

    euid = geteuid();

    if ( euid == 0 ) {
        int                 high_priority;
        struct sched_param  param;

        /* store the old scheduling parameters */
        if ( (origSchedPolicy = sched_getscheduler(0)) == -1 ) {
            throw Exception( __FILE__, __LINE__, "sched_getscheduler", errno);
        }

        if ( sched_getparam( 0, &param) == -1 ) {
            throw Exception( __FILE__, __LINE__, "sched_getparam", errno);
        }
        origSchedPriority = param.sched_priority;
        
        /* set SCHED_FIFO with max - 1 priority */
        if ( (high_priority = sched_get_priority_max(SCHED_FIFO)) == -1 ) {
            throw Exception(__FILE__,__LINE__,"sched_get_priority_max",errno);
        }
        reportEvent( 8, "scheduler high priority", high_priority);

        param.sched_priority = high_priority - 1;

        if ( sched_setscheduler( 0, SCHED_FIFO, &param) == -1 ) {
            throw Exception( __FILE__, __LINE__, "sched_setscheduler", errno);
        }

        /* ask the new priortiy and report it */
        if ( sched_getparam( 0, &param) == -1 ) {
            throw Exception( __FILE__, __LINE__, "sched_getparam", errno);
        }

        reportEvent( 1,
                     "Using POSIX real-time scheduling, priority",
                     param.sched_priority );
    } else {
        reportEvent( 1,
        "Not running as super-user, unable to use POSIX real-time scheduling" );
        reportEvent( 1,
        "It is recommended that you run this program as super-user");
    }
#else
    reportEvent( 1, "POSIX scheduling not supported on this system, "
                    "this may cause recording skips");
#endif // HAVE_SCHED_GETSCHEDULER && HAVE_SCHED_GETPARAM
}


/*------------------------------------------------------------------------------
 *  Set the original scheduling of the process, the one prior to the
 *  setRealTimeScheduling call.
 *  WARNING: make sure you don't call this before setRealTimeScheduling!!
 *----------------------------------------------------------------------------*/
void
DarkIce :: setOriginalScheduling ( void )               throw ( Exception )
{
// Only if the OS has the POSIX real-time scheduling functions implemented.
#if defined( HAVE_SCHED_GETSCHEDULER ) && defined( HAVE_SCHED_GETPARAM )
    uid_t   euid;

    euid = geteuid();

    if ( euid == 0 ) {
        struct sched_param  param;

        if ( sched_getparam( 0, &param) == -1 ) {
            throw Exception( __FILE__, __LINE__, "sched_getparam", errno);
        }

        param.sched_priority = origSchedPriority;

        if ( sched_setscheduler( 0, origSchedPolicy, &param) == -1 ) {
            throw Exception( __FILE__, __LINE__, "sched_setscheduler", errno);
        }

        reportEvent( 5, "reverted to original scheduling");
    }
#endif // HAVE_SCHED_GETSCHEDULER && HAVE_SCHED_GETPARAM
}


/*------------------------------------------------------------------------------
 *  Run the encoder
 *----------------------------------------------------------------------------*/
bool
DarkIce :: encode ( void )                          throw ( Exception )
{
    unsigned int       len;
    unsigned long      bytes;

    if ( !encConnector->open() ) {
        throw Exception( __FILE__, __LINE__, "can't open connector");
    }
    
    bytes = dsp->getSampleRate() *
            (dsp->getBitsPerSample() / 8UL) *
            dsp->getChannel() *
            duration;
                                                
    len = encConnector->transfer( bytes, 4096, 1, 0 );

    reportEvent( 1, len, "bytes transfered to the encoders");

    encConnector->close();

    return true;
}


/*------------------------------------------------------------------------------
 *  Run
 *----------------------------------------------------------------------------*/
int
DarkIce :: run ( void )                             throw ( Exception )
{
    reportEvent( 3, "encoding");
    
    if (enableRealTime) {
        setRealTimeScheduling();
    }
    encode();
    if (enableRealTime) {
        setOriginalScheduling();
    }
    reportEvent( 3, "encoding ends");

    return 0;
}


/*------------------------------------------------------------------------------
 *  Tell each sink to cut what they are doing, and start again.
 *----------------------------------------------------------------------------*/
void
DarkIce :: cut ( void )                             throw ()
{
    reportEvent( 5, "cutting");

    encConnector->cut();

    reportEvent( 5, "cutting ends");
}

