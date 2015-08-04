/******************************************************************************/
/*                        M Q   L O G G E R   E V E N T                       */
/*                                                                            */
/*    description:                                                            */
/*      program for cleaning up MQ transaction logs                           */
/*      program can be called by command line or triggered by trigger monitor */
/*      Program can backup transactional logs to backup directory, in this    */
/*      case backup logs will be compressed.                                  */
/*      after record MQ image,  all archive logs will be removed from the     */
/*      original directory.                                                   */
/*                                    */
/*    attributes:                                                             */
/*      -m --qmgr  : queue manager name, default queue manager not possible   */
/*      -q --queue : event queue name                                         */
/*      -d --log   : path to error log directory                    */
/*      -l --loglev: logging level                                       */
/*      -b --bck   : path to backup directory,                             */
/*                   if not set, no backup of tx files only removing them.    */
/*                                                                            */
/******************************************************************************/

/******************************************************************************/
/*   I N C L U D E S                                                          */
/******************************************************************************/

// ---------------------------------------------------------
// system
// ---------------------------------------------------------
#include <string.h>

// ---------------------------------------------------------
// MQ
// ---------------------------------------------------------
#include <cmqc.h>
#include <cmqcfc.h>

// ---------------------------------------------------------
// own 
// ---------------------------------------------------------
#include "main.h"
#include <ctl.h>
#include <mqdump.h>

// ---------------------------------------------------------
// local
// ---------------------------------------------------------
#include <mqLogEv.h>

/******************************************************************************/
/*   D E F I N E S                                                            */
/******************************************************************************/
#define LOG_DIRECTORY   "/var/mqm/errors/appl"
#define START_MODE_TRIGGER   0
#define START_MODE_CMDLN     1

/******************************************************************************/
/*   M A C R O S                                                              */
/******************************************************************************/

/******************************************************************************/
/*   P R O T O T Y P E S                                                      */
/******************************************************************************/

/******************************************************************************/
/*                                                                            */
/*                                  M A I N                                   */
/*                                                                            */
/******************************************************************************/
#ifndef __TDD__

int main(int argc, const char* argv[] )
{
  MQTMC2 trigData ;

  int  startMode = START_MODE_CMDLN ;
  char qmgrName[MQ_Q_MGR_NAME_LENGTH+1];
  char qName[MQ_Q_NAME_LENGTH+1]       ;
  char *bckPath = NULL;

  char logDir[PATH_MAX];
  char logName[PATH_MAX+NAME_MAX];
//int logLevel = LNA ;   // log level not available
  int logLevel = DBG ;   // log level not available

  int sysRc = 0 ;

  // -------------------------------------------------------
  // initialize      
  // -------------------------------------------------------
  memset( qmgrName, ' ', MQ_Q_MGR_NAME_LENGTH );
  memset( qName   , ' ', MQ_Q_NAME_LENGTH     );
  qmgrName[MQ_Q_MGR_NAME_LENGTH] = '\0' ;
  qName[MQ_Q_NAME_LENGTH] = '\0' ;    

  /************************************************************************/  
  /*  this program can be called by command line or by trigger monitor    */
  /*  first find out how program has been called and set attributes       */
  /************************************************************************/  
  if( argc == 1 )
  {
    usage();
    goto _door;
  }

  // -------------------------------------------------------
  // handle command line call
  // -------------------------------------------------------
  if( strlen(argv[1]) != sizeof(MQTMC2) )   
  {                                        
    sysRc = handleCmdLn( argc, argv ) ;   
    if( sysRc != 0 ) goto _door ;        
    if( getStrAttr( "queue" ) )
    {
      memcpy( qName ,  getStrAttr( "queue" ), strlen( getStrAttr( "queue" ) ));
    }
    else
    {
      memcpy( qName, LOGGER_QUEUE, sizeof(LOGGER_QUEUE) );
    }
    memcpy( qmgrName,  getStrAttr( "qmgr"  ), strlen( getStrAttr( "qmgr"  ) ));
  }                 
  // -------------------------------------------------------
  // handle trigger monitor call
  // -------------------------------------------------------
  else            
  {              
    startMode = START_MODE_TRIGGER ;
    memcpy( qmgrName   , trigData.QMgrName, MQ_Q_MGR_NAME_LENGTH      );
    memcpy( qName      , trigData.QName   , MQ_Q_NAME_LENGTH          );
  }           

  // -------------------------------------------------------
  // setup the logging
  // -------------------------------------------------------
  if( getStrAttr( "log") )
  {
    snprintf( logDir, PATH_MAX, "%s", getStrAttr( "log") ) ;
  }
  else
  {
    snprintf( logDir, PATH_MAX, "%s", LOG_DIRECTORY );
  }

  if( getStrAttr( "loglev" ) )
  {
    logLevel = logStr2lev( getStrAttr( "loglev" ) );
  }

  if( logLevel == LNA )
  {
     logLevel = LOG;
  }

  snprintf( logName, PATH_MAX+NAME_MAX, "%s/%s.log", logDir, progname );

  sysRc = initLogging( (const char*) logName, logLevel );

  if( sysRc != 0 ) goto _door ;

  if( startMode == START_MODE_TRIGGER )
  {
    dumpMqStruct( MQTMC_STRUC_ID,  &trigData, NULL  );
  }

  // -------------------------------------------------------
  // get backup path; in general backup path might stay null
  // -------------------------------------------------------
  bckPath = (char*) getStrAttr( "backup" ) ;

  // -------------------------------------------------------
  // cleanup the logs
  // -------------------------------------------------------
  sysRc = cleanupLog( qmgrName, 
                      qName   ,
                      bckPath);

  if( sysRc != MQRC_NONE ) goto _door ;

_door :

  return sysRc ;
}

#endif

/******************************************************************************/
/*                                                                            */
/*   F U N C T I O N S                                                        */
/*                                                                            */
/******************************************************************************/

