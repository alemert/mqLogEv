/******************************************************************************/
/*                                                                            */
/*                       M Q   L O G G E R   E V E N T                        */
/*                                                                            */
/*   functions:                                                               */
/*     - cleanupLog                                                           */
/*     - pcfReadQueue                                                         */
/*     - mqOlderLog                                                           */
/*     - mqLogName2Id                                                         */
/*     - mqHandleLog                                                          */
/*     - mqCheckLogName                                                       */
/*     - mqCloseDisconnect                                                  */
/*     - rcdMqImg                                              */
/*     - mqCopyLog                                          */
/*     - callZipFile                      */
/*     - getQmgrStatus                                                */
/*                                                        */
/******************************************************************************/
#define C_MODULE_MQLOGEV

/******************************************************************************/
/*                              I N C L U D E S                               */
/******************************************************************************/

// ---------------------------------------------------------
// system
// ---------------------------------------------------------
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <string.h>

// ---------------------------------------------------------
//  MQ
// ---------------------------------------------------------
#include <cmqc.h>

// ---------------------------------------------------------
// own
// ---------------------------------------------------------
#include <ctl.h>
#include <msgcat/lgstd.h>
#include <msgcat/lgmqm.h>
#include <genlib.h>

// ---------------------------------------------------------
// local
// ---------------------------------------------------------
#include "mqLogEv.h"

/******************************************************************************/
/*   D E F I N E S                                                            */
/******************************************************************************/
#define COPY_BUFF_LNG 4096

#define NONE   0 
#define ALL    1
#define MIN    2
#define DEL    3

#define MQ_DEFAULT_INSTALLATION_PATH "/opt/mqm/"
#define RCDMQIMG  "rcdmqimg"

#define RCDMQIMG_CMD_LENGTH  MQ_INSTALLATION_PATH_LENGTH + \
                             sizeof(RCDMQIMG) + 5

/******************************************************************************/
/*                               S T R U C T S                                */
/******************************************************************************/

/******************************************************************************/
/*   P R O T O T Y P E S                                                      */
/******************************************************************************/
void usage() ;
MQLONG pcfReadQueue( MQHCONN  Hcon     ,  // connection handle
                     MQHOBJ   Hqueue   ,  // queue handle
//                   char*    logPath  ,  // logpath, max of 82+1 incl. log file 
                     char*    currLog  ,  // current log name, max of 12+1 
                     char*    recLog   ,  // record  log name, max of 12+1
                     char*    mediaLog);  // media   log name, max of 12+1

MQLONG mqCloseDisconnect( MQHCONN  Hcon    ,  // connection handle   
                         PMQHOBJ  Hqueue );  // queue handle   

int mqOlderLog( const char *log1, const char *log2) ;
int mqLogName2Id( const char* log );
int mqHandleLog( const char* logPath , 
                const char* bckPath  , 
                const char* oldestLog,
                const char* zipBin  );
int mqCheckLogName( const char* log) ;
int rcdMqImg( const char* _qmgr, const char* _instPath );

int mqCopyLog( const char* orgFile, const char* cpyFile );

int callZipFile( const char* zipBin, const char* file );

MQLONG getQmgrStatus( MQHCONN Hconn, tQmgrObjStatus* pQmgrObjStatus );

/******************************************************************************/
/*                                                                            */
/*                                   M A I N                                  */
/*                                                                            */
/******************************************************************************/
int cleanupLog( const char* qmgrName,  // queue manager name
                const char* qName   ,  // logger event name
                const char* bckPath ,  // backup path  
                const char* zipBin  )  // zip binary
{
  logFuncCall() ;                       //

  MQHCONN  Hcon   ;                    // connection handle   
  MQOD     qDscr  = {MQOD_DEFAULT};    // queue descriptor
  MQHOBJ   Hqueue ;                    // queue handle   

  tQmgrObjStatus qmgrObjStatus ;
//char instPath [MQ_INSTALLATION_PATH_LENGTH+1];
//char logPath[MQ_LOG_PATH_LENGTH+1]        ; // transactional log path
  char currLog[MQ_LOG_EXTENT_NAME_LENGTH+1] ; // current log name
  char recLog[MQ_LOG_EXTENT_NAME_LENGTH+1]  ; // record  log name
  char mediaLog[MQ_LOG_EXTENT_NAME_LENGTH+1]; // media   log name
  char oldLog[MQ_LOG_EXTENT_NAME_LENGTH+1]  ; // oldest  log name

  MQLONG sysRc = MQRC_NONE ;
  MQLONG locRc = MQRC_NONE ;

  // -------------------------------------------------------
  // connect to queue manager
  // -------------------------------------------------------
  sysRc =  mqConn( (char*) qmgrName,      // queue manager          
                           &Hcon  );      // connection handle            
                                          //
  switch( sysRc )                         //
  {                                       //
    case MQRC_NONE :     break ;          // OK
    case MQRC_Q_MGR_NAME_ERROR :          // queue manager does not exists
    {                                     //
      logger(LMQM_UNKNOWN_QMGR,qmgrName); //
      goto _door;                         //
    }                                     //
    default : goto _door;                 // error will be logged in mqConn
  }                                       //

  // -------------------------------------------------------
  // open queue
  // -------------------------------------------------------
  memcpy( qDscr.ObjectName, qName, MQ_Q_NAME_LENGTH ); 

  sysRc = mqOpenObject( Hcon                  , // connection handle
                        &qDscr                , // queue descriptor
                        MQOO_INPUT_EXCLUSIVE  | 
                        MQOO_SET              |
                        MQOO_FAIL_IF_QUIESCING, // open options
                        &Hqueue );              // queue handle

  switch( sysRc )
  {
    case MQRC_NONE : break ;
    default        : goto _door;
  }

  // -------------------------------------------------------
  // get installation path 
  // -------------------------------------------------------
  sysRc = getQmgrStatus( Hcon, &qmgrObjStatus );

  switch( sysRc )
  {
    case MQRC_NONE : break;
    default  : goto _door;
  }

  switch( qmgrObjStatus.reason )
  {
    case MQRC_NONE : break;
    default  : goto _door;
  }

  // -------------------------------------------------------
  // set trigger on
  // -------------------------------------------------------
  sysRc = mqSetTrigger( Hcon   ,     // connection handle
                        Hqueue );    // queue handle

  switch( sysRc )
  {
    case MQRC_NONE : break ;
    default        : goto _door;
  }

  // -------------------------------------------------------
  // backup old logs for later analyzes 
  // -------------------------------------------------------
  mqHandleLog( qmgrObjStatus.logPath,  // original log path
               bckPath              ,  // no copy
               NULL                 ,  // oldest log (keep 
               zipBin              );  // zip binary

  // -------------------------------------------------------
  // fork process for RCDMQIMG, log RCDMQIMG output to log
  // -------------------------------------------------------
  rcdMqImg( qmgrName, qmgrObjStatus.instPath ); // exec RCDMQIMG

  // -------------------------------------------------------
  // read all messages from the queue, 
  // get back only the last one for analyzing
  // -------------------------------------------------------
//logPath[0]  = '\0' ;
  currLog[0]  = '\0' ;
  recLog[0]   = '\0' ;
  mediaLog[0] = '\0' ;

  sysRc = pcfReadQueue( Hcon      , 
                        Hqueue    ,
                        currLog   , 
                        recLog    , 
                        mediaLog );
  switch( sysRc )
  {
    // -----------------------------------------------------
    // continue work, at least one message was found
    // -----------------------------------------------------
    case MQRC_NONE: 
    {
      break ;
    }
                    
    // -----------------------------------------------------
    // no message at all was found; 
    //   - write to log
    //   - close queue
    //   - disconnect qmgr
    //   - quit
    // -----------------------------------------------------
    case MQRC_NO_MSG_AVAILABLE : 
    {    
      logger( LMQM_QUEUE_EMPTY, qName ) ;
      goto _door ;
    }
 
    // -----------------------------------------------------
    // anything else is an error, go out of the function
    // -----------------------------------------------------
    default: 
    {
      logger( LSTD_GEN_SYS, progname );
      goto _door ; 
    }
  }

  logger( LMQM_LOG_NAME, "CURRENT", currLog  );
  logger( LMQM_LOG_NAME, "RECORD" , recLog   );
  logger( LMQM_LOG_NAME, "MEDIA"  , mediaLog );
//logger( LMQM_LOG_NAME, "PATH"   , logPath  );

#if(0)
  if( logPath[0]  == '\0' ||
      currLog[0]  == '\0' ||
      recLog[0]   == '\0' ||
      mediaLog[0] == '\0'  )
  {
    sysRc = MQRC_NO_MSG_AVAILABLE ;
    logger( LMQM_QUEUE_EMPTY, qName );
    goto _door ;
  }
#endif

  if( qmgrObjStatus.logPath[0]  != '/' ||
      currLog[0]  != 'S' ||
      recLog[0]   != 'S' ||
      mediaLog[0] != 'S'  )
  {
    logger( LMQM_LOG_NAME_INVALIDE ) ;
    goto _door;
  }
        
  if( mqOlderLog(recLog,mediaLog) > 0)
  { 
    strcpy( oldLog, recLog );
  }
  else
  { 
    strcpy( oldLog, mediaLog );
  }

  logger( LMQM_LOG_NAME, "OLDEST", oldLog ) ;

  // -------------------------------------------------------
  // remove old logs
  // -------------------------------------------------------
  mqHandleLog( qmgrObjStatus.logPath, // original log path
              NULL                  , // no copy
              oldLog                , // oldest log (keep 
              NULL                 ); // zip binary

  // -------------------------------------------------------
  // send reset qmgr type(advancedlog) to command server
  // -------------------------------------------------------
  sysRc = mqResetQmgrLog(Hcon) ;

  switch(sysRc)
  {
    case MQRC_NONE: break ;
    // ------------------------------------------------------
    // reset was not possible, no further processing
    // -----------------------------------------------------
    case MQRC_CMD_SERVER_NOT_AVAILABLE : goto _door ;
    default: goto _door;
  }

  _door:

  // -------------------------------------------------------
  // close queue
  // -------------------------------------------------------
  locRc = mqCloseObject( Hcon    ,      // connection handle
                         &Hqueue );     // queue handle

  switch( locRc )
  {
    case MQRC_NONE : break ;
    default        : logger( LSTD_GEN_SYS, progname );
                     break;
  }

  // -------------------------------------------------------
  // disconnect from queue manager
  // -------------------------------------------------------
  locRc =  mqDisc( &Hcon );    // connection handle            
  switch( locRc )
  {
    case MQRC_NONE : break ;
    default        : logger( LSTD_GEN_SYS, progname );
                     break;
  }

  if( sysRc == MQRC_NONE )
  {
    sysRc = locRc ;
  }

  logFuncExit() ;

  return sysRc ;
}

/******************************************************************************/
/*   R E A D   A   P C F   M E S S A G E   F R O M   A   Q U E U E            */
/******************************************************************************/
MQLONG pcfReadQueue( MQHCONN  Hcon    , // connection handle   
                     MQHOBJ   Hqueue  , // queue handle   
//                   char*    logPath , // logpath, max of 82+1 incl. log file 
                     char*    currLog , // current log name, max of 12+1 
                     char*    recLog  , // record  log name, max of 12+1
                     char*    mediaLog) // media   log name, max of 12+1
{                                       // 
  logFuncCall() ;                       //

  PMQVOID msg     ;                     // message buffer 
  MQMD    md      = {MQMD_DEFAULT} ;    // message descriptor
  MQGMO   gmo     = {MQGMO_DEFAULT};    // get message options
  MQLONG  msgSize = 512 ;               //
                                        //
  PMQCFH  pPCFh   ;                     // PCF (header) pointer
  PMQCHAR pPCFcmd ;                     // PCF command pointer
  PMQCFST pPCFstr ;                     // PCF string pointer
                                        //
  int      sysRc    = MQRC_NONE;        // general return code
  MQLONG   mqreason ;                   // MQ return code
  int      cnt      ;                   // message counter
  int      loop     = 0;                //
                                        //
                                        //
  // ---------------------------------------------------------
  // prepare get message options 
  //   and message descriptor for reading
  // ---------------------------------------------------------
  gmo.Version      = MQGMO_VERSION_2;    // avoid need to reset message id and
  gmo.MatchOptions = MQMO_NONE      ;    // corelation id after every MQGET
              //
  // ---------------------------------------------------------
  // read all messages from queue and count them
  // ---------------------------------------------------------
  cnt = 0 ;

  loop = 1;
  while(loop)
  {
    cnt++ ;
    // -------------------------------------------------------
    // read a single message from the queue
    // -------------------------------------------------------
    msg = (PMQVOID) malloc(msgSize*sizeof(char));
    mqreason = mqGet( Hcon    ,     // connection handle
                      Hqueue  ,     // object(queue) handle
                      msg     ,     // message buffer
                      &msgSize,     // message size
                      &md     ,     // message descriptor 
                      gmo     ,     // get message options
                      500    );     // wait interval (to adjust gmo)
  
    // -------------------------------------------------------
    // check if reading from a queue was OK
    // -------------------------------------------------------
    switch( mqreason )
    {
      // -----------------------------------------------------
      // stay in the loop, read next message
      // -----------------------------------------------------
      case MQRC_NONE: break ;
                    
      // -----------------------------------------------------
      // this is the last message, break out of the loop
      // -----------------------------------------------------
      case MQRC_NO_MSG_AVAILABLE : 
      { 
        // ---------------------------------------------------
        // at least one message found
        // ---------------------------------------------------
	sysRc = mqreason ;
        if( cnt > 1 ) 
        {
          sysRc = MQRC_NONE ;
        }

        loop = 0;
	continue ;
      }
  
      // -----------------------------------------------------
      // anything else is an error, go out of the function
      // -----------------------------------------------------
      default: 
      {
        sysRc = mqreason ;
        goto  _door; ;
      }
    }
  
    if( memcmp( md.Format, MQFMT_EVENT, sizeof(md.Format) ) != 0 )
    {
      sysRc = MQRC_FORMAT_ERROR ;
      logMQCall( CRI, "MQGET", sysRc ) ;
      goto _door;
    }
  
    // -------------------------------------------------------
    // set PCF pointer to the start of the message
    // set Command pointer after PCF pointer
    // -------------------------------------------------------
    pPCFh   = (PMQCFH) msg ;
    dumpMqStruct( MQFMT_PCF, pPCFh, NULL ) ;
  
    pPCFcmd = (PMQCHAR) (pPCFh+1) ;

    while( pPCFh->ParameterCount-- > 0 )
    {
      pPCFstr = (PMQCFST) pPCFcmd ;
     
      dumpMqStruct( "PCFSTR", pPCFstr, NULL ) ;  // pPCFh->ParameterCount ) ;

      switch( pPCFh->ParameterCount )
      {
        // ---------------------------------------------------
        // queue manager name, ignore it
        // ---------------------------------------------------
        case 4: break ;

        // ---------------------------------------------------
        // current log
        // ---------------------------------------------------
        case 3: memcpy(currLog,pPCFstr->String,pPCFstr->StringLength) ;
                currLog[pPCFstr->StringLength] = '\0' ;
        //      logger( LM_SY_SOME_STR, currLog ) ;
        //      logger( LM_SY_SOME_STR, pPCFstr->String ) ;
        //      logger( LM_SY_SOME_INT, pPCFstr->StringLength ) ;
                break ;

        // ---------------------------------------------------
        // record log
        // ---------------------------------------------------
        case 2: memcpy(recLog,pPCFstr->String,pPCFstr->StringLength) ;
                recLog[pPCFstr->StringLength] = '\0' ;
                break ;

        // ---------------------------------------------------
        // media log
        // ---------------------------------------------------
        case 1: memcpy(mediaLog,pPCFstr->String,pPCFstr->StringLength) ;
                mediaLog[pPCFstr->StringLength] = '\0' ;
                break ;

        // ---------------------------------------------------
        // log path -> will be evaluated by DISPLAY QMSTATUS  
        // ---------------------------------------------------
        #if(0)
        case 0: memcpy(logPath,pPCFstr->String,pPCFstr->StringLength) ;
                logPath[pPCFstr->StringLength] = '\0' ;
                break ;
        #endif

        default: break ;
      }
      pPCFcmd += pPCFstr->StrucLength ;
    }
  }

  _door:

  logFuncExit() ;

  return sysRc ;
}

/******************************************************************************/
/*   O L D E R   T R A N S A C T I O N A L   L O G                            */
/*                                                                            */
/*   finds out which of log1 and log2 is older from transaction point of view */
/*                                                                            */
/*   return code                                                              */
/*               -1 if log2 is older                                          */
/*               +1 if log1 is older                                          */
/*                0 if log1 equals log2                                       */
/*                                                                            */
/******************************************************************************/
int mqOlderLog( const char *log1, const char *log2)
{
  logFuncCall() ;                    

  int cnt1 ;
  int cnt2 ;

  cnt1 = mqLogName2Id( log1 ) ;
  cnt2 = mqLogName2Id( log2 ) ;

  if( cnt1 > cnt2 ) return -1 ;
  if( cnt1 < cnt2 ) return +1 ;

  logFuncExit() ;

  return 0 ;
}

/******************************************************************************/
/*   L O G   N A M E   T O   I D                     */
/******************************************************************************/
int mqLogName2Id( const char* log )
{
  logFuncCall() ;                    

  char buff[16] ;
  int id        ;
  char *p ;

  strcpy( buff, log ); 

  buff[0] = '0' ;

  p  = buff ; 
  while( *++p != '.' ) ; *p='\0' ;
  id = (int) atoi( buff );

  logFuncExit() ;

  return id ;
}

/******************************************************************************/
/*   C L E A N   T R A N S A C T I O N A L   L O G S                          */
/*                                                                            */
/*   attributes:                                                              */
/*      - logPath:   path to original transactional logs                */
/*      - bckPath:   path to backup location. If bckPath == NULL no backup    */
/*                   will be done, files will be just removed            */
/*      - oldestLog: files older then this are not necessary for active work  */
/*                   and will be removed on logPath location.       */
/*                   if oldesLog == NULL no remove will be done, files will   */
/*                   just be copied                             */
/*      - zipBin:    compress program to use i.g. /bin/zip             */
/*                                                                            */
/*   description:                                                             */
/*      1) create a backup directory on bckPath  ,                            */
/*         directory should include time stamp in the name with             */
/*         format YYYY.MM.DD-hh.mm-ss                                    */
/*      2) all transactional logs and the active control file "amqhlctl.lfh"  */
/*         will be copied to the backup location.                             */
/*      3) transactional logs on the backup location will be compressed       */
/*      4) all files older then the oldest log will be removed from the       */
/*         original location                                                  */
/*      5) if backup location is null, no backup will be done, only files     */
/*         older then oldest log will be removed                        */
/*      6) if oldest log is null just copy will be done, files will not be    */
/*         removed                                              */
/*                                                                            */
/******************************************************************************/
int mqHandleLog( const char* logPath   , 
                 const char* bckPath   , 
                 const char* oldestLog ,
                 const char* zipBin    )
{
  logFuncCall() ;

  int sysRc = 0;

  DIR *orgDir;                         // source directory pointer
  struct dirent *orgDirEntry;          // source directory entry
  char orgFile[PATH_MAX]    ;          // source file name
  char cpyFile[PATH_MAX]    ;          //

  char actBckPath[PATH_MAX];
                                                    //
  // -------------------------------------------------------
  // initialize all directories
  // -------------------------------------------------------
  orgDir = opendir(logPath);                         // open source directory 
  if( orgDir == NULL )                               //   for list all files
  {                                                  //
    logger( LSTD_OPEN_DIR_FAILED, logPath );         //
    logger( LSTD_ERRNO_ERR, errno, strerror(errno) );//
    sysRc = errno ;                                  //
    goto _door;                                      //
  }                                                  //
                                                     //
  if( bckPath != NULL )                              //
  {                                                  //
    sprintf( actBckPath,"%s/active", bckPath );
    sysRc = mkdirRecursive( actBckPath, 0775 );         //
    if(sysRc == -1 )                                 // create goal directory 
    {                                                //
      logger( LSTD_MAKE_DIR_FAILED, actBckPath );    //
      logger( LSTD_ERRNO_ERR, errno, strerror(errno) );
      sysRc = errno ;                                //
      goto _door;                                    //
    }                                                //
  }                                                  //
                                                     //
  // -------------------------------------------------------
  // list all files in source directory
  // -------------------------------------------------------
  while( NULL != (orgDirEntry = readdir(orgDir) ) )  // 
  {                                                  //
    if( strcmp(orgDirEntry->d_name,  "." ) == 0 ||   // skip directories
        strcmp(orgDirEntry->d_name,  "..") == 0  )   //
    {                                                //
      continue;                                      //
    }                                                //
                                                     //
    if( mqCheckLogName( orgDirEntry->d_name ) != 0 ) // file does not match 
    {                                                // naming standards for 
      continue;                                      // transactional logs
    }                                                //
                                                     //
    strcpy( orgFile, logPath );                      // set up absolute source 
    strcat( orgFile, "/" );                          //   file name 
    strcat( orgFile, orgDirEntry->d_name );          //
                                                     //
    if( bckPath != NULL )                            //
    {                                                //
      strcpy( cpyFile, actBckPath );                 // set up absolute goal 
      strcat( cpyFile, "/" );                        // set up absolute goal 
      strcat( cpyFile, orgDirEntry->d_name );        //  file name
                                          //
      sysRc = mqCopyLog( orgFile, cpyFile );         // copy file
      if( sysRc != 0 ) goto _door;                   //
                                    //
      sysRc = callZipFile( zipBin, cpyFile );        //
      if( sysRc != 0 ) goto _door;                   //
    }                                                //
                                                     //
    if( oldestLog == NULL )                          //
    {                                                //
      continue;                            //
    }                                                //
                                                     //
    if( mqOlderLog( orgDirEntry->d_name, oldestLog ) > 0 )
    {                                                // log is not needed any more
      logger(LMQM_INACTIVE_LOG,orgDirEntry->d_name); //   remove it
      unlink(orgFile);                               //
      usleep(1000);                                  //
    }                                                //
    else                                             //
    {                                                //
      logger(LMQM_ACTIVE_LOG,orgDirEntry->d_name);   //   keep the log
    }                                                //
  }                                                  //

  closedir( orgDir );

  _door:

  logFuncExit() ;

  return sysRc ;
}

/******************************************************************************/
/*   C H E C K   I F   F I L E   I S   T R A N S A C T I O N A L   L O G      */
/******************************************************************************/
int mqCheckLogName( const char* log)
{
  logFuncCall() ;

  char *p ;
  int  i  ;

  p=(char*)log ;

  if( *p++ != 'S' ) return 1 ;
  for( i=1; i<8 ; i++ ) 
  {
    if( *p < '0' ) return i ;
    if( *p > '9' ) return i ;
    p++ ;
  } 
  if( *p++ != '.' ) return  9 ;
  if( *p++ != 'L' ) return 10 ;
  if( *p++ != 'O' ) return 11 ;
  if( *p++ != 'G' ) return 12 ;

  logFuncExit() ;

  return 0 ;
} 

/******************************************************************************/
/*   M Q   C L O S E   A N D   D I S C O N N E C T                            */
/******************************************************************************/
MQLONG mqCloseDisconnect( MQHCONN  Hcon   ,  // connection handle   
                          PMQHOBJ  Hqueue )  // queue handle   
{
  logFuncCall() ;

  int sysRc ;

  // ---------------------------------------------------
  // close queue
  // ---------------------------------------------------
  sysRc = mqCloseObject( Hcon    ,      // connection handle
                      Hqueue );     // queue handle

  switch( sysRc )
  {
    case MQRC_NONE : break ;
    default        : goto _door ;
  }

  // ---------------------------------------------------
  // disconnect from queue manager
  // ---------------------------------------------------
  sysRc =  mqDisc( &Hcon );  // connection handle            
  switch( sysRc )
  {
    case MQRC_NONE : break ;
    default        : goto _door ;
  }

  _door:

  logFuncExit() ;

  return sysRc ;
}

/******************************************************************************/
/*   R E C O R D   M Q   I M A G E                                            */
/******************************************************************************/
int rcdMqImg( const char* _qmgr, const char* _instPath )
{
  logFuncCall() ;

  char qmgrStr[MQ_Q_MGR_NAME_LENGTH+1] ;

  int pid                 ;  // pid after fork
  int stdErr[2]           ;  // pipe file descriptors std err
  char  c                 ;  // char buffer for pipe
  char pipeBuff[PIPE_BUF] ;  // string buffer for pipe

  int i  ;
  int sysRc = 0;
  
  char rcdmqimgCmd[RCDMQIMG_CMD_LENGTH+1] ;

  // -------------------------------------------------------
  // initialize full path record image command 
  // -------------------------------------------------------
  snprintf( rcdmqimgCmd, RCDMQIMG_CMD_LENGTH, "%s/bin/%s", 
                                               _instPath , 
                                               RCDMQIMG );

  // -------------------------------------------------------
  // fork process for rcdmqimg incl. pipes
  // -------------------------------------------------------
  sysRc = pipe(stdErr) ;
  if( sysRc < 0 )
  {
    logger( LSTD_OPEN_PIPE_FAILED ) ;
    goto _door ;
  }

  pid = (int) fork() ;  
  switch(pid)
  {
    // -----------------------------------------------------
    // fork failed: 
    //    do not quit, trigger has to be reactivated
    // -----------------------------------------------------
    case -1: 
    {
      logger( LSTD_FORK_FAILED ) ;
      sysRc = -1 ;
      goto _door ;
    }

    // -----------------------------------------------------
    // child:
    //    map stderr to pipe
    //    close read end of the pipe, 
    //    exec rcdmqimg
    // -----------------------------------------------------
    case  0: 
    {
      logger(LSTD_FORK_CHILD  )   ; 
      // ---------------------------------------------------
      // handle file descriptors
      // ---------------------------------------------------
      dup2( stdErr[1], STDERR_FILENO ) ;      
      close( stdErr[0]    ) ;     
      close( stdErr[1]    ) ;      

      // ---------------------------------------------------
      // setup queue manager name
      // ---------------------------------------------------
      memcpy( qmgrStr, _qmgr, MQ_Q_MGR_NAME_LENGTH ) ;  
      for(i=0;i<MQ_Q_MGR_NAME_LENGTH;i++) 
      {
        if( qmgrStr[i] == ' ' )
        {
          qmgrStr[i] = '\0' ;
          break   ;
        }
      }

      // ---------------------------------------------------
      // exec
      // ---------------------------------------------------
      execl( rcdmqimgCmd, RCDMQIMG, "-m", qmgrStr ,
                                    "-t", "all"   , 
                                    "*" , NULL   );

      // -------------------------------------------------
      // there is no chance to get that far 
      // -------------------------------------------------
      logger(  LSTD_GEN_CRI, "rcdmqimg" ) ;
 
      exit(0) ;                      
    }

    // -----------------------------------------------------
    // parent:
    //    close write end of the pipe
    //    read from the pipe and write to log
    // -----------------------------------------------------
    default: 
    {
      logger( LSTD_FORK_PARENT  );
      // ---------------------------------------------------
      // setup queue manager name
      // ---------------------------------------------------
      dup2( stdErr[0], STDIN_FILENO ) ;             
      close( stdErr[0]   ) ;             
      close( stdErr[1]   ) ;             

      // ---------------------------------------------------
      // redirect output from child (RCDMQIMG) to log file
      // ---------------------------------------------------
      logger( LSYS_MULTILINE_START, "RCDMQIMG" ) ;          
                              
      i=0  ;                           
      while( 1 )                          // read for ever
      {                                   // 
        sysRc=read( STDIN_FILENO, &c, 1 );// read from the pipe
        if( (int)sysRc == 0) break ;      // stop if EOF (will work only 
        pipeBuff[i] = c ;                 //       on nonblocked device)
        if( c == '\n' )                   // if eol
        {                                 //
          pipeBuff[i] = '\0' ;            // replace '\n' by '\0' 
          i=0;                            // start new line
          logger( LSYS_MULTILINE_ADD, pipeBuff ); 
          continue ;                      // write full line to log
        }                                 //
        i++ ;                             //
      }                                   //        
      if( c != '\n' )                     // if last line is not ended by '\n'
      {                                   // 
        pipeBuff[i] = '\0' ;              // write it to log file
        logger( LSYS_MULTILINE_ADD, pipeBuff ); 
      }
      logger( LSYS_MULTILINE_END, "RCDMQIMG" ) ;          

      // ---------------------------------------------------
      // allow child to free (disable zombi)
      // ---------------------------------------------------
      waitpid(pid,&sysRc,WNOHANG) ;
      sysRc >>=   8 ;
      sysRc  &= 127 ;
      if( sysRc == 0 )
      {
        logger(  LSTD_CHILD_ENDED_OK, "rcdmqimg" );
      }
      else
      {
        logger(  LSTD_CHILD_ENDED_ERR, "rcdmqimg", sysRc );
	goto _door;
      }
      break ;
    }
  }

  _door:
  return sysRc ;
}

#if(0)
/******************************************************************************/
/*   B A C K U P   T R A N S A C T I O N A L   L O G S                     */
/******************************************************************************/
int mqBackupLog( const char* logPath,   // original log path
                 const char* bckPath,   // backup log path
                 const char* oldLog )   // oldest log name
{
  logFuncCall() ;

  char origFile[255] ;
  char copyFile[255] ;

  int oldLogId = mqLogName2Id( oldLog  );
  int curLogId = mqLogName2Id( currLog );
  int logId    ;

  time_t     curTime ;
  struct tm *localTime ;
  char       timeStr[32] ;

  int rc ;

  // -------------------------------------------------------
  // get time for mkdir & mkdir
  // -------------------------------------------------------
  curTime = time( NULL ) ;
  localTime = localtime( &curTime ) ;
  strftime( timeStr, 32, "%Y%m%d_%H%M%S", localTime );

  sprintf( copyFile, "/%s/%s",bckPath,timeStr ) ;
  rc = mkdir(copyFile, S_IRUSR|S_IWUSR|S_IXUSR |
                       S_IRGRP|S_IWGRP|S_IXGRP );
  if( rc < 0 )
  {
    logger( LSTD_MAKE_DIR_FAILED , copyFile ) ;
    return 1 ;
  }

  // -------------------------------------------------------
  // copy all relevant files
  // -------------------------------------------------------
  for( logId=oldLogId; logId<curLogId; logId++ )
  {
    sprintf( origFile, "/%s/S%07d.LOG",logPath,logId ) ;
    sprintf( copyFile, "/%s/%s/S%07d.LOG",bckPath,timeStr,logId ) ;

    logger( LSTD_FILE_COPIED, origFile, copyFile) ;

    rc = mqCopyLog( origFile, copyFile );
    if( rc != 0 ) return rc ;
  }
  return 0 ;
}
#endif

/******************************************************************************/
/*   C O P Y   T R A N S A C T I O N A L   L O G                              */
/******************************************************************************/
int mqCopyLog( const char* orgFile, const char* cpyFile )
{
  logFuncCall() ;

  int sysRc = 0 ;
  
  char copyBuff[COPY_BUFF_LNG] ;

  int orgFd ;
  int cpyFd ;

  int eof = 0;

  int rc ;

  // -------------------------------------------------------
  // open original file for reading
  // -------------------------------------------------------
  if( (orgFd = open( orgFile, O_RDONLY) ) == -1 ) 
  {
    logger( LSTD_OPEN_FILE_FAILED, orgFile ) ;
    logger( LSTD_ERRNO_ERR, errno, strerror(errno));
    sysRc = errno ;
    goto _door;
  }

  // -------------------------------------------------------
  // open backup file for writing
  // -------------------------------------------------------
  if( (cpyFd = open( cpyFile, O_WRONLY|O_CREAT, 
                              S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP )) == -1 )
  {
    logger(  LSTD_OPEN_FILE_FAILED, cpyFile ) ;
    logger( LSTD_ERRNO_ERR, errno, strerror(errno));
    sysRc = errno;
    goto _door;
  }

  // -------------------------------------------------------
  // copy file in 4k blocks, since page size is 4k
  // -------------------------------------------------------
  eof = 0 ;
  while( !eof )
  {
    rc=read(  orgFd, copyBuff, COPY_BUFF_LNG ) ;
    switch( rc )
    {
      case COPY_BUFF_LNG: write( cpyFd, copyBuff, COPY_BUFF_LNG );
                          break ;

      case 0            : close(orgFd) ;
                          close(cpyFd) ;
                          sysRc = 0 ;
                          eof = 1;

      default           : logger( LSTD_ERR_READING_FILE, orgFile);
                          write( cpyFd, copyBuff, rc );
                          close(orgFd) ;
                          close(cpyFd) ;
                          sysRc = 1;
                          goto _door;
    } 
  }

  _door:

  logFuncExit() ;

  return sysRc ;
}

/******************************************************************************/
/*   C A L L   Z I P   F I L E                                                */
/******************************************************************************/
int callZipFile( const char* zipBin, const char* file )
{
  logFuncCall() ;

  int sysRc = 0;

  char *argv[2] ;
        argv[0] = (char*) file;
	argv[1] = NULL;

  startChild( zipBin, NULL, NULL, argv );
   

//_door:

  logFuncExit() ;

  return sysRc ;
}

/******************************************************************************/
/*   G E T   Q U E U E   M A N A G E R   O B J E C T   S T A T U S            */
/******************************************************************************/
MQLONG getQmgrStatus( MQHCONN Hconn, tQmgrObjStatus* pQmgrObjStatus )
{
  logFuncCall() ;

  #if MQ_INSTALLATION_PATH_LENGTH > MQ_LOG_PATH_LENGTH 
    #define ITEM_LENGTH MQ_INSTALLATION_PATH_LENGTH
  #else
    #define ITEM_LENGTH MQ_LOG_PATH_LENGTH
  #endif

  MQLONG mqrc = MQRC_NONE ;  
  MQLONG compCode ;

  MQHBAG cmdBag  = MQHB_UNUSABLE_HBAG;
  MQHBAG respBag = MQHB_UNUSABLE_HBAG;
  MQHBAG attrBag ;

  MQLONG parentItemCount ;
  MQLONG parentItemType  ;
  MQLONG parentSelector  ;

  MQLONG childItemCount ;
  MQLONG childItemType  ;
  MQLONG childSelector  ;

  MQINT32 selInt32Val ;
  MQCHAR  selStrVal[ITEM_LENGTH];

  MQLONG  selStrLng ;
  MQLONG  ccsid ;

  char  sBuffer[ITEM_LENGTH+1];

  int i;
  int j;

  // -------------------------------------------------------
  // initialize 
  // -------------------------------------------------------
  pQmgrObjStatus->compCode = MQCC_UNKNOWN;
  pQmgrObjStatus->reason   = MQRC_NONE   ;
  memset( pQmgrObjStatus->instPath, 0, MQ_INSTALLATION_PATH_LENGTH+1);
  memset( pQmgrObjStatus->logPath,  0, MQ_LOG_PATH_LENGTH+1         );

  // -------------------------------------------------------
  // open bags
  // -------------------------------------------------------
  mqrc = mqOpenAdminBag( &cmdBag );
  switch( mqrc )
  {
    case MQRC_NONE : break;
    default: goto _door;
  }

  mqrc = mqOpenAdminBag( &respBag );
  switch( mqrc )
  {
    case MQRC_NONE : break;
    default: goto _door;
  }

  // -------------------------------------------------------
  // DISPLAY QMSTATUS ALL 
  //   process command in two steps
  //   1. setup the list of arguments MQIACF_ALL = ALL
  //   2. send a command MQCMD_INQUIRE_Q_MGR_STATUS = DISPLAY QMSTATUS
  // -------------------------------------------------------
  mqrc = mqSetInqAttr( cmdBag, MQIACF_ALL );      // set attribute 
                                                  // to PCF command
  switch( mqrc )                                  // DISPLAY QMSTATUS ALL
  {                                               //
    case MQRC_NONE : break;                       //
    default: goto _door;                          //
  }                                               //
                                                  //
  mqrc = mqExecPcf( Hconn                     ,   // send a command to 
                    MQCMD_INQUIRE_Q_MGR_STATUS,   //  the command queue
                    cmdBag                    ,   //
                    respBag                  );   //
                                                  //
  switch( mqrc )                                  //
  {                                               //
    case MQRC_NONE : break;                       //
    default: goto _door;                          //
  }                                               //
                                                  //
  // ---------------------------------------------------------
  // count the items in response bag
  // -------------------------------------------------------
  mqrc = mqBagCountItem( respBag              ,    // get the amount of items
                         MQSEL_ALL_SELECTORS );    //  for all selectors
                                                   //
  if( mqrc > 0 )                                   // 
  {                                                //
    goto _door;                                    //
  }                                                //
  else                                             // if reason code is less 
  {                                                //  then 0 then it is not 
    parentItemCount = -mqrc;                       //  a real reason code it's  
    mqrc = MQRC_NONE ;                             //  the an item counter
  }                                                //
                                                   //
  // ---------------------------------------------------------
  // go through all items
  // ---------------------------------------------------------
  for( i=0; i<parentItemCount; i++ )               // analyze all items
  {                                                //
    mqInquireItemInfo( respBag           ,         // find out the item type
                       MQSEL_ANY_SELECTOR,         //
                       i                 ,         //
                       &parentSelector   ,         //
                       &parentItemType   ,         //
                       &compCode         ,         //
                       &mqrc            );         //
                                                   //
    switch( mqrc )                                 //
    {                                              //
      case MQRC_NONE : break;                      //
      default:                                     //
      {                                            //
        logMQCall( ERR, "mqInquireItemInfo", mqrc );
        goto _door;                                //
      }                                            //
    }                                              //
    logMQCall( DBG, "mqInquireItemInfo", mqrc );   //
                                                   //
#define _LOGTERM_                                  //
#undef  _LOGTERM_                                //
#ifdef  _LOGTERM_                                  //
    char* pBuffer;
    printf( "%2d selector: %04d %-30.30s type %10.10s",
               i                               ,   //
               parentSelector                  ,   //
               mqSelector2str(parentSelector)  ,   //
               mqItemType2str(parentItemType) );   //
#endif                                             //

    // -------------------------------------------------------
    // for each item:
    //    - get the item type
    //    - analyze selector depending on the type
    // -------------------------------------------------------
    switch( parentItemType )                       //
    {                                              //
      // -----------------------------------------------------
      // TYPE: 32 bit integer -> in this program only system 
      // -----------------------------------------------------
      case MQITEM_INTEGER :                        // in this program only 
      {                                            //  system selectors can
        mqInquireInteger( respBag           ,      //  be expected
                          MQSEL_ANY_SELECTOR,      // 
                          i                 ,      // out of system selectors
                          &selInt32Val      ,      //  only Comp Code and 
                          &compCode         ,      //  reason code are 
                          &mqrc            );      //  important, all other 
        switch( mqrc )                             //  will be ignored
        {                                          //
          case MQRC_NONE :                         // analyze InquireInteger
          {                                        //  reason code
            break;                                 //
          }                                        //
          default :                                //
          {                                        //
            logMQCall( ERR, "mqInquireInteger", mqrc ); 
            goto _door;                            //
          }                                        //
        }                                          //
        logMQCall(DBG,"mqInquireInteger",mqrc);    //
                                                   //
#ifdef _LOGTERM_                                   //
        pBuffer=(char*)itemValue2str( parentSelector,
                                    (MQLONG)selInt32Val) ;
        if( pBuffer )                              //
        {                                          //
          printf(" value %s\n", pBuffer );           //
        }                                          //
        else                                       //
        {                                          //
          printf(" value %d\n", (int)selInt32Val);   //
        }                                          //
#endif                                             //
        // ---------------------------------------------------
        // TYPE: 32 bit integer; analyze selector
        // ---------------------------------------------------
        switch( parentSelector )                   // all 32 bit integer 
        {                                          //  selectors are system
          case MQIASY_COMP_CODE:                   //  selectors
          {                                        //
            pQmgrObjStatus->compCode = (MQLONG)selInt32Val;
            break;                                 // only mqExec completion 
          }                                        //  code and reason code are 
          case MQIASY_REASON:                      //  interesting for later use
          {                                        // 
            pQmgrObjStatus->reason = (MQLONG)selInt32Val;
            break;                                 //
          }                                        //
          default :                                //
          {                                        // all other selectors can
            break;                                 //  be ignored
          }                                        //
        }                                          //
        break;                                     //
      }                                            //
                                                   //
      // -----------------------------------------------------
      // TYPE: Bag -> Bag in Bag -> cascaded bag contains real data 
      // -----------------------------------------------------
      case MQITEM_BAG :                            //
      {                                            //
#ifdef  _LOGTERM_                                  //
        printf("\n===========================");   //
        printf("===========================\n");   //
#endif                                             //
        mqInquireBag( respBag       ,              // usable data are located
                      parentSelector,              //  in cascaded bag
                      0             ,              //
                      &attrBag      ,              // out of this data only:
                      &compCode     ,              //  - installation path
                      &mqrc        );              //  - log path
        switch( mqrc )                             //
        {                                          //
          case MQRC_NONE :                         //
          {                                        //
            break;                                 //
          }                                        //
          default :                                //
          {                                        //
            logMQCall( ERR, "mqInquireItemBag", mqrc ); 
            goto _door;                            //
          }                                        //
        }                                          //
        logMQCall(DBG,"mqInquireItemBag",mqrc);    //
                                                   //
        // ---------------------------------------------------
        // count the items in the sub bag
        // ---------------------------------------------------
        mqrc=mqBagCountItem(attrBag             ,  // get the amount of items
                            MQSEL_ALL_SELECTORS);  //  for all selectors in 
        if( mqrc > 0 )                             //  child bag
        {                                          //
          goto _door;                              //
        }                                          //
        else                                       //
        {                                          //
          childItemCount = -mqrc;                  //
          mqrc = MQRC_NONE;                        //
        }                                          //
                                                   //
        // ---------------------------------------------------
        // go through all parent items
        // ---------------------------------------------------
        for( j=0; j<childItemCount; j++ )          //
        {                                          //
          mqInquireItemInfo( attrBag           ,   // find out the item type
                             MQSEL_ANY_SELECTOR,   //
                             j                 ,   //
                             &childSelector    ,   //
                             &childItemType    ,   //
                             &compCode         ,   //
                             &mqrc            );   //
          switch( mqrc )                           //
          {                                        //
            case MQRC_NONE: break;                 //
            default :                              //
            {                                      //
              logMQCall( ERR, "mqInquireItemInfo", mqrc ); 
              goto _door;                          //
            }                                      //
          }                                        //
          logMQCall( DBG, "mqInquireItemInfo", mqrc );
                                                   //
#ifdef    _LOGTERM_                                //
          printf("   %2d selector: %04d %-30.30s type %10.10s",
                 j                             ,   //
                 childSelector                 ,   //
                 mqSelector2str(childSelector) ,   //
                 mqItemType2str(childItemType));   //
#endif                                             //
                                                   //
          // -------------------------------------------------
          // CHILD ITEM
          //   analyze each child item / selector
          // -------------------------------------------------
          switch( childItemType )                  //
          {                                        //
            // -----------------------------------------------
            // CHILD ITEM
            // TYPE: 32 bit integer
            // -----------------------------------------------
            case MQITEM_INTEGER :                  // not a single integer 
            {                                      //  can be used in this 
              mqInquireInteger(attrBag           , //  program.
                               MQSEL_ANY_SELECTOR, // so the the selInt32Val
                               j                 , //  does not have to be 
                               &selInt32Val      , //  evaluated
                               &compCode         , //
                               &mqrc            ); //
              switch( mqrc )                       //
              {                                    //
                case MQRC_NONE : break;            //
                default :                          //
                {                                  //
                  logMQCall( ERR, "mqInquireInteger", mqrc ); 
                  goto _door;                      //
                }                                  //
              }                                    //
              logMQCall( DBG, "mqInquireInteger", mqrc ); 
                                                   //
#ifdef        _LOGTERM_                            //
              pBuffer=(char*)itemValue2str(childSelector,
                                           (MQLONG)selInt32Val) ;
              if( pBuffer )                        //
              {                                    //
                printf(" value %s\n", pBuffer );     //
              }                                    //
              else                                 //
              {                                    //
                printf(" value %d\n", (int)selInt32Val);
              }                                    //
#endif                                             //
              break;                               //
            }                                      //
                                                   //
            // -----------------------------------------------
            // CHILD ITEM
            // TYPE: string
            // -----------------------------------------------
            case MQITEM_STRING :                   // installation path
            {                                      //  and log path have
              mqInquireString(attrBag           ,  //  type STRING
                              MQSEL_ANY_SELECTOR,  //
                              j                 ,  //  
                              ITEM_LENGTH       ,  //
                              selStrVal         ,  //
                              &selStrLng        ,  //
                              &ccsid            ,  //
                              &compCode         ,  //
                              &mqrc            );  //
              switch( mqrc )                       //
              {                                    //
                case MQRC_NONE: break ;            //
                default:                           //
                {                                  //
                  logMQCall( ERR, "mqInquireString", mqrc ); 
                  goto _door;                      //
                }                                  //
              }                                    //
              logMQCall( DBG, "mqInquireString", mqrc ); 
                                                   //
              mqTrim(ITEM_LENGTH,                  // trim string 
                     selStrVal,                    //
                     sBuffer  ,                    //
                     &compCode,                    //
                     &mqrc   );                    //
              switch( mqrc )                       //
              {                                    //
                case MQRC_NONE : break;            //
                default :                          //
                {                                  //
                  logMQCall( ERR, "mqTrim", mqrc );//
                  goto _door ;                     //
                }                                  //
              }                                    //
              logMQCall( ERR, "mqTrim", mqrc );    //
                                                   //
              // ---------------------------------------------
              // CHILD ITEM
              // TYPE: string
              // analyze selector
              // ---------------------------------------------
              switch( childSelector )              // 
              {                                    //
                case MQCA_INSTALLATION_PATH :      //
                {                                  //
                  strcpy( pQmgrObjStatus->instPath, sBuffer ); 
                  break;                           //
                }                                  //
                case MQCACF_LOG_PATH :             //
                {                                  //
                  strcpy( pQmgrObjStatus->logPath, sBuffer ); 
                  break;                           //
                }                                  //
                default: break ;                   //
              }                                    //
#ifdef        _LOGTERM_                            //
              printf(" value %s\n", sBuffer );       //
#endif                                             //
              break;                               //
            }                                      //
                                                   //
            // -----------------------------------------------
            // any other item type for child bag is an error
            // -----------------------------------------------
            default :                              //
            {                                      //
              logMQCall( ERR, "mqInquireItemInfo unexpected Type", mqrc );
              goto _door;                          //
            }                                      //
          }                                        //
        }                                          //
        break ;                                    //
      }                                            //
                                                   //
      // -----------------------------------------------------
      // all other parent item types are not expected
      // -----------------------------------------------------
      default :                                    //
      {                                            //
        logMQCall( ERR, "mqInquireItemInfo unexpected Type", mqrc );
	goto _door;                                //
      }                                            //
    }                                              //
  }                                                //
                                                   //

  _door:

  mqCloseBag( &cmdBag );
  mqCloseBag( &respBag );

  if( pQmgrObjStatus->instPath[0] == '\0' )
  {
    strcpy( pQmgrObjStatus->instPath, MQ_DEFAULT_INSTALLATION_PATH );
  }

#ifdef _LOGTERM_
  printf("\n");
#endif 

  logFuncExit() ;
  return mqrc ;
}
