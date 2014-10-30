/******************************************************************************/
/*                                                                            */
/*                       M Q   L O G G E R   E V E N T                        */
/*                                                                            */
/*   functions:                                                               */
/*     - cleanupLog                                                           */
/*     - pcfReadQueue                                                    */
/*     - mqOlderLog                                                  */
/*     - mqLogName2Id                                              */
/*     - mqCleanLog                                                */
/*     - mqCheckLogName                                          */
/*     - mqCloseDisconnect                                      */
/*     - rcdMqImg                                */
/*     - mqBackupLog                            */
/*     - mqCopyLog                              */
/*     - getMqInstPath                              */
/*                          */
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

// ---------------------------------------------------------
// own
// ---------------------------------------------------------
#include <ctl.h>
#include <msgcat/lgstd.h>
#include <msgcat/lgmqm.h>

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

#define LOGGER_QUEUE "SYSTEM.ADMIN.LOGGER.EVENT"

/******************************************************************************/
/*                               S T R U C T S                                */
/******************************************************************************/

/******************************************************************************/
/*   P R O T O T Y P E S                                                      */
/******************************************************************************/
void usage() ;
MQLONG pcfReadQueue( MQHCONN  Hcon     ,  // connection handle
                     MQHOBJ   Hqueue   ,  // queue handle
                     char*    logPath  ,  // logpath, max of 82+1 incl. log file 
                     char*    currLog  ,  // current log name, max of 12+1 
                     char*    recLog   ,  // record  log name, max of 12+1
                     char*    mediaLog);  // media   log name, max of 12+1

MQLONG mqCloseDisconnect( MQHCONN  Hcon    ,  // connection handle   
                         PMQHOBJ  Hqueue );  // queue handle   

int mqOlderLog( const char *log1, const char *log2) ;
int mqLogName2Id( const char* log );
int mqCleanLog( const char* logPath, const char* oldestLog );
int mqCheckLogName( const char* log) ;
int rcdMqImg( const char* _qmgr, const char* _instPath );

int mqBackupLog( const char* logPath ,   // original log path
                 const char* bckPath ,   // backup log path
                 const char* oldLog  ,   // oldest log name
                 const char* currLog);   // current log

int mqCopyLog( const char* orgFile, const char* cpyFile );

MQLONG getMqInstPath( MQHCONN Hconn, char* instPath );

/******************************************************************************/
/*                                                                            */
/*                                   M A I N                                  */
/*                                                                            */
/******************************************************************************/
int cleanupLog( const char* qmgrName,  
                const char* qName  )
{
  MQTMC2 trigData ;

  MQHCONN  Hcon   ;                 // connection handle   
  MQOD     qDscr  = {MQOD_DEFAULT}; // queue descriptor
  MQHOBJ   Hqueue ;                 // queue handle   

  char instPath [MQ_INSTALLATION_PATH_LENGTH];
//char *instPath ;
  char logPath[124] ; // logpath, theoratical max of 82+1 (incl. log file name)
//char bckPath[124] ; // backup path, path were logs are to be backuped
  char currLog[16]  ; // current log name, max of 12+1 
  char recLog[16]   ; // record  log name, max of 12+1
  char mediaLog[16] ; // media   log name, max of 12+1
  char oldLog[16]   ; // oldest  log name, max of 12+1 equals media or record

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
    sysRc = getMqInstPath( Hcon, instPath );

  switch( sysRc )
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
  // fork process for RCDMQIMG, log RCDMQIMG output to log
  // -------------------------------------------------------
  rcdMqImg( qmgrName, instPath ); // exec RCDMQIMG

  // -------------------------------------------------------
  // read all messages from the queue, 
  // get back only the last one for analyzing
  // -------------------------------------------------------
  logPath[0]  = '\0' ;
  currLog[0]  = '\0' ;
  recLog[0]   = '\0' ;
  mediaLog[0] = '\0' ;

  sysRc = pcfReadQueue( Hcon      , 
                        Hqueue    ,
                        logPath   , 
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
      // ---------------------------------------------------
      // close queue
      // ---------------------------------------------------
      sysRc = mqCloseObject( Hcon    ,      // connection handle
                            &Hqueue );     // queue handle
  
      switch( sysRc )
      {
        case MQRC_NONE : break ;
        default        : logger( LSTD_GEN_SYS, progname );
                         goto _door ; 
      }
  
      // ---------------------------------------------------
      // disconnect from queue manager
      // ---------------------------------------------------
      sysRc =  mqDisc( &Hcon );        // connection handle            
      switch( sysRc )
      {
        case MQRC_NONE : break ;
        default        :  logger( LSTD_GEN_SYS, progname );
                           goto _door ; 
      }
    
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
  logger( LMQM_LOG_NAME, "PATH"   , logPath  );

  if( logPath[0]  != '/' ||
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
  mqCleanLog( logPath, oldLog ) ;

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

  return sysRc ;
}

/******************************************************************************/
/*   R E A D   A   P C F   M E S S A G E   F R O M   A   Q U E U E            */
/******************************************************************************/
MQLONG pcfReadQueue( MQHCONN  Hcon    , // connection handle   
                     MQHOBJ   Hqueue  , // queue handle   
                     char*    logPath , // logpath, max of 82+1 incl. log file 
                     char*    currLog , // current log name, max of 12+1 
                     char*    recLog  , // record  log name, max of 12+1
                     char*    mediaLog) // media   log name, max of 12+1
{                                       // 
  PMQVOID msg     ;                     // message buffer 
  MQMD    md      = {MQMD_DEFAULT} ;    // message descriptor
  MQGMO   gmo     = {MQGMO_DEFAULT};    // get message options
  MQLONG  msgSize = 512 ;               //
                                        //
  PMQCFH  pPCFh   ;                     // PCF (header) pointer
  PMQCHAR pPCFcmd ;                     // PCF command pointer
  PMQCFST pPCFstr ;                     // PCF string pointer
                                        //
  int      sysRc      = MQRC_NONE;      // general return code
  MQLONG   mqreason   ;                 // MQ return code
  int      cnt        ;                 // message counter
                                        //
  logFuncCall() ;                       //
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
  while(1)
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
        // no message at all was found
        // ---------------------------------------------------
        if( cnt == 1 ) 
	{
          goto _door;
	}
	sysRc = MQRC_NONE ;

        // ---------------------------------------------------
        // at least one message was found
        // ---------------------------------------------------
        sysRc = MQRC_NONE ;
	break;
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
        // log path
        // ---------------------------------------------------
        case 0: memcpy(logPath,pPCFstr->String,pPCFstr->StringLength) ;
                logPath[pPCFstr->StringLength] = '\0' ;
                break ;

        default: break ;
      }
      pPCFcmd += pPCFstr->StrucLength ;
    }
  }

  _door:
  return sysRc ;
}

/******************************************************************************/
/*   U S A G E                                         */ 
/******************************************************************************/

// usage()
// usage function moved to cmdln.c

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
#if(0)
  char buf1[16] ;
  char buf2[16] ;
#endif

  int cnt1 ;
  int cnt2 ;

#if(0)
  char *p;

  strcpy( buf1, log1 );
  strcpy( buf2, log2 );

  buf1[0] = '0' ;
  buf2[0] = '0' ;

  p= buf1 ; while( *++p != '.' ) ; *p='\0' ;
  p= buf2 ; while( *++p != '.' ) ; *p='\0' ;

  cnt1 = (int) atoi(buf1);   
  cnt2 = (int) atoi(buf2);   
#else
  cnt1 = mqLogName2Id( log1 ) ;
  cnt2 = mqLogName2Id( log2 ) ;
#endif

  if( cnt1 > cnt2 ) return -1 ;
  if( cnt1 < cnt2 ) return +1 ;
  return 0 ;
}

/******************************************************************************/
/*   L O G   N A M E   T O   I D                     */
/******************************************************************************/
int mqLogName2Id( const char* log )
{
  char buff[16] ;
  int id        ;
  char *p ;

  strcpy( buff, log ); 

  buff[0] = '0' ;

  p  = buff ; 
  while( *++p != '.' ) ; *p='\0' ;
  id = (int) atoi( buff );

  return id ;
}

/******************************************************************************/
/*   C L E A N   T R A N S A C T I O N A L   L O G S                          */
/******************************************************************************/
int mqCleanLog( const char* logPath, const char* oldestLog )
{
  logFuncCall() ;

  DIR *dir ;
  struct dirent *dirEntry ;
  char fileName[256] ;
  struct stat fMode ;

  dir = opendir(logPath) ;

  while( NULL != (dirEntry = readdir(dir) ) )
  {
    if( strcmp(dirEntry->d_name,  "." ) == 0 ||    // skip directories
        strcmp(dirEntry->d_name,  "..") == 0  )    //
    {                                              //
      continue;                                    //
    }                                              //
    // if( S_ISDIR(fMode.st_mode) ) logger(LM_SY_DBG_MARKER) ;
                                                   //
    if( mqCheckLogName( dirEntry->d_name ) != 0 )  // file does not match 
    {                                              // naming standards for 
      continue;                                    // transactional logs
    }                                              //
                                                   //
    if( mqOlderLog( dirEntry->d_name, oldestLog ) > 0 )
    {                                              // log is not needed any more
      logger(LMQM_INACTIVE_LOG,dirEntry->d_name);  //   remove it
      strcpy(fileName,logPath);                    //
      strcat(fileName,"/");                        //
      strcat(fileName,dirEntry->d_name);           // functionality for moving
      unlink(fileName);                            //  instead of removing 
      usleep(1000);                                //  transactional logs
    }                                              //  has to be applied
    else                                           //
    {                                              //
      logger(LMQM_ACTIVE_LOG,dirEntry->d_name);    //   keep the log
    }                                              //
  }                                                //
  return 0 ;
}

/******************************************************************************/
/*   C H E C K   I F   F I L E   I S   T R A N S A C T I O N A L   L O G      */
/******************************************************************************/
int mqCheckLogName( const char* log)
{
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

  return 0 ;
} 

/******************************************************************************/
/*   M Q   C L O S E   A N D   D I S C O N N E C T                            */
/******************************************************************************/
MQLONG mqCloseDisconnect( MQHCONN  Hcon   ,  // connection handle   
                          PMQHOBJ  Hqueue )  // queue handle   
{
  int sysRc ;

  logFuncCall() ;

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
      execl( "/opt/mqm/bin/rcdmqimg", "rcdmqimg", "-m", qmgrStr ,
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

/******************************************************************************/
/*   B A C K U P   T R A N S A C T I O N A L   L O G S                     */
/******************************************************************************/
int mqBackupLog( const char* logPath,   // original log path
                 const char* bckPath,   // backup log path
                 const char* oldLog ,   // oldest log name
                 const char* currLog)   // current log
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

/******************************************************************************/
/*   C O P Y   T R A N S A C T I O N A L   L O G                              */
/******************************************************************************/
int mqCopyLog( const char* orgFile, const char* cpyFile )
{
  logFuncCall() ;
  
  char copyBuff[COPY_BUFF_LNG] ;

  int orgFd ;
  int cpyFd ;

  int rc ;

  // -------------------------------------------------------
  // open original file for reading
  // -------------------------------------------------------
  if( (orgFd = open( orgFile, O_RDONLY) ) == -1 ) 
  {
    logger( LSTD_OPEN_FILE_FAILED, orgFile ) ;
    return 1 ;
  }

  // -------------------------------------------------------
  // open backup file for writing
  // -------------------------------------------------------
  if( (cpyFd = open( cpyFile, O_WRONLY|O_CREAT, 
                              S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP )) == -1 )
  {
    logger(  LSTD_OPEN_FILE_FAILED, cpyFile ) ;
    return 1 ;
  }

  // -------------------------------------------------------
  // copy file in 4k blocks, since page size is 4k
  // -------------------------------------------------------
  while( 1 )
  {
    rc=read(  orgFd, copyBuff, COPY_BUFF_LNG ) ;
    switch( rc )
    {
      case COPY_BUFF_LNG: write( cpyFd, copyBuff, COPY_BUFF_LNG );
                          break ;

      case 0            : close(orgFd) ;
                          close(cpyFd) ;
                          return 0 ;

      default           : logger( LSTD_ERR_READING_FILE, orgFile);
                          write( cpyFd, copyBuff, rc );
                          close(orgFd) ;
                          close(cpyFd) ;
                          return 1 ;
    } 
  }
}

/******************************************************************************/
/*   G E T   M Q   I N S T A L L A T I O N   P A T H                          */
/******************************************************************************/
MQLONG getMqInstPath( MQHCONN Hconn, char* instPath )
{
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
  MQCHAR  selStrVal[MQ_INSTALLATION_PATH_LENGTH];

  MQLONG  selStrLng ;
  MQLONG  ccsid ;

  char* pBuffer ;
  char  sBuffer[MQ_INSTALLATION_PATH_LENGTH+1];

  int i;
  int j;

  // -------------------------------------------------------
  // initialize 
  // -------------------------------------------------------
  memset( instPath, ' ', MQ_INSTALLATION_PATH_LENGTH);

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
  mqrc = mqSetInqAttr( cmdBag, MQIACF_ALL );    //  set attribute to PCF command
                                                // display qmstatus ALL
  switch( mqrc )                                //
  {                                             //
    case MQRC_NONE : break;                     //
    default: goto _door;                        //
  }                                             //
                                                //
  mqrc = mqExecPcf( Hconn                     , // send a command to the command queue
                    MQCMD_INQUIRE_Q_MGR_STATUS, //
                    cmdBag                    , //
                    respBag                  ); //
                                                //
  switch( mqrc )                                //
  {                                             //
    case MQRC_NONE : break;                     //
    default: goto _door;                        //
  }                                             //
                                                //
  // ---------------------------------------------------------
  // count the items in response bag
  // -------------------------------------------------------
  mqrc = mqBagCountItem( respBag              , // get the amount of items
                         MQSEL_ALL_SELECTORS ); //  for all selectors
                                                //
  if( mqrc > 0 )                                // 
  {                                             //
    goto _door;                                 //
  }                                             //
  else                                          // reason code less then 0 is
  {                                             //  not a real reason code it's 
    parentItemCount = -mqrc;                    //  the an item counter
    mqrc = MQRC_NONE ;                          //
  }                                             //
                                                //
  for( i=0; i<parentItemCount; i++ )            // analyze all items
  {                                             //
    mqInquireItemInfo( respBag           ,      // find out the item type
                       MQSEL_ANY_SELECTOR,      //
                       i                 ,      //
                       &parentSelector   ,      //
                       &parentItemType   ,      //
                       &compCode         ,      //
                       &mqrc            );      //
                                                //
    switch( mqrc )                              //
    {                                           //
      case MQRC_NONE : break;                   //
      default:                                  //
      {                                         //
        logMQCall( ERR, "mqInquireItemInfo", mqrc );
        goto _door;                             //
      }                                         //
    }                                           //
    logMQCall( DBG, "mqInquireItemInfo", mqrc );//
                                                //
#define _LOGTERM_                               //
#ifdef  _LOGTERM_                               //
    printf( "\n%2d selector: %04d %-20.20s type %10.10s",
               i                               ,//
               parentSelector                  ,//
               mqSelector2str(parentSelector)  ,//
               mqItemType2str(parentItemType) );//
#endif                                          //
    switch( parentItemType )                    //
    {                                           //
      // -----------------------------------------------------
      // 32 bit integer -> in this case for system selectors only
      // -----------------------------------------------------
      case MQITEM_INTEGER :                     //
      {                                         //
        mqInquireInteger( respBag           ,   // 
                          MQSEL_ANY_SELECTOR,   // only system selectors can
                          i                 ,   //  be expected
                          &selInt32Val      ,   //
                          &compCode         ,   //
                          &mqrc            );   //
#ifdef _LOGTERM_                                //
        pBuffer=(char*)itemValue2str( parentSelector,
                                    (MQLONG)selInt32Val) ;
        if( pBuffer )                           //
        {                                       //
          printf(" value %s", pBuffer );        //
        }                                       //
        else                                    //
        {                                       //
          printf(" value %d", (int)selInt32Val);//
        }                                       //
#endif                                          //
        break;                                  //
      }                                         //
                                                //
      // -----------------------------------------------------
      // a bag in a bag -> real data
      // -----------------------------------------------------
      case MQITEM_BAG :                         //
      {                                         //
#ifdef  _LOGTERM_                               //
        printf("\n===========================");//
        printf("===========================\n");//
#endif                                          //
        mqInquireBag( respBag       ,           //
                      parentSelector,           //
                      0             ,           //
                      &attrBag      ,           //
                      &compCode     ,           //
                      &mqrc        );           //
        switch( mqrc )                          //
        {                                       //
          case MQRC_NONE :                      //
          {                                     //
            break;                              //
          }                                     //
          default :                             //
          {                                     //
            logMQCall( ERR, "mqInquireItemBag", mqrc ); 
            goto _door;                         //
          }                                     //
        }                                       //
        logMQCall(DBG,"mqInquireItemBag",mqrc); //
                                                //
        // ---------------------------------------------------
        // count the items in the sub bag
        // ---------------------------------------------------
        mqrc=mqBagCountItem(attrBag             ,// get the amount of items
                            MQSEL_ALL_SELECTORS);//  for all selectors in 
        if( mqrc > 0 )                           //  child bag
        {                                        //
          goto _door;                            //
        }                                        //
        else                                     //
        {                                        //
          childItemCount = -mqrc;                //
          mqrc = MQRC_NONE;                      //
        }                                        //
                                                 //
        // ---------------------------------------------------
	// go through all parent items
        // ---------------------------------------------------
        for( j=0; j<childItemCount; j++ )        //
        {                                        //
          mqInquireItemInfo( attrBag           , // find out the item type
                             MQSEL_ANY_SELECTOR, //
                             j                 , //
                             &childSelector    , //
                             &childItemType    , //
                             &compCode         , //
                             &mqrc            ); //
          switch( mqrc )                         //
          {                                      //
            case MQRC_NONE: break;               //
            default :                            //
            {                                    //
              logMQCall( ERR, "mqInquireItemInfo", mqrc ); 
              goto _door;                        //
            }                                    //
          }                                      //
          logMQCall( DBG, "mqInquireItemInfo", mqrc );
                                                 //
#ifdef    _LOGTERM_                              //
          printf("\n%2d selector: %04d %-20.20s type %10.10s",
                 j                             , //
                 childSelector                 , //
                 mqSelector2str(childSelector) , //
                 mqItemType2str(childItemType)); //
#endif                                           //
                                                 //
          // -------------------------------------------------
	  // analyze each item / selector
          // -------------------------------------------------
          switch( childItemType )                //
          {                                      //
#if(0)
            // -----------------------------------------------
	    // child item is a 32 bit integer
            // -----------------------------------------------
            case MQITEM_INTEGER :                //
            {                                    //
              mqInquireInteger(attrBag     ,     // 
                               MQSEL_ANY_SELECTOR,
                               j           ,     //  
                               &selInt32Val,     //
                               &compCode   ,     //
                               &mqrc      );     //
	      switch( mqrc )                     //
              {                                  //
                case MQRC_NONE : break;          //
		default :                        //
                {                                //
                  logMQCall( ERR, "mqInquireInteger", mqrc ); 
		  goto _door;                    //
                }                                //
              }                                  //
              logMQCall( DBG, "mqInquireInteger", mqrc ); 
                                                 //
#ifdef        _LOGTERM_                          //
              pBuffer=(char*)itemValue2str(childSelector,
                                           (MQLONG)selInt32Val) ;
              if( pBuffer )                      //
              {                                  //
                printf(" value %s", pBuffer );   //
              }                                  //
              else                               //
              {                                  //
                printf(" value %d", (int)selInt32Val);
              }                                  //
#endif                                           //
              break;                             //
            }                                    //
                                                 //
            // -----------------------------------------------
	    // child item is a string
            // -----------------------------------------------
            case MQITEM_STRING :                 //
            {                                    //
              mqInquireString(attrBag   ,        // 
                              MQSEL_ANY_SELECTOR,//
                              j         ,        //  
			      MQ_INSTALLATION_PATH_LENGTH,
                              selStrVal ,        //
			      &selStrLng,        //
			      &ccsid    ,        //
                              &compCode ,        //
                              &mqrc    );        //
	      switch( mqrc )                     //
	      {                                  //
                case MQRC_NONE: break ;          //
                default:                         //
		{                                //
                  logMQCall( ERR, "mqInquireString", mqrc ); 
                  goto _door;                    //
		}                                //
	      }                                  //
              logMQCall( DBG, "mqInquireString", mqrc ); 
                                                 //
	      mqTrim( MQ_INSTALLATION_PATH_LENGTH , 
                      selStrVal,                 //
                      sBuffer  ,                 //
                      &compCode,                 //
                      &mqrc   );                 //
                                                 //
              if( childSelector == MQCA_INSTALLATION_PATH )
	      {                                  //
		strncpy( instPath,               //
                         sBuffer ,               //
                         MQCA_INSTALLATION_PATH ) ;
	      }                                  //
#ifdef        _LOGTERM_                          //
	      printf(" value %s", sBuffer );     //
#endif                                           //
	      break;                             //
	    }                                    //
                                                 //
            // -----------------------------------------------
	    // any other item type for child bag is an error
            // -----------------------------------------------
	    default :                            //
	    {                                    //
              logMQCall( ERR, "mqInquireItemInfo unexpected Type", mqrc );
	      goto _door;                        //
	    }                                    //
#endif
          }                                      //
        }                                        //
        break ;                                  //
      }                                          //
                                                 //
      // -----------------------------------------------------
      // all other parent item types are not expected
      // -----------------------------------------------------
      default :                                  //
      {                                          //
        logMQCall( ERR, "mqInquireItemInfo unexpected Type", mqrc );
	goto _door;                              //
      }                                          //
    }                                            //
  }                                              //
                                                 //

  _door:

  mqCloseBag( &cmdBag );
  mqCloseBag( &respBag );

#ifdef _LOGTERM_
  printf("\n");
#endif 

  return mqrc ;
}
