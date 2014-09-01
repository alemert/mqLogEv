/******************************************************************************/
/*                                                                            */
/*                       M Q   L O G G E R   E V E N T                        */
/*                                                                            */
/******************************************************************************/

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
int pcfReadQueue( MQHCONN  Hcon     ,  // connection handle
                  MQHOBJ   Hqueue   ,  // queue handle
                  char*    logPath  ,  // logpath, max of 82+1 incl. log file 
                  char*    currLog  ,  // current log name, max of 12+1 
                  char*    recLog   ,  // record  log name, max of 12+1
                  char*    mediaLog);  // media   log name, max of 12+1

void mqCloseDisonnect( const char* prg  ,  // program name
                       MQHCONN  Hcon    ,  // connection handle   
                       PMQHOBJ  Hqueue );  // queue handle   

int mqOlderLog( const char *log1, const char *log2) ;
int mqLogName2Id( const char* log );
int mqCleanLog( const char* logPath, const char* oldestLog );
int mqCheckLogName( const char* log) ;
void rcdMqImg( const char* qmgr ) ;

int mqBackupLog( const char* logPath ,   // original log path
                 const char* bckPath ,   // backup log path
                 const char* oldLog  ,   // oldest log name
                 const char* currLog);   // current log

int mqCopyLog( const char* orgFile, const char* cpyFile );

/******************************************************************************/
/*                                                                            */
/*                                   M A I N                                  */
/*                                                                            */
/******************************************************************************/
int cleanupLog( const char* qmgrName,  
                const char* qName   ,  
                const char* iniFile )
{
  MQTMC2 trigData ;

  MQHCONN  Hcon   ;                 // connection handle   
  MQOD     qDscr  = {MQOD_DEFAULT}; // queue descriptor
  MQHOBJ   Hqueue ;                 // queue handle   

  char logPath[124] ; // logpath, theoratical max of 82+1 (incl. log file name)
  char bckPath[124] ; // backup path, path were logs are to be backuped
  char currLog[16]  ; // current log name, max of 12+1 
  char recLog[16]   ; // record  log name, max of 12+1
  char mediaLog[16] ; // media   log name, max of 12+1
  char oldLog[16]   ; // oldest  log name, max of 12+1 equals media or record
  char opt          ; // clm option   
//struct stat ls    ; // stat structure for ls -ald

#if(0)
  char qmgrName[MQ_Q_MGR_NAME_LENGTH] ;
  char qName[MQ_Q_NAME_LENGTH] ;
  char iniFileName[255] ;
#endif

  extern char* optarg ;
  extern int   optind, optopt ;

//fncrc_t  ack;          // return vector from ini file
//sess_t  *sp = NULL;    // session descriptor (ini file)
//comp_t  *cp = NULL;    // compiler descriptro (ini file)
//data_t  *tree = NULL;  // resolve structure from ini file


  int  sysRc = 0;
  char rcBuff[64] ;

  // -------------------------------------------------------
  // read the ini file
  // -------------------------------------------------------
// fnc_init(&ack, stderr) ;   // initialization of function control code
                              //    reason codes, description
// if((rc = sess_new(&ack, stdin, stdout, stderr, &sp)) != FNC_OKAY) 
// {
     // fehler
// }
 

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
  rcdMqImg( qmgrName ); // exec RCDMQIMG

  // -------------------------------------------------------
  // read all messages from the queue, 
  // get back only the last one for analyze
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
  mqReasonId2Str( rc, rcBuff ) ; 

  // -------------------------------------------------------
  // reset was not possible, no further processing
  // -------------------------------------------------------
  if( rc != MQRC_NONE )  
  {
    if( rc == MQRC_CMD_SERVER_NOT_AVAILABLE ) 
    {
      logger(LM_MQ_CMD_STOPPED ) ;
    }
    else 
    {
      logger( LM_MQ_GENERAL_ERR, "reset qmgr type(advancelog)", rc, rcBuff ) ;
    }
  }
  // -------------------------------------------------------
  // reset was ok, backup queue manager
  // -------------------------------------------------------
  else
  {

#if(0)
    // -----------------------------------------------------
    // read all messages from the queue, 
    // get back only the last one for analyse,
    // i.g. there should be only one message from reset qmgr advance log
    // -----------------------------------------------------
    pcfReadQueue( Hcon, Hqueue, logPath, currLog, recLog, mediaLog ) ;

    logger( LM_MQ_CURRLOG , currLog  );
    logger( LM_MQ_RECLOG  , recLog   );
    logger( LM_MQ_MEDIALOG, mediaLog );
    logger( LM_MQ_LOG_PATH, logPath  );

    if( mqOlderLog(recLog,mediaLog) > 0)
    { 
      strcpy( oldLog, recLog );
    }
    else
    { 
      strcpy( oldLog, mediaLog );
    }

    logger( LM_MQ_OLDEST_LOG, oldLog ) ;
    
    // -----------------------------------------------------
    // copy files for backup
    // -----------------------------------------------------
    rc = mqBackupLog( logPath, bckPath, oldLog, currLog ) ;
    if( rc == 0 )
    {
      logger( LM_MQ_LOG_BACKUP_OK ) ;
    }
    else
    {
      logger( LM_MQ_LOG_BACKUP_ERR ) ;
    }
#endif
  }

  // -------------------------------------------------------
  // close queue
  // -------------------------------------------------------
  rc = mqCloseObject( Hcon    ,      // connection handle
                      &Hqueue );     // queue handle

  switch( rc )
  {
    case MQRC_NONE : break ;
    default        : logger(LM_SY_ABORTING, argv[0], "MQ close failed");
                     exit(1) ;
  }

  // -------------------------------------------------------
  // disconnect from queue manager
  // -------------------------------------------------------
  rc =  mqDiscon( &Hcon );  // connection handle            
  switch( rc )
  {
    case MQRC_NONE : break ;
    default        : logger(LM_SY_ABORTING, argv[0], 
                            "QM disconnect failed");
                                 exit(1) ;
  }

  _door:

  return sysRc ;
}

/******************************************************************************/
/*   R E A D   A   P C F   M E S S A G E   F R O M   A   Q U E U E            */
/******************************************************************************/
int pcfReadQueue( MQHCONN  Hcon    ,  // connection handle   
                  MQHOBJ   Hqueue  ,  // queue handle   
                  char*    logPath ,  // logpath, max of 82+1 incl. log file 
                  char*    currLog ,  // current log name, max of 12+1 
                  char*    recLog  ,  // record  log name, max of 12+1
                  char*    mediaLog)  // media   log name, max of 12+1
{
  PMQVOID msg ;                       // message buffer 
  MQMD    msgDscr = {MQMD_DEFAULT};   // msg Desriptor

  PMQCFH  pPCFh   ;    // PCF (header) pointer
  PMQCHAR pPCFcmd ;    // PCF command pointer
  PMQCFST pPCFstr ;    // PCF string pointer

  char _buff_[64] ;    // carbage buffer
  int  rc         ;    // general return code
  int  cnt        ;    // message counter

  logFuncCall() ;

  cnt = 0 ;
  while(1)
  {
    cnt++ ;
    // -------------------------------------------------------
    // read the message from the queue
    // -------------------------------------------------------
    msg = (PMQVOID) malloc(512*sizeof(char));
    rc = mqReadQueue( Hcon, Hqueue, msg,  512, &msgDscr);
  
    // -------------------------------------------------------
    // check if reading from a queue was ok
    // -------------------------------------------------------
    switch( rc )
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
        if( cnt == 1 ) return MQRC_NO_MSG_AVAILABLE ;

        // ---------------------------------------------------
        // at least one message was found
        // ---------------------------------------------------
        return MQRC_NONE ;
      }
  
      // -----------------------------------------------------
      // anything else is an error, go out of the function
      // -----------------------------------------------------
      default: 
      {
        mqReasonId2Str( rc, _buff_ ) ;
        logger( LM_MQ_GENERAL_ERR, "Read PCF", rc, _buff_ ) ;
        return rc ;
      }
    }
  
    if( memcmp( msgDscr.Format, MQFMT_EVENT, sizeof(msgDscr.Format) ) != 0 )
    {
      logger( LM_MQ_WRONG_MSG_TYPE, msgDscr.Format, MQFMT_EVENT ) ;
      logger( LM_SY_ABORTING, __FILE__, "wrong message type") ;
      exit(1) ;
    }
  
    // -------------------------------------------------------
    // set PCF pointer to the start of the message
    // set Command pointer after PCF pointer
    // -------------------------------------------------------
    pPCFh   = (PMQCFH) msg ;
    logPcfHeader( pPCFh ) ;
  
    pPCFcmd = (PMQCHAR) (pPCFh+1) ;

    while( pPCFh->ParameterCount-- > 0 )
    {
      pPCFstr = (PMQCFST) pPCFcmd ;
      logPcfString( pPCFstr, pPCFh->ParameterCount ) ;

      switch( pPCFh->ParameterCount )
      {
        // ---------------------------------------------------
        // queue manager name, ignore it
        // ---------------------------------------------------
        case 4: break ;

#if(1)
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
#endif
      }
      pPCFcmd += pPCFstr->StrucLength ;
    }
  }
  return MQRC_NONE ;
}

/******************************************************************************/
/*   U S A G E                       */ 
/******************************************************************************/
void usage(const char* prg)
{
  printf("usage   %s -m <qmgr> [ -q <queue.name>] -i <file.ini> \n", prg); 
}

/******************************************************************************/
/*   O L D E R   T R A N S A C T I O N A L   L O G                            */
/*                                                                            */
/*   finds out which of log1 and log2 ist older from transacional view        */
/*                                                                            */
/*   return code                                                        */
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
    if( strcmp(dirEntry->d_name,  "." ) == 0  ||
        strcmp(dirEntry->d_name,  "..")  == 0 )
    {
    //logger(LM_SY_SOME_STR, dirEntry->d_name );
      continue ;
    }
    if( S_ISDIR(fMode.st_mode) ) logger(LM_SY_DBG_MARKER) ;

    if( mqCheckLogName( dirEntry->d_name ) != 0 )
    {
      logger( LM_MQ_CHECK_LOG_NAME, dirEntry->d_name ) ;
      continue ;
    }
    else
    {
      logger( LM_MQ_CHECK_LOG_TIME, dirEntry->d_name, oldestLog ) ;
      if( mqOlderLog( dirEntry->d_name, oldestLog ) > 0 )
      {
        strcpy(fileName,logPath) ; 
        strcat(fileName,"/");
        strcat(fileName,dirEntry->d_name);
        logger( LM_MQ_REMOVE_LOG, fileName ) ;
        unlink(fileName);
        usleep(1000) ;
      }
      else
      {
        logger( LM_MQ_KEEP_LOG, dirEntry->d_name ) ;
      }
      
    }
  } 
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
void mqCloseDisonnect( const char* prg ,  // program name
                       MQHCONN  Hcon   ,  // connection handle   
                       PMQHOBJ  Hqueue )  // queue handle   
{
  int rc ;

  logFuncCall() ;

  // ---------------------------------------------------
  // close queue
  // ---------------------------------------------------
  rc = mqCloseObject( Hcon    ,      // connection handle
                      Hqueue );     // queue handle

  switch( rc )
  {
    case MQRC_NONE : break ;
    default        : logger(LM_SY_ABORTING, prg, "MQ close failed");
                     exit(1) ;
  }

  // ---------------------------------------------------
  // disconnect from queue manager
  // ---------------------------------------------------
  rc =  mqDiscon( &Hcon );  // connection handle            
  switch( rc )
  {
    case MQRC_NONE : break ;
    default        : logger(LM_SY_ABORTING, prg, 
                            "QM disconnect failed");
                                 exit(1) ;
  }
}

/******************************************************************************/
/*   R E C O R D   M Q   I M A G E                                            */
/******************************************************************************/
void rcdMqImg( const char* qmgr )
{
  logFuncCall() ;

  char qmgrStr[MQ_Q_MGR_NAME_LENGTH+1] ;

  int pid                 ;  // pid after fork
  int stdErr[2]           ;  // pipe file descriptors std err
  char  c                 ;  // char buffer for pipe
  char pipeBuff[PIPE_BUF] ;  // string buffer for pipe

  int i  ;
  int rc ;

  // -------------------------------------------------------
  // fork process for rcdmqimg incl. pipes
  // -------------------------------------------------------
  if( pipe(stdErr) < 0 )
  {
    logger( LM_SY_OPEN_PIPE_FAILED ) ;
  }
  else
  {
    pid = (int) fork() ;  
    switch(pid)
    {
      // ---------------------------------------------------
      // fork failed: 
      //    do not quit, trigger has to be reactevated
      // ---------------------------------------------------
      case -1: logger( LM_SY_FORK_FAILED ) ;
               break ;

      // ---------------------------------------------------
      // child:
      //    map std err to pipe
      //    close read end of the pipe, 
      //    exec rcdmqimg
      // ---------------------------------------------------
      case  0: 
      {
        logger( LM_SY_FORK_CHILD )   ; 
        // -------------------------------------------------
        // handle file descriptors
        // -------------------------------------------------
        dup2( stdErr[1], STDERR_FILENO ) ;      
        close( stdErr[0]    ) ;     
        close( stdErr[1]    ) ;      

        // -------------------------------------------------
        // setup queue manager name
        // -------------------------------------------------
        memcpy( qmgrStr, qmgr, MQ_Q_MGR_NAME_LENGTH ) ;  
        for(i=0;i<MQ_Q_MGR_NAME_LENGTH;i++) 
        {
          if( qmgrStr[i] == ' ' )
          {
            qmgrStr[i] = '\0' ;
            break   ;
          }
        }

        // -------------------------------------------------
        // exec
        // -------------------------------------------------
        execl( "/opt/mqm/bin/rcdmqimg", "rcdmqimg", "-m", qmgrStr ,
                                                    "-t", "all"   , 
                                                    "*" , NULL   );

        // -------------------------------------------------
        // there is no chance to get that far 
        // -------------------------------------------------
        logger( LM_SY_EXEC_ERR , "rcdmqimg" ) ;
        logger( LM_SY_ABORTING , "rcdmqimg" ) ;
 
        exit(0) ;                      
      }

      // ---------------------------------------------------
      // parent:
      //    close write end of the pipe
      //    read from the pipe and write to log
      // ---------------------------------------------------
      default: 
      {
        logger( LM_SY_FORK_PARENT, pid );
        // -------------------------------------------------
        // setup queue manager name
        // -------------------------------------------------
        dup2( stdErr[0], STDIN_FILENO ) ;             
        close( stdErr[0]   ) ;             
        close( stdErr[1]   ) ;             

        // -------------------------------------------------
        // redirect output from cild (rcdmqimg) to log file
        // -------------------------------------------------
        logger( LM_ST_MULTI_LINE_OPEN, "rcdmqimg" ) ;          
                                
        i=0  ;                           
        while( 1 )                        // read for ever
        {                                 // 
          rc=read( STDIN_FILENO, &c, 1 ); // read from the pipe
          if( (int)rc == 0) break ;       // stop if EOF (will work only 
          pipeBuff[i] = c ;               //       on nonblocked device)
          if( c == '\n' )                 // if eol
          {                               //
            pipeBuff[i] = '\0' ;          // replace '\n' by '\0' 
            i=0;                          // start new line
            logger( LM_ST_MULTI_LINE_TXT, pipeBuff ); 
            continue ;                    // write full line to log
          }
          i++ ; 
        }                                 //        
        if( c != '\n' )                   // if last line is not ended by '\n'
        {                                 // 
          pipeBuff[i] = '\0' ;            // write it to log file
          logger( LM_ST_MULTI_LINE_TXT, pipeBuff ) ;
        }
        logger( LM_ST_MULTI_LINE_CLOSE, "rcdmqimg" ) ;          

        // -------------------------------------------------
        // allow child to free (disable zombi)
        // -------------------------------------------------
        waitpid(pid,&rc,WNOHANG) ;
        rc >>=   8 ;
        rc  &= 127 ;
        if( rc == 0 )
        {
          logger( LM_SY_CHILD_ENDED_OK, "rcdmqimg", pid ) ;
        }
        else
        {
          logger(LM_SY_CHILD_ENDED_ERR,"rcdmqimg", pid, rc);
        }
        break ;
      }
    }
  }
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
    logger( LM_SY_MKDIR_ERR, copyFile ) ;
    return 1 ;
  }

  // -------------------------------------------------------
  // copy all relevant files
  // -------------------------------------------------------
  for( logId=oldLogId; logId<curLogId; logId++ )
  {
    sprintf( origFile, "/%s/S%07d.LOG",logPath,logId ) ;
    sprintf( copyFile, "/%s/%s/S%07d.LOG",bckPath,timeStr,logId ) ;

    logger(LM_MQ_BACKUP_LOG, origFile, copyFile) ;

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
  // open origina file for reading
  // -------------------------------------------------------
  if( (orgFd = open( orgFile, O_RDONLY) ) == -1 ) 
  {
    logger( LM_SY_OPEN_FILE, orgFile ) ;
    return 1 ;
  }

  // -------------------------------------------------------
  // open backup file for writing
  // -------------------------------------------------------
  if( (cpyFd = open( cpyFile, O_WRONLY|O_CREAT, 
                              S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP )) == -1 )
  {
    logger( LM_SY_OPEN_FILE, cpyFile ) ;
    return 1 ;
  }

  // -------------------------------------------------------
  // copy file in 4k blocks, since pagesize is 4k
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
      default           : logger(LM_SY_READ_FILE_ERR, orgFile);
                          write( cpyFd, copyBuff, rc );
                          close(orgFd) ;
                          close(cpyFd) ;
                          return 1 ;
    } 
  }
}
