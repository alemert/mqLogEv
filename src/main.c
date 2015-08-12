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
#include <regex.h>

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
#include <msgcat/lgstd.h>
#include <mqdump.h>

// ---------------------------------------------------------
// local
// ---------------------------------------------------------
#include <mqLogEv.h>
#include <cmqbc.h>

/******************************************************************************/
/*   D E F I N E S                                                            */
/******************************************************************************/
#define LOG_DIRECTORY   "/var/mqm/errors/appl"
#define START_MODE_TRIGGER   0
#define START_MODE_CMDLN     1
#define TRIGGER_ATTR_CNT     8

/******************************************************************************/
/*   M A C R O S                                                              */
/******************************************************************************/

/******************************************************************************/
/*   P R O T O T Y P E S                                                      */
/******************************************************************************/
int userData2argv( char *uData, char*** pArgv );

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
  char userData[MQ_PROCESS_USER_DATA_LENGTH+1];
  char **triggArgv = NULL;
  int  triggArgc = 0;
  char *bckPath = NULL;

  char logDir[PATH_MAX];
  char logName[PATH_MAX+NAME_MAX];
//int logLevel = LNA ;   // log level not available
  int logLevel = DBG ;   // log level not available

  int sysRc = 0 ;
  int compCode = MQCC_UNKNOWN;
  int reason   = MQRC_NONE;

  // -------------------------------------------------------
  // initialize      
  // -------------------------------------------------------
  memset( qmgrName, ' ', MQ_Q_MGR_NAME_LENGTH );
  memset( qName   , ' ', MQ_Q_NAME_LENGTH     );
  qmgrName[MQ_Q_MGR_NAME_LENGTH] = '\0' ;
  qName[MQ_Q_NAME_LENGTH] = '\0' ;    
  userData[MQ_PROCESS_USER_DATA_LENGTH]='\0';

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
#if(0)
  printf( "%d\n",strlen(argv[1]));
  printf( "%d\n",sizeof(MQTMC2) )   ;
#endif
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
    startMode = START_MODE_TRIGGER;           //  get data from MQTMC2 structure
                                              //
    memcpy( &trigData        ,                // get trigger data from 
             argv[1]         ,                //   real command line
             sizeof(MQTMC2) );                // 
                                              //
    memcpy( qmgrName              ,           // get queue manager name from TMC
            trigData.QMgrName     ,           // 
            MQ_Q_MGR_NAME_LENGTH );           //
                                              //
    memcpy( qName                 ,           // get queue name from TMC
            trigData.QName        ,           //
            MQ_Q_NAME_LENGTH     );           //
                                              //
    mqTrim( MQ_PROCESS_USER_DATA_LENGTH,      // get user data from TMC
            trigData.UserData          ,      //
            userData                   ,      //
            &compCode                  ,      //
            &reason                   );      //
                                              // check if trimming 
    if( reason != MQRC_NONE )                 //   user data was OK
    {                                         //
      fprintf( stderr, "mqTrim on MQTMC.UserData failed\n");
      sysRc = reason;                         // exit if trimming failed
      goto _door;                             //
    }                                         //
                                              //
    if( strlen(userData) > 0 )                // check if there is something in
    {                                         //   user data
      triggArgc = userData2argv( userData,    // if so -> convert it to command 
                                &triggArgv ); //   line format
                                              //
      sprintf(triggArgv[0],argv[0]);          //
      sprintf(triggArgv[triggArgc],"--queue")    ; triggArgc++;
      sprintf(triggArgv[triggArgc],"%s",qName)   ; triggArgc++;
      sprintf(triggArgv[triggArgc],"--qmgr")     ; triggArgc++;
      sprintf(triggArgv[triggArgc],"%s",qmgrName); triggArgc++;
                                              //
      sysRc = handleCmdLn( triggArgc,         //
                           (const char**) triggArgv );  
                                              //
      if( sysRc != 0 ) goto _door;            //
                                              //
    }                                         //  
  }                                           //
                                              //
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

/******************************************************************************/
/*   U S E R   D A T A   T O   C O M M A N D   L I N E   A R G U M E N T S    */
/******************************************************************************/
int userData2argv( char *uData, char*** pArgv )
{
  int sysRc = 0; //function return code

  int i;      // counter
  int j;      // counter
  int len;    // length

  char** argv =NULL;
  int argc = 0 ;

  #define RX_MATCH_CNT     16                 // max nr of matches 3*3+1 < 16
  #define RX_ERR_BUFF_LNG 100                 // regex error message buffer
  #define RX_KEY_VAL "(\\w+)=([[:graph:]]+)"  // regular expression key=val
  #define RX_BLANK   "\\s"                    // regular expression blank

  char rxNat[] = "^("RX_BLANK"*"RX_KEY_VAL")?"    // up to 5 key=value pairs
                  "("RX_BLANK"+"RX_KEY_VAL")?"    // can be matched
                  "("RX_BLANK"+"RX_KEY_VAL")?"    // RX_MATCH_CNT = 16 
                  "("RX_BLANK"+"RX_KEY_VAL")?"    // 5*3+1=16
                  "("RX_BLANK"+"RX_KEY_VAL")?" ;  

  regex_t    rxComp ;               // regular expression compiled
  regmatch_t rxMatch[RX_MATCH_CNT]; // regular expression match array
  char rxErrBuff[RX_ERR_BUFF_LNG];  // regular expression error buffer

  // -------------------------------------------------------
  // compile regular expression 
  // -------------------------------------------------------
  sysRc = regcomp( &rxComp, rxNat, REG_NEWLINE +  // match one line only
                                   REG_NOTBOL  +  // enable ^
                                   REG_NOTEOL  );  // enable $
                               //  REG_EXTENDED );
  if( sysRc != REG_NOERROR )
  {
    regerror( sysRc, &rxComp,rxErrBuff,RX_ERR_BUFF_LNG);
    logger( LSTD_XML_REGEX_CC_ERR, rxErrBuff );
    sysRc = (sysRc > 0) ? -sysRc : sysRc ;
    goto _door;
  }

  // -------------------------------------------------------
  // execute regular expression
  // -------------------------------------------------------
  sysRc = regexec( &rxComp, uData, RX_MATCH_CNT, rxMatch, 0 );
  if( sysRc != REG_NOERROR )
  {
    regerror( sysRc, &rxComp,rxErrBuff,RX_ERR_BUFF_LNG);
    logger( LSTD_XML_REGEX_EXEC_ERR, rxErrBuff ) ;
    sysRc = (sysRc > 0) ? -sysRc : sysRc ;
    goto _door;
  }

  // -------------------------------------------------------
  // check if regular expression worked
  // -------------------------------------------------------
  if( (int)strlen(uData) != (int)rxMatch[0].rm_eo )
  {
    //printf( "userData %d : match Length %d\n",strlen(uData),rxMatch[0].rm_eo);
    // error message
    sysRc = -1 ;
    goto _door;
  }

  // -------------------------------------------------------
  // count key=value pairs
  // -------------------------------------------------------
  for( i=0; i<RX_MATCH_CNT; i++ )  // group 0 is whole user data -> ignore
  {                                // other groups consist of three sub-matches: 
    if( rxMatch[i].rm_so < 0 ||    // 1st group: key=value -> ignore
        rxMatch[i].rm_eo < 0  )    // 2nd group: key
    {                              // 3th group: value
      argc = i/3*2 ;               // i/3    -> number of key=value pairs
      break;                       // i/3*2  -> number of arguments
    }                              //
  }                                //
                                   //
  // -------------------------------------------------------
  // allocate argument - array
  // -------------------------------------------------------
  argv=(char**)malloc(sizeof(char*)*(argc+6));// +1 for program name
                                              // +2 for queue 
  argv[argc+5] = NULL;                        // +2 for queue manager
  argc++;                                     // +1 to set last argument to null

  *pArgv = argv ;

  // -------------------------------------------------------
  // allocate and initialize argument fields
  // -------------------------------------------------------
  for( i=1, j=1; i<RX_MATCH_CNT; i++ )  // group 0 is whole user data -> ignore
  {                                     // ignore group 0 by starting at Ix 1
    len = rxMatch[i].rm_eo -            // end of group
          rxMatch[i].rm_so ;            // start of group
                                        // j=1 since argv[0] == program name
    if( j == argc ) break;
                                        //
    // -----------------------------------------------------
    // analyze result of regular expression matches
    // -----------------------------------------------------
    switch( i%3 )                       // group consists of 3 sub groups
    {                                   //
      // ---------------------------------------------------
      // 1st group: key=value -> ignore
      // ---------------------------------------------------
      case 1: continue;         

      // ---------------------------------------------------
      // 2nd group: key
      // ---------------------------------------------------
      case 2:                           // to transform key to arguments add -- 
      {                                 // (log -> --log)
	len += 2;                       //
        argv[j] = (char*)  malloc( sizeof(char)*(len+1));
	snprintf( argv[j], len+1, "--%s",(uData+rxMatch[i].rm_so));
        j++;
        break;                          //
      }                                 //

      // ---------------------------------------------------
      // 3th group: value 
      // ---------------------------------------------------
      case 0:                           // nothing to be transformed
      {                                 // 
        argv[j] = (char*)  malloc( sizeof(char)*(len+1));
	snprintf( argv[j], len+1, "%s",(uData+rxMatch[i].rm_so));
        j++;
        break; 
      } // case 3
    }   // switch  
  }     // for    
        
  _door:

  if( argv == NULL )
  {
    argc=0;
    argv=(char**)malloc(sizeof(char*)*(argc+5));
  }

  argv[0]      = (char*) malloc(sizeof(char)*NAME_MAX) ;
  argv[argc+0] = (char*) malloc( sizeof(char) * (sizeof("--queue ")+1) );
  argv[argc+2] = (char*) malloc( sizeof(char) * (sizeof("--queue ")+1) );
  argv[argc+1] = (char*) malloc( sizeof(char) * MQ_Q_NAME_LENGTH );
  argv[argc+3] = (char*) malloc( sizeof(char) * MQ_Q_NAME_LENGTH );
  argv[argc+4] = NULL ;

  regfree(&rxComp);

  if( sysRc < 0 ) return sysRc;
  return argc ;
}
