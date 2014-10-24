/******************************************************************************/
/* change title on for new project                                            */
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
  char qmgrName[MQ_Q_MGR_NAME_LENGTH] ;
  char qName[MQ_Q_NAME_LENGTH] ;
  char iniFileName[255] ;

  int sysRc = 0 ;


  // -------------------------------------------------------
  // initialize      
  // -------------------------------------------------------
  memset( qmgrName, ' ', MQ_Q_MGR_NAME_LENGTH );
  memset( qName   , ' ', MQ_Q_NAME_LENGTH     );

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
    memcpy( qName   ,  getStrAttr( "queue" ), strlen( getStrAttr( "queue" ) ));
    memcpy( qmgrName,  getStrAttr( "qmgr"  ), strlen( getStrAttr( "qmgr"  ) ));
//  qName[   strlen(qName)   ] = ' ' ;
//  qmgrName[strlen(qmgrName)] = ' ' ;
  }                 
  // -------------------------------------------------------
  // handle trigger monitor call
  // -------------------------------------------------------
  else            
  {              
    memcpy( &trigData, argv[1], sizeof(MQTMC2) ) ;  
    dumpTrigData( &trigData ) ;
    memcpy( qmgrName   , trigData.QMgrName, MQ_Q_MGR_NAME_LENGTH      );
    memcpy( qName      , trigData.QName   , MQ_Q_NAME_LENGTH          );
    memcpy( iniFileName, trigData.UserData, sizeof(trigData.UserData) );
  }           

  sysRc = initLogging("/var/mqm/errors/appl/mqLogEv.log",DBG);

  if( sysRc != 0 ) goto _door ;

  sysRc = cleanupLog( qmgrName, qName );

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

