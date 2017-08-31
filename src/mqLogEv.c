/******************************************************************************/
/*                                                                            */
/*                       M Q   L O G G E R   E V E N T                        */
/*                                                                            */
/*   functions:                                                               */
/*     - cleanupLog                                                           */
/*     - cleanupBackup                                                        */
/*     - pcfReadQueue                                                         */
/*     - rmRecrusive                                                          */
/*     - mqOlderLog                                                           */
/*     - mqLogName2Id                                                         */
/*     - mqHandleLog                                                          */
/*     - mqCheckLogName                                                       */
/*     - mqCloseDisconnect                                                    */
/*     - rcdMqImg                                                             */
/*     - mqCopyLog                                                            */
/*     - copyFile                                                             */
/*     - copyQmIni                                                            */
/*     - copySslRepos                                                         */
/*     - copyCatalog                                                          */
/*     - callZipFile                                                          */
/*     - createQmgrObject                                                     */
/*     - deleteQmgrObject                                                     */
/*     - getQmgrStatus                                                        */
/*     - getQmgrObject                                                        */
/*                                                                            */
/******************************************************************************/
#define C_MODULE_MQLOGEV

/******************************************************************************/
/*                             D E B U G G I N G                              */
/******************************************************************************/
#define _LOGTERM_                              
#undef  _LOGTERM_                           

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
#include <libgen.h>

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

#define ITEM_LENGTH MQ_LOG_PATH_LENGTH 

#if ITEM_LENGTH < MQ_INSTALLATION_PATH_LENGTH 
#undef  ITEM_LENGTH 
#define ITEM_LENGTH  MQ_INSTALLATION_PATH_LENGTH 
#endif

#if ITEM_LENGTH < MQ_SSL_KEY_REPOSITORY_LENGTH
#undef  ITEM_LENGTH 
#define ITEM_LENGTH  MQ_SSL_KEY_REPOSITORY_LENGTH
#endif

#if(0)
#define BACKUP_AUDIT_SUB_DIR    "audit"
#endif
#define BACKUP_RECOVER_SUB_DIR  "recover"
#define BACKUP_CONFIG_SUB_DIR   "config"

#define CONTROL_FILE     "amqhlctl.lfh" 
#define CATALOG_FILE     "QMQMOBJCAT"

#define CLEANUP_DIR_LENGTH    10

/******************************************************************************/
/*                               S T R U C T S                                */
/******************************************************************************/
struct sListFile
{
  char   name[NAME_MAX+1];
  time_t mtime ;
};

/******************************************************************************/
/*                                 T Y P E S                                  */
/******************************************************************************/
typedef struct sListFile tListFile ;

/******************************************************************************/
/*   P R O T O T Y P E S                                                      */
/******************************************************************************/
void usage( );
int rmRecrusive( const char* path ) ;

MQLONG pcfReadQueue( MQHCONN Hcon, // connection handle
                     MQHOBJ Hqueue, // queue handle
//                   char*  logPath  ,  // logpath, max of 82+1 incl. log file 
                     char* currLog, // current log name, max of 12+1 
                     char* recLog, // record  log name, max of 12+1
                     char* mediaLog ); // media   log name, max of 12+1

MQLONG mqCloseDisconnect( MQHCONN Hcon, // connection handle   
                          PMQHOBJ Hqueue ); // queue handle   

int mqOlderLog( const char *log1, const char *log2 );
int mqLogName2Id( const char* log );
int mqHandleLog( MQHCONN hConn,
                 const char* logPath,
                 const char* bckPath,
                 const char* oldestLog,
                 const char* zipBin );
int mqCheckLogName( const char* log );
int rcdMqImg( const char* _qmgr, const char* _instPath );

int mqCopyLog( const char* orgFile, const char* cpyFile );
int copyFile( const char *src, const char *goal );
int copyQmIni( const char *origPath, const char *goalPath );
int copySslRepos( const char *mqSslPath,
                  const char* mqSslRep,
                  const char *bckPath );
int callZipFile( const char* zipBin, const char* file );
int copyCatalog( const char *mqDataPath, const char *bckPath );

tQmgrObj* createQmgrObject( );
void deleteQmgrObject( tQmgrObj *qmgrObj );
MQLONG getQmgrStatus( MQHCONN Hconn, tQmgrObj* pQmgrObjStatus );
MQLONG getQmgrObject( MQHCONN Hconn, tQmgrObj* pQmgrObjStatus );

/******************************************************************************/
/*                                                                            */
/*                          C L E A N U P   L O G S                           */
/*                                                                            */

/******************************************************************************/
int cleanupLog( const char* _qmgrName, // queue manager name
                const char* _qName, // logger event name
                tBackup _bck ) // backup properties
{
  logFuncCall( );

  MQHCONN Hcon;                // connection handle   
  MQOD qDscr = {MQOD_DEFAULT}; // queue descriptor
  MQHOBJ Hqueue;               // queue handle   
                               //
  tQmgrObj *pQmgrObj = NULL;   // queue manager properties needed for backup
                               //
  char currLog[MQ_LOG_EXTENT_NAME_LENGTH + 1];  // current log name
  char recLog[MQ_LOG_EXTENT_NAME_LENGTH + 1];   // record  log name
  char mediaLog[MQ_LOG_EXTENT_NAME_LENGTH + 1]; // media   log name
  char oldLog[MQ_LOG_EXTENT_NAME_LENGTH + 1];   // oldest  log name
                                                //
  char bckSslPath[PATH_MAX + 1];     // path to SSL Backup directory
  char bckCfgPath[PATH_MAX + 1];     // path to SSL Backup directory
  //char bckAuditPath[PATH_MAX+1];   // path for audit backup 
  char bckRecoverPath[PATH_MAX + 1]; // path for recovery backup 

  MQLONG sysRc = MQRC_NONE;
  MQLONG locRc = MQRC_NONE;


  //snprintf(bckAuditPath,  PATH_MAX,"%s/%s/",_bck.path,BACKUP_AUDIT_SUB_DIR  );
  snprintf( bckRecoverPath, PATH_MAX, "%s/%s/", _bck.path, BACKUP_RECOVER_SUB_DIR );

  // -------------------------------------------------------
  // connect to queue manager
  // -------------------------------------------------------
  sysRc = mqConn( (char*) _qmgrName, // queue manager          
                  &Hcon );           // connection handle            
                                     //
  switch( sysRc )                    //
  {                                  //
    case MQRC_NONE: break;           // OK
    case MQRC_Q_MGR_NAME_ERROR:      // queue manager does not exists
    {                                //
      logger( LMQM_UNKNOWN_QMGR, _qmgrName );
      goto _door;                    //
    }                                //
    default: goto _door;             // error will be logged in mqConn
  }                                  //

  // -------------------------------------------------------
  // open queue
  // -------------------------------------------------------
  memcpy( qDscr.ObjectName, _qName, MQ_Q_NAME_LENGTH );

  sysRc = mqOpenObject( Hcon                  , // connection handle
                        &qDscr                , // queue descriptor
                        MQOO_INPUT_EXCLUSIVE  |
                        MQOO_SET              |
                        MQOO_FAIL_IF_QUIESCING, // open options
                        &Hqueue              ); // queue handle

  switch( sysRc )
  {
    case MQRC_NONE: break;
    default: goto _door;
  }

  // -------------------------------------------------------
  // get installation path,
  //     log path
  //     SSL key repository path
  //     QM.INI path will be produced from SSL path
  // -------------------------------------------------------
  pQmgrObj = createQmgrObject( );
  if( pQmgrObj == NULL )
  {
    sysRc = -1;
    goto _door;
  }

  sysRc = getQmgrStatus( Hcon, pQmgrObj );

  switch( sysRc )
  {
    case MQRC_NONE: break;
    default: goto _door;
  }

  switch( pQmgrObj->reason )
  {
    case MQRC_NONE: break;
    default: goto _door;
  }

  sysRc = getQmgrObject( Hcon, pQmgrObj );

  switch( sysRc )
  {
    case MQRC_NONE: break;
    default: goto _door;
  }

  switch( pQmgrObj->reason )
  {
    case MQRC_NONE: break;
    default: goto _door;
  }

  memcpy( pQmgrObj->dataPath, pQmgrObj->sslPath, strlen( pQmgrObj->sslPath ) );
  memcpy( pQmgrObj->sslRep,
          (char*) basename( pQmgrObj->sslPath ),
          strlen( pQmgrObj->sslPath ) );
  dirname( dirname( pQmgrObj->dataPath ) );
  dirname( pQmgrObj->sslPath );

  // -------------------------------------------------------
  // set trigger on
  // -------------------------------------------------------
  sysRc = mqSetTrigger( Hcon   ,  // connection handle
                        Hqueue ); // queue handle

  switch( sysRc )
  {
    case MQRC_NONE: break;
    default: goto _door;
  }

#if(0)
  // -------------------------------------------------------
  // backup old logs for audit
  // -------------------------------------------------------
  if( _bck.audit == ON ) //
  { // 
    //
    sysRc = mqHandleLog( Hcon, // connection handle for reset
                         pQmgrObj->logPath, // original log path
                         bckAuditPath, // path save the logs
                         NULL, // oldest log 
                         _bck.zip ); // zip binary
    //
    if( sysRc != 0 ) goto _door; //
  } //
#endif
  //
  // -------------------------------------------------------
  // backup configuration and security i.g.
  //    - QM.INI
  //    - SSL key.*
  // -------------------------------------------------------
  if( _bck.path )                            //
  {                                          //
    snprintf( bckCfgPath, PATH_MAX, "%s/%s", // create extra directory 
              _bck.path                    , //  for SSL backup
              BACKUP_CONFIG_SUB_DIR       ); //
                                             //
    snprintf( bckSslPath, PATH_MAX, "%s/%s", // create extra directory 
              bckCfgPath                   , //  for SSL backup
              (char*) basename( pQmgrObj->sslPath ) );
                                             //
    copyQmIni( pQmgrObj->dataPath,           //
               bckCfgPath        );          // backup QM.INI
                                             //
    copySslRepos( pQmgrObj->sslPath,         // backup whole SSl directory
                  pQmgrObj->sslRep ,         //
                  bckSslPath      );         //
    copyCatalog( pQmgrObj->dataPath,         //
                 bckCfgPath        );        // backup object catalog
  }                                          //
  //
  // -------------------------------------------------------
  // fork process for RCDMQIMG, log RCDMQIMG output to log
  // -------------------------------------------------------
  rcdMqImg( _qmgrName, pQmgrObj->instPath ); // exec RCDMQIMG

  // -------------------------------------------------------
  // read all messages from the queue, 
  // get back only the last one for analyzing
  // -------------------------------------------------------
  //logPath[0]  = '\0' ;
  currLog[0] = '\0';
  recLog[0] = '\0';
  mediaLog[0] = '\0';

  sysRc = pcfReadQueue( Hcon,
                        Hqueue,
                        currLog,
                        recLog,
                        mediaLog );
  switch( sysRc )
  {
      // -----------------------------------------------------
      // continue work, at least one message was found
      // -----------------------------------------------------
    case MQRC_NONE:
    {
      break;
    }

      // -----------------------------------------------------
      // no message at all was found; 
      //   - write to log
      //   - close queue
      //   - disconnect queue manager
      //   - quit
      // -----------------------------------------------------
    case MQRC_NO_MSG_AVAILABLE:
    {
      logger( LMQM_QUEUE_EMPTY, _qName );
      goto _door;
    }

      // -----------------------------------------------------
      // anything else is an error, go out of the function
      // -----------------------------------------------------
    default:
    {
      logger( LSTD_GEN_SYS, progname );
      goto _door;
    }
  }

  logger( LMQM_LOG_NAME, "CURRENT", currLog );
  logger( LMQM_LOG_NAME, "RECORD", recLog );
  logger( LMQM_LOG_NAME, "MEDIA", mediaLog );
  //logger( LMQM_LOG_NAME, "PATH"   , logPath  );

#if(0)
  if( logPath[0] == '\0' ||
      currLog[0] == '\0' ||
      recLog[0] == '\0' ||
      mediaLog[0] == '\0' )
  {
    sysRc = MQRC_NO_MSG_AVAILABLE;
    logger( LMQM_QUEUE_EMPTY, _qName );
    goto _door;
  }
#endif

  if( pQmgrObj->logPath[0] != '/' ||
      currLog[0] != 'S' ||
      recLog[0] != 'S' ||
      mediaLog[0] != 'S' )
  {
    logger( LMQM_LOG_NAME_INVALIDE );
    goto _door;
  }

  if( mqOlderLog( recLog, mediaLog ) > 0 )
  {
    strcpy( oldLog, recLog );
  }
  else
  {
    strcpy( oldLog, mediaLog );
  }

  logger( LMQM_LOG_NAME, "OLDEST", oldLog );

#if(0)
  // -------------------------------------------------------
  // remove old logs
  // -------------------------------------------------------
  sysRc = mqHandleLog( hConn, // connection handle
                       pQmgrObj->logPath, // original log path
                       NULL, // no copy
                       oldLog, // oldest log (keep 
                       NULL ); // zip binary

  if( sysRc != 0 ) goto _door;
#endif

  // -------------------------------------------------------
  // backup old logs for recovery
  // -------------------------------------------------------
#if(0)
  //if( _bck.recover == ON )                      //
  //{                                             // send 
  sysRc = mqResetQmgrLog( Hcon ); //  RESET QMGR TYPE(ADVANCEDLOG) 
  // to the command server
  switch( sysRc ) //
  { //
    case MQRC_NONE: //
      break; //
      //
    case MQRC_CMD_SERVER_NOT_AVAILABLE: // reset was not possible, 
      goto _door; //   no further processing
      //
    default: //
      goto _door; //
  } //
#endif
  //
  sysRc = mqHandleLog( Hcon             , //
                       pQmgrObj->logPath, // original log path
                       bckRecoverPath   , // path save the logs
                       oldLog           , // oldest log 
                       _bck.zip        ); // zip binary
                                          //
  if( sysRc != 0 ) goto _door;            //
                                          //                                             //

_door:

  // -------------------------------------------------------
  // close queue
  // -------------------------------------------------------
  locRc = mqCloseObject( Hcon     , // connection handle
                         &Hqueue ); // queue handle

  switch( locRc )
  {
    case MQRC_NONE: break;
    default: logger( LSTD_GEN_SYS, progname );
      break;
  }

  // -------------------------------------------------------
  // disconnect from queue manager
  // -------------------------------------------------------
  locRc = mqDisc( &Hcon ); // connection handle            
  switch( locRc )
  {
    case MQRC_NONE: break;
    default: logger( LSTD_GEN_SYS, progname );
      break;
  }

  if( sysRc == MQRC_NONE )
  {
    sysRc = locRc;
  }

  // -> remove old logs  

  deleteQmgrObject( pQmgrObj );

  logFuncExit( );

  return sysRc;
}

/******************************************************************************/
/*                                                                            */
/*                          C L E A N U P   L O G S                           */
/*                                                                            */
/******************************************************************************/
int cleanupBackup( const char* _basePath, // base backup path
                   const char* _actPath , // base backup path
                   int _generation )
{
  logFuncCall( ); //

  // -----------------------------------
  // Directory vara
  // -----------------------------------
  DIR *dir;                // directory pointer
  struct dirent *dirEntry; // directory entry
  char *path = NULL      ; // name for opendir has to come from strdup()

  // -----------------------------------
  // Directory time vara
  // -----------------------------------
  struct stat dirStat;     // for time of directory

  tListFile dirList[CLEANUP_DIR_LENGTH];

  int sysRc = 0;
  int i, j;
  int oldestIx;
  int generationSum;

  if( _basePath == NULL ) goto _door;
 
  path = strdup( _basePath );

  dir = opendir( path );                         // open source directory 
  if( dir == NULL )                              //   for list all files
  {                                              //
    logger( LSTD_OPEN_DIR_FAILED, _basePath );   //
    logger( LSTD_ERRNO_ERR, sysRc, strerror( sysRc ) );
    goto _door;                                  //
  }                                              //
                                                 //
  // -------------------------------------------------------
  // list all files in source directory
  // -------------------------------------------------------
#if(0)
  dirList = (tListFile*) malloc( sizeof(tListFile) * 
                                 ( CLEANUP_DIR_LENGTH + _generation) );
  for(i=0; i<( CLEANUP_DIR_LENGTH + _generation); i++ )
  {

  }
#endif
  if( dirList == NULL )                          //
  {                                              //
    logger( LSTD_MEM_ALLOC_ERROR );              //
    logger( LSTD_ERRNO_ERR, errno, strerror( errno ) );
    sysRc = errno;                               //
    goto _door;                                  //
  }                                              //
  for( i=0; i < CLEANUP_DIR_LENGTH; i++ )
  {                                              //
    dirList[i].name[0]='\0';                     //
  }                                              //
                                                 //
  i=0;                                           //
  while( NULL != (dirEntry = readdir( dir )) )   // 
  {                                              //
    if( strcmp( dirEntry->d_name, "."  ) == 0 || // skip directory . and ..
        strcmp( dirEntry->d_name, ".." ) == 0 )  //
    {                                            //
      continue;                                  //
    }                                            //
    snprintf( dirList[i].name, NAME_MAX        , //
                               "%s/%s"         , //
                               _basePath       , //
                               dirEntry->d_name);//
    sysRc = stat( dirList[i].name, &dirStat );   //
    if( sysRc != 0 )                             //
    {                                            //
      sysRc = errno;                             //
      logger( LSTD_GET_FILE_SIZE, dirList[i].name ) ;
      logger( LSTD_ERRNO_ERR, errno, strerror( errno ) );
      goto _door;                                //
    }                                            //
    dirList[i].mtime = dirStat.st_mtim.tv_sec;   //
    i++;                                         //
    if( i > CLEANUP_DIR_LENGTH)                  //
    {                                            //
      break;                                     //
    }                                            //
  }                                              //
                                                 //
  // -------------------------------------------------------
  // delete old backups
  // -------------------------------------------------------
  for( i=0;                                      // outer loop: in every pass
       i<(CLEANUP_DIR_LENGTH);                   // delete the oldest backup
       i++ )                                     // from the list
  {                                              //
    oldestIx = -1;                               // index of the oldest backup 
    generationSum = 0;                           // backups left in the list
    if( dirList[i].name[0] == '\0' ) continue;   // already deleted
                                                 //
    for( j=0;                                    // inner loop:
	 j<CLEANUP_DIR_LENGTH;                   // find the oldest backup, 
	j++ )                                    // count existing backups 
    {                                            //
      if( dirList[j].name[0] == '\0' ) continue; // already deleted
      generationSum++;                           //
      if( dirList[j].mtime < dirList[i].mtime )  // temporary oldest found
      {                                          //
	oldestIx = j;                            //
      }                                          //
    }                                            //
                                                 //
    if( oldestIx  == -1 )                        // nothing found
    {                                            //
      break;                                     //
    }                                            //
    if( generationSum <= _generation )           // minimal amount of
    {                                            //  generations reached
      break;                                     //
    }                                            //
    if( strcmp( dirList[oldestIx].name,          // do not delete the backup
	        _actPath              ) == 0 )   //  just created
    {                                            //
      continue;                                  //
    }                                            //
                                                 //
    rmRecrusive( dirList[oldestIx].name );       //
    dirList[oldestIx].name[0]='\0';
  }                                              //

  closedir( dir );

_door:

  if( path    != NULL ) { free(path);    }
//if( dirList != NULL ) { free(dirList); }

  logFuncExit( );

  return sysRc;
}

/******************************************************************************/
/*                           R E M O V E   F I L E S                          */
/*      I N C L U D I N G   D I R E C T O R I E S   R E C U R S I V E L Y     */
/******************************************************************************/
int rmRecrusive( const char* _path )
{
  logFuncCall( ); //

  struct stat fInf;     // for time of directory
  DIR *dir;                // directory pointer
  struct dirent *dirent; // directory entry

  char fullFileName[NAME_MAX+1];
  char *path = NULL;
  int sysRc = 0 ;

  // -------------------------------------------------------
  // get the file type 
  // -------------------------------------------------------
  sysRc = stat( _path, &fInf );                // get the type of the file
  if( sysRc != 0 )                             //
  {                                            //
    sysRc = errno;                             //
    logger( LSTD_GET_FILE_SIZE, path );        //
    logger( LSTD_ERRNO_ERR, errno, strerror( errno ) );
    goto _door;                                //
  }                                            //
                                               //
  // -------------------------------------------------------
  // evaluate the file type
  // -------------------------------------------------------
  path = (char*) strdup( (char*)_path ) ;

  // -----------------------------------------------------
  // regular file or soft link
  // -----------------------------------------------------
  if( ( (S_IFREG & fInf.st_mode) > 0 ) ||    // regular file
      ( (S_IFLNK & fInf.st_mode) > 0 )  )    // soft link
  {                                          //
    unlink( path );                          //
  }                                          //
  else if( (S_IFDIR & fInf.st_mode) > 0 )    //
  {                                          //
    dir = opendir( path );                   // open source directory 
    if( dir == NULL )                        //   for list all files
    {                                        //
      logger( LSTD_OPEN_DIR_FAILED, path );  //
      logger( LSTD_ERRNO_ERR, sysRc, strerror( sysRc ) );
      goto _door;                            //
    }                                        //
                                             //
    while( NULL != (dirent =readdir(dir)) )  // 
    {                                        //
      if( strcmp(dirent->d_name,"." ) == 0 ||// skip directory . and ..
          strcmp(dirent->d_name,"..") == 0 ) //
      {                                      //
        continue;                            //
      }                                      //
      snprintf( fullFileName, NAME_MAX,      //
                              "%s/%s" ,      //
                              path    ,      //
                              dirent->d_name);
      sysRc = rmRecrusive( fullFileName );   //
      if( sysRc != 0 )                       //
      {                                      //
	logger( LSTD_ERR_REMOVE_DIR, fullFileName );
        logger( LSTD_ERRNO_ERR, sysRc, strerror( sysRc ) );
        sysRc = errno;                       //
	goto _door;                          //
      }                                      //
    }                                        //
    sysRc = rmdir(path);                     //
    if( sysRc != 0 )                         //
    {                                        //
      logger( LSTD_ERR_REMOVE_DIR, path );   //
      logger( LSTD_ERRNO_ERR, sysRc, strerror( sysRc ) );
      sysRc = errno;                         //
      goto _door;                            //
    }                                        //
  }                                          //
  else                                       //
  {                                          //
    goto _door;                              //
  }                                          //
                                             //
  _door:

  if( path != NULL ) free( path );

  logFuncExit( );

  return sysRc;
}

/******************************************************************************/
/*   R E A D   A   P C F   M E S S A G E   F R O M   A   Q U E U E            */

/******************************************************************************/
MQLONG pcfReadQueue( MQHCONN Hcon,    // connection handle   
                     MQHOBJ Hqueue,   // queue handle   
                  // char*  logPath,  // logpath, max of 82+1 incl. log file 
                     char* currLog,   // current log name, max of 12+1 
                     char* recLog,    // record  log name, max of 12+1
                     char* mediaLog ) // media   log name, max of 12+1
{                                     // 
  logFuncCall( );                     //

  PMQVOID msg;                 // message buffer 
  MQMD md = {MQMD_DEFAULT};    // message descriptor
  MQGMO gmo = {MQGMO_DEFAULT}; // get message options
  MQLONG msgSize = 512;        //
                               //
  PMQCFH pPCFh;                // PCF (header) pointer
  PMQCHAR pPCFcmd;             // PCF command pointer
  PMQCFST pPCFstr;             // PCF string pointer
                               //
  int sysRc = MQRC_NONE;       // general return code
  MQLONG mqreason;             // MQ return code
  int cnt;                     // message counter
  int loop = 0;                //
  //
  //
  // ---------------------------------------------------------
  // prepare get message options 
  //   and message descriptor for reading
  // ---------------------------------------------------------
  gmo.Version = MQGMO_VERSION_2; // avoid need to reset message id and
  gmo.MatchOptions = MQMO_NONE; // corelation id after every MQGET
  //
  // ---------------------------------------------------------
  // read all messages from queue and count them
  // ---------------------------------------------------------
  cnt = 0;

  loop = 1;
  while( loop )
  {
    cnt++;
    // -------------------------------------------------------
    // read a single message from the queue
    // -------------------------------------------------------
    msg = (PMQVOID) malloc( msgSize * sizeof (char) );
    if( msg == NULL )
    {
      logger( LSTD_MEM_ALLOC_ERROR );
      sysRc = errno;
      goto _door;
    }
    mqreason = mqGet( Hcon    , // connection handle
                      Hqueue  , // object(queue) handle
                      msg     , // message buffer
                      &msgSize, // message size
                      &md     , // message descriptor 
                      gmo     , // get message options
                      500    ); // wait interval (to adjust gmo)

    // -------------------------------------------------------
    // check if reading from a queue was OK
    // -------------------------------------------------------
    switch( mqreason )
    {
        // -----------------------------------------------------
        // stay in the loop, read next message
        // -----------------------------------------------------
      case MQRC_NONE: break;

        // -----------------------------------------------------
        // this is the last message, break out of the loop
        // -----------------------------------------------------
      case MQRC_NO_MSG_AVAILABLE:
      {
        // ---------------------------------------------------
        // at least one message found
        // ---------------------------------------------------
        sysRc = mqreason;
        if( cnt > 1 )
        {
          sysRc = MQRC_NONE;
        }

        loop = 0;
        continue;
      }

        // -----------------------------------------------------
        // anything else is an error, go out of the function
        // -----------------------------------------------------
      default:
      {
        sysRc = mqreason;
        goto _door;
        ;
      }
    }

    if( memcmp( md.Format, MQFMT_EVENT, sizeof (md.Format) ) != 0 )
    {
      sysRc = MQRC_FORMAT_ERROR;
      logMQCall( CRI, "MQGET", sysRc );
      goto _door;
    }

    // -------------------------------------------------------
    // set PCF pointer to the start of the message
    // set Command pointer after PCF pointer
    // -------------------------------------------------------
    pPCFh = (PMQCFH) msg;
    dumpMqStruct( MQFMT_PCF, pPCFh, NULL );

    pPCFcmd = (PMQCHAR) (pPCFh + 1);

    while( pPCFh->ParameterCount-- > 0 )
    {
      pPCFstr = (PMQCFST) pPCFcmd;

      dumpMqStruct( "PCFSTR", pPCFstr, NULL ); // pPCFh->ParameterCount ) ;

      switch( pPCFh->ParameterCount )
      {
          // ---------------------------------------------------
          // queue manager name, ignore it
          // ---------------------------------------------------
        case 4: break;

          // ---------------------------------------------------
          // current log
          // ---------------------------------------------------
        case 3: memcpy( currLog, pPCFstr->String, pPCFstr->StringLength );
          currLog[pPCFstr->StringLength] = '\0';
          //      logger( LM_SY_SOME_STR, currLog ) ;
          //      logger( LM_SY_SOME_STR, pPCFstr->String ) ;
          //      logger( LM_SY_SOME_INT, pPCFstr->StringLength ) ;
          break;

          // ---------------------------------------------------
          // record log
          // ---------------------------------------------------
        case 2: memcpy( recLog, pPCFstr->String, pPCFstr->StringLength );
          recLog[pPCFstr->StringLength] = '\0';
          break;

          // ---------------------------------------------------
          // media log
          // ---------------------------------------------------
        case 1: memcpy( mediaLog, pPCFstr->String, pPCFstr->StringLength );
          mediaLog[pPCFstr->StringLength] = '\0';
          break;

          // ---------------------------------------------------
          // log path -> will be evaluated by DISPLAY QMSTATUS  
          // ---------------------------------------------------
#if(0)
        case 0: memcpy( logPath, pPCFstr->String, pPCFstr->StringLength );
          logPath[pPCFstr->StringLength] = '\0';
          break;
#endif

        default: break;
      }
      pPCFcmd += pPCFstr->StrucLength;
    }
  }

_door:

  logFuncExit( );

  return sysRc;
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
int mqOlderLog( const char *log1, const char *log2 )
{
  logFuncCall( );

  int cnt1;
  int cnt2;

  cnt1 = mqLogName2Id( log1 );
  cnt2 = mqLogName2Id( log2 );

  if( cnt1 > cnt2 ) return -1;
  if( cnt1 < cnt2 ) return +1;

  logFuncExit( );

  return 0;
}

/******************************************************************************/
/*   L O G   N A M E   T O   I D                                              */
/******************************************************************************/
int mqLogName2Id( const char* log )
{
  logFuncCall( );

  char buff[16];
  int id;
  char *p;

  strcpy( buff, log );

  buff[0] = '0';

  p = buff;
  while( *++p != '.' );
  *p = '\0';
  id = (int) atoi( buff );

  logFuncExit( );

  return id;
}

/******************************************************************************/
/*   C L E A N   T R A N S A C T I O N A L   L O G S                          */
/*                                                                            */
/*   attributes:                                                              */
/*      - logPath:   absolute path to original transactional logs             */
/*                   including /../active/                                    */
/*      - bckPath:   path to backup location. If bckPath == NULL no backup    */
/*                   will be done, files will be just removed                 */
/*      - oldestLog: files older then this are not necessary for active work  */
/*                   and will be removed on logPath location.                 */
/*                   if oldesLog == NULL no remove will be done, files will   */
/*                   just be copied                                           */
/*      - zipBin:    compress program to use i.g. /bin/zip                    */
/*                                                                            */
/*   description:                                                             */
/*      1) create a backup directory on bckPath  ,                            */
/*         directory should include time stamp in the name with               */
/*         format YYYY.MM.DD-hh.mm-ss                                         */
/*      2) all transactional logs and the active control file "amqhlctl.lfh"  */
/*         will be copied to the backup location.                             */
/*      3) transactional logs on the backup location will be compressed       */
/*      4) all files older then the oldest log will be removed from the       */
/*         original location                                                  */
/*      5) if backup location is null, no backup will be done, only files     */
/*         older then oldest log will be removed                              */
/*      6) if oldest log is null just copy will be done, files will not be    */
/*         removed                                                            */
/*                                                                            */

/******************************************************************************/
int mqHandleLog( MQHCONN hConn,
                 const char* logPath,
                 const char* bckPath,
                 const char* oldestLog,
                 const char* zipBin )
{
  logFuncCall( );

  int sysRc = 0;

  DIR *orgDir; // source directory pointer
  struct dirent *orgDirEntry; // source directory entry
  char orgFile[PATH_MAX];     // source file name
  char cpyFile[PATH_MAX];     //

  char actBckPath[PATH_MAX];
  char logPathShort[PATH_MAX]; // path to control file
  char orgCtrlFile[PATH_MAX];
  char bckCtrlFile[PATH_MAX];

  int actLogId = 0;    // actual file id S{id}.LOG
  int maxLogId = 0;    // highest file id S{id}.LOG
  int oldestLogId = 0; // oldest file id S{id}.LOG

  if( oldestLog == NULL ) // null pointer exception
  {
    sysRc = 1;
    goto _door;
  }

  oldestLogId = mqLogName2Id( oldestLog );

  // -------------------------------------------------------
  // initialize all directories
  // -------------------------------------------------------
  snprintf( logPathShort,  // cut off 'active' from the end of the path
            PATH_MAX    ,  // to transactional logs to get 
            "%s"        ,  // the path to control file
            logPath    );  // -----
  dirname( logPathShort ); //  
                           //
  snprintf( orgCtrlFile, PATH_MAX, "%s/%s", logPathShort, CONTROL_FILE );
  snprintf( bckCtrlFile, PATH_MAX, "%s/%s", bckPath, CONTROL_FILE );
                                          //
  orgDir = opendir( logPath );            // open the source directory 
  if( orgDir == NULL )                    //   to list all files
  {                                       //
    logger(LSTD_OPEN_DIR_FAILED,logPath); //
    logger( LSTD_ERRNO_ERR,errno,strerror( errno ) ); //
    sysRc = errno;                        //
    goto _door;                           //
  }                                       //
                                          //
  if( bckPath != NULL )                   //
  {                                       //
    sprintf( actBckPath, "%s/active", bckPath ); //
    sysRc = mkdirRecursive( actBckPath, 0775 ); //
    if( sysRc != 0 )                      // create goal directory 
    {                                     //
      logger( LSTD_MAKE_DIR_FAILED, actBckPath ); //
      logger( LSTD_ERRNO_ERR, sysRc, strerror( sysRc ) );
      goto _door;                         //
    }                                     //
  }                                       //
                                          //
  // -------------------------------------------------------
  // list all files in source directory
  // -------------------------------------------------------
  while( NULL != (orgDirEntry = readdir( orgDir )) ) // 
  { //
    if( strcmp( orgDirEntry->d_name, "." ) == 0 || // skip directory . and ..
        strcmp( orgDirEntry->d_name, ".." ) == 0 ) //
    { //
      continue; //
    } //
    //
    if( mqCheckLogName( orgDirEntry->d_name ) != 0 ) // file does not match 
    { // naming standards for 
      continue; // transactional logs
    } //
    //
    strcpy( orgFile, logPath ); // set up absolute source 
    strcat( orgFile, "/" ); //   file name 
    strcat( orgFile, orgDirEntry->d_name ); //
    //
    // -----------------------------------------------------
    // handle all archive logs 
    // -----------------------------------------------------
    if( mqOlderLog( orgDirEntry->d_name, oldestLog ) > 0 ) // log is not needed
    { //  any more
      if( bckPath != NULL ) // archiving is on
      { //
        strcpy( cpyFile, actBckPath ); // set up absolute goal  
        strcat( cpyFile, "/" ); //  file name
        strcat( cpyFile, orgDirEntry->d_name ); //
        //
        sysRc = mqCopyLog( orgFile, cpyFile ); // copy file
        if( sysRc != 0 ) goto _door; //
        //
        sysRc = callZipFile( zipBin, cpyFile ); // compress file
        if( sysRc != 0 ) goto _door; //
      } //
      //
      logger( LMQM_INACTIVE_LOG, orgDirEntry->d_name ); // remove the file even 
      unlink( orgFile ); // if archiving off
      usleep( 1000 ); //
    } //
    else //
    { //
      logger( LMQM_ACTIVE_LOG, orgDirEntry->d_name ); //   keep the log
    } //
  } //
  //
  closedir( orgDir ); //
  //
  // -------------------------------------------------------
  // if transactional logs have been copied, also:
  //   - reset advance log 
  //   - copy control file
  //   - copy additional log file, that might have been 
  //      created by advance log command
  // -------------------------------------------------------
  if( bckPath != NULL ) //
  { //
    // -----------------------------------------------------
    // start writing into new log
    // -----------------------------------------------------
    sysRc = mqResetQmgrLog( hConn ); // send
    // RESET QMGR ADVANCEDLOG 
    switch( sysRc ) // to the command server
    { //
      case MQRC_NONE: //
        break; //
        //
      case MQRC_CMD_SERVER_NOT_AVAILABLE: // reset was not possible, 
        goto _door; //   no further processing
        //
      default: //
        goto _door; //
    } //
    //
    // -----------------------------------------------------
    // copy control file
    // -----------------------------------------------------
    sysRc = copyFile( orgCtrlFile, bckCtrlFile ); //
    if( sysRc != 0 ) goto _door; //
    //
    // -----------------------------------------------------
    // copy all other (active?) files, 
    // -----------------------------------------------------
    for( actLogId = oldestLogId; actLogId < 10000000; actLogId++ )
    { // go through all files, file names
      snprintf( orgFile, // higher than S9999999.LOG are not
                PATH_MAX, // possible -> limit of 10000000 
                "%s/S%07d.LOG", //
                logPath, // set the original file
                actLogId ); //  name
      //
      snprintf( cpyFile, // set the copy file name
                PATH_MAX, //
                "%s/S%07d.LOG", //
                actBckPath, //
                actLogId ); //
      //
      if( access( orgFile, F_OK ) == 0 ) // check if file exists
      { //
        mqCopyLog( orgFile, cpyFile ); // copy file
      } //
      else // break the loop if no file found
      { //  the limit in the head of for 
        errno = 0; //  loop is just to break the loop
        break; //  if something (unknown) goes 
      } //  wrong
    } //
    maxLogId = actLogId; //
    //
    // -----------------------------------------------------
    // zip all other (active?) files
    // -----------------------------------------------------
    for( actLogId = oldestLogId; actLogId < maxLogId; actLogId++ )
    { //
      snprintf( orgFile, // copying and compressing
                PATH_MAX, //  of active files is not done
                "%s/S%07d.LOG", //  in one loop, since copy should
                logPath, //  be done as quick as possible to
                actLogId ); //  achieve high consistence of 
      //  the copied files.
      snprintf( cpyFile, // 
                PATH_MAX, //
                "%s/S%07d.LOG", //
                actBckPath, //
                actLogId ); //
      //
      callZipFile( zipBin, cpyFile ); //
    } //
    //
  } //

_door:

  logFuncExit( );

  return sysRc;
}

/******************************************************************************/
/*   C H E C K   I F   F I L E   I S   T R A N S A C T I O N A L   L O G      */

/******************************************************************************/
int mqCheckLogName( const char* log )
{
  logFuncCall( );

  char *p;
  int i;

  p = (char*) log;

  if( *p++ != 'S' ) return 1;
  for( i = 1; i < 8; i++ )
  {
    if( *p < '0' ) return i;
    if( *p > '9' ) return i;
    p++;
  }
  if( *p++ != '.' ) return 9;
  if( *p++ != 'L' ) return 10;
  if( *p++ != 'O' ) return 11;
  if( *p++ != 'G' ) return 12;

  logFuncExit( );

  return 0;
}

/******************************************************************************/
/*   M Q   C L O S E   A N D   D I S C O N N E C T                            */

/******************************************************************************/
MQLONG mqCloseDisconnect( MQHCONN Hcon, // connection handle   
                          PMQHOBJ Hqueue ) // queue handle   
{
  logFuncCall( );

  int sysRc;

  // ---------------------------------------------------
  // close queue
  // ---------------------------------------------------
  sysRc = mqCloseObject( Hcon, // connection handle
                         Hqueue ); // queue handle

  switch( sysRc )
  {
    case MQRC_NONE: break;
    default: goto _door;
  }

  // ---------------------------------------------------
  // disconnect from queue manager
  // ---------------------------------------------------
  sysRc = mqDisc( &Hcon ); // connection handle            
  switch( sysRc )
  {
    case MQRC_NONE: break;
    default: goto _door;
  }

_door:

  logFuncExit( );

  return sysRc;
}

/******************************************************************************/
/*   R E C O R D   M Q   I M A G E                                            */

/******************************************************************************/
int rcdMqImg( const char* _qmgr, const char* _instPath )
{
  logFuncCall( );

  char qmgrStr[MQ_Q_MGR_NAME_LENGTH + 1];

  int pid; // pid after fork
  int stdErr[2]; // pipe file descriptors std err
  char c; // char buffer for pipe
  char pipeBuff[PIPE_BUF]; // string buffer for pipe

  int i;
  int sysRc = 0;

  char rcdmqimgCmd[RCDMQIMG_CMD_LENGTH + 1];

  // -------------------------------------------------------
  // initialize full path record image command 
  // -------------------------------------------------------
  snprintf( rcdmqimgCmd, RCDMQIMG_CMD_LENGTH, "%s/bin/%s",
            _instPath,
            RCDMQIMG );

  // -------------------------------------------------------
  // fork process for rcdmqimg incl. pipes
  // -------------------------------------------------------
  sysRc = pipe( stdErr );
  if( sysRc < 0 )
  {
    logger( LSTD_OPEN_PIPE_FAILED );
    goto _door;
  }

  pid = (int) fork( );
  switch( pid )
  {
      // -----------------------------------------------------
      // fork failed: 
      //    do not quit, trigger has to be reactivated
      // -----------------------------------------------------
    case -1:
    {
      logger( LSTD_FORK_FAILED );
      sysRc = -1;
      goto _door;
    }

      // -----------------------------------------------------
      // child:
      //    map stderr to pipe
      //    close read end of the pipe, 
      //    exec rcdmqimg
      // -----------------------------------------------------
    case 0:
    {
      logger( LSTD_FORK_CHILD );
      // ---------------------------------------------------
      // handle file descriptors
      // ---------------------------------------------------
      dup2( stdErr[1], STDERR_FILENO );
      close( stdErr[0] );
      close( stdErr[1] );

      // ---------------------------------------------------
      // setup queue manager name
      // ---------------------------------------------------
      memcpy( qmgrStr, _qmgr, MQ_Q_MGR_NAME_LENGTH );
      for( i = 0; i < MQ_Q_MGR_NAME_LENGTH; i++ )
      {
        if( qmgrStr[i] == ' ' )
        {
          qmgrStr[i] = '\0';
          break;
        }
      }

      // ---------------------------------------------------
      // exec
      // ---------------------------------------------------
      execl( rcdmqimgCmd, RCDMQIMG, "-m", qmgrStr,
             "-t", "all",
             "*", NULL );

      // -------------------------------------------------
      // there is no chance to get that far 
      // -------------------------------------------------
      logger( LSTD_GEN_CRI, "rcdmqimg" );

      exit( 0 );
    }

      // -----------------------------------------------------
      // parent:
      //    close write end of the pipe
      //    read from the pipe and write to log
      // -----------------------------------------------------
    default:
    {
      logger( LSTD_FORK_PARENT );
      // ---------------------------------------------------
      // setup queue manager name
      // ---------------------------------------------------
      dup2( stdErr[0], STDIN_FILENO );
      close( stdErr[0] );
      close( stdErr[1] );

      // ---------------------------------------------------
      // redirect output from child (RCDMQIMG) to log file
      // ---------------------------------------------------
      logger( LSYS_MULTILINE_START, "RCDMQIMG" );

      i = 0;
      while( 1 )                              // read for ever
      {                               // 
        sysRc = read( STDIN_FILENO, &c, 1 ); // read from the pipe
        if( (int) sysRc == 0 ) break; // stop if EOF (will work only 
        pipeBuff[i] = c;       //       on nonblocked device)
        if( c == '\n' )             // if eol
        {                               //
          pipeBuff[i] = '\0'; // replace '\n' by '\0' 
          i = 0;                     // start new line
          logger( LSYS_MULTILINE_ADD, pipeBuff );
          continue;         // write full line to log
        }                     //
        i++;                     //
      }                     //        
      if( c != '\n' )     // if last line is not ended by '\n'
      {                           // 
        pipeBuff[i] = '\0'; // write it to log file
        logger( LSYS_MULTILINE_ADD, pipeBuff );
      }                          //
      logger( LSYS_MULTILINE_END, "RCDMQIMG" );

      // ---------------------------------------------------
      // allow child to free (disable zombi)
      // ---------------------------------------------------
      waitpid( pid, &sysRc, WNOHANG );
      sysRc >>= 8;
      sysRc &= 127;
      if( sysRc == 0 )
      {
        logger( LSTD_CHILD_ENDED_OK, "rcdmqimg" );
      }
      else
      {
        logger( LSTD_CHILD_ENDED_ERR, "rcdmqimg", sysRc );
        goto _door;
      }
      break;
    }
  }

_door:
  return sysRc;
}

#if(0)
/******************************************************************************/
/*   B A C K U P   T R A N S A C T I O N A L   L O G S                     */

/******************************************************************************/
int mqBackupLog( const char* logPath, // original log path
                 const char* bckPath, // backup log path
                 const char* oldLog ) // oldest log name
{
  logFuncCall( );

  char origFile[255];
  char copyFile[255];

  int oldLogId = mqLogName2Id( oldLog );
  int curLogId = mqLogName2Id( currLog );
  int logId;

  time_t curTime;
  struct tm *localTime;
  char timeStr[32];

  int rc;

  // -------------------------------------------------------
  // get time for mkdir & mkdir
  // -------------------------------------------------------
  curTime = time( NULL );
  localTime = localtime( &curTime );
  strftime( timeStr, 32, "%Y%m%d_%H%M%S", localTime );

  sprintf( copyFile, "/%s/%s", bckPath, timeStr );
  rc = mkdir( copyFile, S_IRUSR | S_IWUSR | S_IXUSR |
              S_IRGRP | S_IWGRP | S_IXGRP );
  if( rc < 0 )
  {
    logger( LSTD_MAKE_DIR_FAILED, copyFile );
    return 1;
  }

  // -------------------------------------------------------
  // copy all relevant files
  // -------------------------------------------------------
  for( logId = oldLogId; logId < curLogId; logId++ )
  {
    sprintf( origFile, "/%s/S%07d.LOG", logPath, logId );
    sprintf( copyFile, "/%s/%s/S%07d.LOG", bckPath, timeStr, logId );

    logger( LSTD_FILE_COPIED, origFile, copyFile );

    rc = mqCopyLog( origFile, copyFile );
    if( rc != 0 ) return rc;
  }
  return 0;
}
#endif

/******************************************************************************/
/*   C O P Y   T R A N S A C T I O N A L   L O G                              */

/******************************************************************************/
int mqCopyLog( const char* orgFile, const char* cpyFile )
{
  logFuncCall( );

  int sysRc = 0;

  char copyBuff[COPY_BUFF_LNG];

  int orgFd;
  int cpyFd;

  int eof = 0;

  int rc;

  // -------------------------------------------------------
  // open original file for reading
  // -------------------------------------------------------
  if( (orgFd = open( orgFile, O_RDONLY )) == -1 )
  {
    logger( LSTD_OPEN_FILE_FAILED, orgFile );
    logger( LSTD_ERRNO_ERR, errno, strerror( errno ) );
    sysRc = errno;
    goto _door;
  }

  // -------------------------------------------------------
  // open backup file for writing
  // -------------------------------------------------------
  if( (cpyFd = open( cpyFile, O_WRONLY | O_CREAT,
                     S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP )) == -1 )
  {
    logger( LSTD_OPEN_FILE_FAILED, cpyFile );
    logger( LSTD_ERRNO_ERR, errno, strerror( errno ) );
    sysRc = errno;
    goto _door;
  }

  // -------------------------------------------------------
  // copy file in 4k blocks, since page size is 4k
  // -------------------------------------------------------
  eof = 0;
  while( !eof )
  {
    rc = read( orgFd, copyBuff, COPY_BUFF_LNG );
    switch( rc )
    {
      case COPY_BUFF_LNG:
      {
        write( cpyFd, copyBuff, COPY_BUFF_LNG );
        break;
      }
      case 0:
      {
        close( orgFd );
        close( cpyFd );
        sysRc = 0;
        eof = 1;
        break;
      }
      default:
      {
        logger( LSTD_ERR_READING_FILE, orgFile );
        write( cpyFd, copyBuff, rc );
        close( orgFd );
        close( cpyFd );
        sysRc = 1;
        goto _door;
      }
    }
  }

_door:

  logFuncExit( );

  return sysRc;
}

/******************************************************************************/
/*   C O P Y   F I L E                                                        */
/*                                                                            */
/*   description:                                                             */
/*     copy file kByte by kByte (binary copy)                                 */
/*                                                                            */

/******************************************************************************/
int copyFile( const char *orig, const char *goal )
{
  logFuncCall( );

  int sysRc = 0;

  FILE *origFp = NULL;
  FILE *copyFp = NULL;

#define CHUNK_LENGTH 1024
  size_t bytesRead;
  size_t bytesWrite;
  unsigned char buffer[1024];

  origFp = fopen( orig, "r" );
  if( origFp == NULL )
  {
    logger( LSTD_OPEN_FILE_FAILED, orig );
    logger( LSTD_ERRNO_ERR, errno, strerror( errno ) );
    sysRc = errno;
    goto _door;
  }

  copyFp = fopen( goal, "w" );
  if( copyFp == NULL )
  {
    logger( LSTD_OPEN_FILE_FAILED, goal );
    logger( LSTD_ERRNO_ERR, errno, strerror( errno ) );
    sysRc = errno;
    goto _door;
  }

  while( (bytesRead = fread( buffer, 1, sizeof (buffer), origFp )) > 0 )
  {
    bytesWrite = fwrite( buffer, 1, bytesRead, copyFp );
    if( bytesWrite < bytesRead )
    {
      logger( LSTD_ERR_WRITING_FILE, goal );
      logger( LSTD_ERRNO_ERR, errno, strerror( errno ) );
      sysRc = errno;
      goto _door;
    }
  }

_door:

  if( origFp ) fclose( origFp );
  if( copyFp ) fclose( copyFp );

  logFuncExit( );

  return sysRc;
}

/******************************************************************************/
/*   B A C K U P   Q M   I N I                                                */

/******************************************************************************/
int copyQmIni( const char *mqDataPath, const char *bckPath )
{
  logFuncCall( );

  int sysRc = 0;

  char qmIniOrig[PATH_MAX];
  char qmIniGoal[PATH_MAX];

  sysRc = mkdirRecursive( bckPath, 0775 );
  if( sysRc != 0 )
  {
    logger( LSTD_MAKE_DIR_FAILED, bckPath );
    logger( LSTD_ERRNO_ERR, sysRc, strerror( sysRc ) );
    goto _door;
  }

  snprintf( qmIniOrig, PATH_MAX, "%s/qm.ini", mqDataPath );
  snprintf( qmIniGoal, PATH_MAX, "%s/qm.ini", bckPath );

  sysRc = copyFile( qmIniOrig, qmIniGoal );

_door:

  logFuncExit( );

  return sysRc;
}

/******************************************************************************/
/*   B A C K U P   S S L   R E P O S I T O R Y                                */

/******************************************************************************/
int copySslRepos( const char *_sslDir, // path to SSL Repository 
                  const char *_SslBase, // Repository file name without suffix
                  const char *_bckDir ) // Backup directory
{
  logFuncCall( );

  int sysRc = 0;

  char sslFile[PATH_MAX]; // real name of original SSL file
  char bckFile[PATH_MAX]; // real name of backup SSL file

  DIR *sslDp;
  struct dirent *sslDirEntry;

  // -------------------------------------------------------
  // create SSL backup directory
  // -------------------------------------------------------
  sysRc = mkdirRecursive( _bckDir, 0775 );
  if( sysRc != 0 )
  {
    logger( LSTD_MAKE_DIR_FAILED, _bckDir );
    logger( LSTD_ERRNO_ERR, sysRc, strerror( sysRc ) );
    goto _door;
  }

  // -------------------------------------------------------
  // open original SSL directory
  // -------------------------------------------------------
  sslDp = opendir( _sslDir );
  if( sslDp == NULL )
  {
    logger( LSTD_OPEN_DIR_FAILED, _sslDir );
    logger( LSTD_ERRNO_ERR, errno, strerror( errno ) );
    sysRc = errno;
    goto _door;
  }

  // -------------------------------------------------------
  // got through original SSL directory
  // -------------------------------------------------------
  while( NULL != (sslDirEntry = readdir( sslDp )) ) // get each file
  {                                 //
    if( sslDirEntry->d_type != DT_REG ) // skip directories
    {                         //
      continue;                 //
    }                               //
                                  //
    if( strncmp( sslDirEntry->d_name, // ignore all files that do 
                 _SslBase, //  not match key.*
                 strlen( _SslBase ) ) != 0 ) //
    {                   //
      continue;           // get source and goal
    }                     //  file names
    snprintf( sslFile, PATH_MAX, "%s/%s", _sslDir, (char*) sslDirEntry->d_name );
    snprintf( bckFile, PATH_MAX, "%s/%s", _bckDir, (char*) sslDirEntry->d_name );
    sysRc = copyFile( sslFile, bckFile ); // copy file
  }                       //
                            //

_door:

  logFuncExit( );

  return sysRc;
}

/******************************************************************************/
/*   C O P Y   O B J E C T   C A T A L O G                                    */

/******************************************************************************/
int copyCatalog( const char *mqDataPath, const char *bckPath )
{
  logFuncCall( );

  char orgCtrlFile[PATH_MAX];
  char bckCtrlFile[PATH_MAX];

  int sysRc;

  sysRc = mkdirRecursive( bckPath, 0775 );

  snprintf( orgCtrlFile, PATH_MAX, "%s/qmanager/%s", mqDataPath, CATALOG_FILE );
  snprintf( bckCtrlFile, PATH_MAX, "%s/%s", bckPath, CATALOG_FILE );

  sysRc = copyFile( orgCtrlFile, bckCtrlFile );

  logFuncExit( );

  return sysRc;
}

/******************************************************************************/
/*   C A L L   Z I P   F I L E                                                */

/******************************************************************************/
int callZipFile( const char* zipBin, const char* file )
{
  logFuncCall( );

  int sysRc = 0;

  char *argv[3];

  argv[0] = (char*) zipBin;
  argv[1] = (char*) file;
  argv[2] = NULL;

  sysRc = startChild( zipBin, NULL, NULL, argv ) != 0;
  switch( sysRc )
  {
    case 0:
      logger( LSTD_ZIPPED, file );
      break;
    default:
      logger( LSTD_ZIP_FAILED, file );
      goto _door;
  }


_door:

  logFuncExit( );

  return sysRc;
}

/******************************************************************************/
/*   C O N S T R U C T O R   :   Q U E U E   M A N A G E R   O B J E C T     */

/******************************************************************************/
tQmgrObj* createQmgrObject( )
{
  logFuncCall( );

  tQmgrObj *qmgrObj = NULL;

  qmgrObj = (tQmgrObj*) malloc( sizeof (tQmgrObj) );

  if( qmgrObj == NULL )
  {
    logger( LSTD_MEM_ALLOC_ERROR );
    goto _door;
  }

  qmgrObj->compCode = MQCC_OK;
  qmgrObj->reason = MQRC_NONE;

  memset( qmgrObj->logPath, MQ_LOG_PATH_LENGTH + 1, 0 );
  memset( qmgrObj->instPath, MQ_INSTALLATION_PATH_LENGTH + 1, 0 );
  memset( qmgrObj->sslPath, MQ_SSL_KEY_LIBRARY_LENGTH + 1, 0 );
  memset( qmgrObj->dataPath, MQ_SSL_KEY_REPOSITORY_LENGTH + 1, 0 );
  memset( qmgrObj->sslRep, MQ_SSL_KEY_REPOSITORY_LENGTH + 1, 0 );

_door:
  logFuncExit( );

  return qmgrObj;
}

/******************************************************************************/
/*   C O N S T R U C T O R   :   Q U E U E   M A N A G E R   O B J E C T     */

/******************************************************************************/
void deleteQmgrObject( tQmgrObj *qmgrObj )
{
  logFuncCall( );

  if( qmgrObj == NULL ) goto _door;

  free( qmgrObj );

_door:

  logFuncExit( );

  return;
}


/******************************************************************************/
/*   G E T   Q U E U E   M A N A G E R   O B J E C T   S T A T U S            */

/******************************************************************************/
MQLONG getQmgrStatus( MQHCONN Hconn, tQmgrObj* pQmgrObjStatus )
{
  logFuncCall( );

  MQLONG mqrc = MQRC_NONE;

  MQHBAG cmdBag = MQHB_UNUSABLE_HBAG;
  MQHBAG respBag = MQHB_UNUSABLE_HBAG;
  MQHBAG attrBag;

  MQLONG parentItemCount;
  MQLONG parentItemType;
  MQLONG parentSelector;

  MQLONG childItemCount;
  MQLONG childItemType;
  MQLONG childSelector;

  MQINT32 selInt32Val;
  MQCHAR selStrVal[ITEM_LENGTH];

  MQLONG selStrLng;

  char sBuffer[ITEM_LENGTH + 1];

  int i;
  int j;

  // -------------------------------------------------------
  // open bags for MQ Execute
  // -------------------------------------------------------
  mqrc = mqOpenAdminBag( &cmdBag );
  switch( mqrc )
  {
    case MQRC_NONE: break;
    default: goto _door;
  }

  mqrc = mqOpenAdminBag( &respBag );
  switch( mqrc )
  {
    case MQRC_NONE: break;
    default: goto _door;
  }

  // -------------------------------------------------------
  // DISPLAY QMSTATUS ALL 
  //   process command in two steps
  //   1. setup the list of arguments MQIACF_ALL = ALL
  //   2. send a command MQCMD_INQUIRE_Q_MGR_STATUS = DISPLAY QMSTATUS
  // -------------------------------------------------------
  mqrc = mqSetInqAttr( cmdBag, MQIACF_ALL ); // set attribute 
  // to PCF command
  switch( mqrc ) // DISPLAY QMSTATUS ALL
  { //
    case MQRC_NONE: break; //
    default: goto _door; //
  } //
  //
  mqrc = mqExecPcf( Hconn, // send a command to 
                    MQCMD_INQUIRE_Q_MGR_STATUS, //  the command queue
                    cmdBag, //
                    respBag ); //
  //
  switch( mqrc ) //
  { //
    case MQRC_NONE: break; //
    default: // mqExecPcf includes  
    { // evaluating mqErrBag,
      pQmgrObjStatus->reason = (MQLONG) selInt32Val; // additional evaluating of 
      goto _door; // MQIASY_REASON is therefor
    } // not necessary
  } //
  //
  // ---------------------------------------------------------
  // count the items in response bag
  // -------------------------------------------------------
  mqrc = mqBagCountItem( respBag, // get the amount of items
                         MQSEL_ALL_SELECTORS ); //  for all selectors
  //
  if( mqrc > 0 ) // 
  { //
    goto _door; //
  } //
  else // if reason code is less 
  { //  then 0 then it is not 
    parentItemCount = -mqrc; //  a real reason code it's  
    mqrc = MQRC_NONE; //  the an item counter
  } //
  //
  // ---------------------------------------------------------
  // go through all items
  //  there are two loops, first one over parent items
  //  and the second one for child items
  //  child items are included in a internal (child) bag in one of parents item
  // ---------------------------------------------------------
  for( i = 0; i < parentItemCount; i++ ) // analyze all items
  { //
    mqrc = mqItemInfoInq( respBag, // find out the item type
                          MQSEL_ANY_SELECTOR, //
                          i, //
                          &parentSelector, //
                          &parentItemType ); //
    //
    switch( mqrc ) //
    { //
      case MQRC_NONE: break; //
      default: goto _door; //
    } //
    //
#ifdef  _LOGTERM_                                  //
    char* pBuffer; //
    printf( "%2d selector: %04d %-30.30s type %10.10s",
            i, //
            parentSelector, //
            mqSelector2str( parentSelector ), //
            mqItemType2str( parentItemType ) ); //
#endif                                             //
    //
    // -------------------------------------------------------
    // for each item:
    //    - get the item type
    //    - analyze selector depending on the type
    // -------------------------------------------------------
    switch( parentItemType ) //
    { //
        // -----------------------------------------------------
        // Parent Bag:
        // TYPE: 32 bit integer -> in this function only system 
        //       items will be needed, f.e. compilation and reason code 
        // -----------------------------------------------------
      case MQITEM_INTEGER: // in this program only 
      { //  system selectors will 
        mqrc = mqIntInq( respBag, //  be expected
                         MQSEL_ANY_SELECTOR, // out of system selectors 
                         i, //  only complition and 
                         &selInt32Val ); //  reason code are important
        switch( mqrc ) //  all other will be ignored  
        { //
          case MQRC_NONE: break; // analyze InquireInteger
          default: goto _door; //  reason code
        } //
        //
#ifdef _LOGTERM_                                   //
        pBuffer = (char*) itemValue2str( parentSelector,
                                         (MQLONG) selInt32Val );
        if( pBuffer ) //
        { //
          printf( " value %s\n", pBuffer ); //
        } //
        else //
        { //
          printf( " value %d\n", (int) selInt32Val ); //
        } //
#endif                                             //
        // ---------------------------------------------------
        // Parent Bag: 
        // TYPE: 32 bit integer; analyze selector
        // ---------------------------------------------------
        switch( parentSelector ) // all 32 bit integer 
        { //  selectors are system
          case MQIASY_COMP_CODE: //  selectors
          { //
            pQmgrObjStatus->compCode = (MQLONG) selInt32Val;
            break; // only mqExec completion 
          } //  code and reason code are 
          case MQIASY_REASON: //  interesting for later use
          { // 
            pQmgrObjStatus->reason = (MQLONG) selInt32Val;
            break; //
          } //
          default: //
          { // all other selectors can
            break; //  be ignored
          } //
        } //
        break; //
      } //
        //
        // -----------------------------------------------------
        // Parent Type:
        // TYPE: Bag -> Bag in Bag 
        //       cascaded bag contains real data 
        //       like Installation and Log Path 
        // -----------------------------------------------------
      case MQITEM_BAG: //
      { //
#ifdef  _LOGTERM_                                  //
        printf( "\n======================================================\n" );
#endif                                             //
        mqrc = mqBagInq( respBag, 0, &attrBag ); // usable data are located 
        switch( mqrc ) //  in cascaded (child) bag
        { // use only:
          case MQRC_NONE: break; //  - installation path
          default: goto _door; //  - log path
        } //
        //
        // ---------------------------------------------------
        // count the items in the child bag
        // ---------------------------------------------------
        mqrc = mqBagCountItem( attrBag, // get the amount of items
                               MQSEL_ALL_SELECTORS ); //  for all selectors in 
        if( mqrc > 0 ) //  child bag
        { //
          goto _door; //
        } //
        else //
        { //
          childItemCount = -mqrc; //
          mqrc = MQRC_NONE; //
        } //
        //
        // ---------------------------------------------------
        // go through all child items
        //  this is the internal loop
        // ---------------------------------------------------
        for( j = 0; j < childItemCount; j++ ) //
        { //
          mqrc = mqItemInfoInq( attrBag, //
                                MQSEL_ANY_SELECTOR, //
                                j, //
                                &childSelector, //
                                &childItemType ); //
          switch( mqrc ) //
          { //
            case MQRC_NONE: break; //
            default: goto _door; //
          } //
          //
#ifdef    _LOGTERM_                                //
          printf( "   %2d selector: %04d %-30.30s type %10.10s",
                  j, //
                  childSelector, //
                  mqSelector2str( childSelector ), //
                  mqItemType2str( childItemType ) ); //
#endif                                             //
          //
          // -------------------------------------------------
          // CHILD ITEM
          //   analyze each child item / selector
          // -------------------------------------------------
          switch( childItemType ) // main switch in 
          { //  internal loop
              // -----------------------------------------------
              // CHILD ITEM
              // TYPE: 32 bit integer
              // -----------------------------------------------
            case MQITEM_INTEGER: // not a single integer 
            { //  can be used in this 
              mqrc = mqIntInq( attrBag, //  program.
                               MQSEL_ANY_SELECTOR, // so the the selInt32Val
                               j, //  does not have to be 
                               &selInt32Val ); //  evaluated
              switch( mqrc ) //
              { //
                case MQRC_NONE: break; //
                default: goto _door; //
              } //
              //
#ifdef        _LOGTERM_                            //
              pBuffer = (char*) itemValue2str( childSelector,
                                               (MQLONG) selInt32Val );
              if( pBuffer ) //
              { //
                printf( " value %s\n", pBuffer ); //
              } //
              else //
              { //
                printf( " value %d\n", (int) selInt32Val );
              } //
#endif
              break; // --- internal loop over child items
            } // --- Item Type Integer

              // -----------------------------------------------
              // CHILD ITEM
              // TYPE: string
              // -----------------------------------------------
            case MQITEM_STRING: // installation path
            { //  and log path have
              mqrc = mqStrInq( attrBag, //  type STRING
                               MQSEL_ANY_SELECTOR, //
                               j, //  
                               ITEM_LENGTH, //
                               selStrVal, //
                               &selStrLng ); //
              switch( mqrc ) //
              { //
                case MQRC_NONE: break; //
                default: goto _door; //
              } //
              //
              mqrc = mqTrimStr( ITEM_LENGTH, // trim string 
                                selStrVal, //
                                sBuffer ); //
              switch( mqrc ) //
              { //
                case MQRC_NONE: break; //
                default: goto _door; //
              } //
              //
              // ---------------------------------------------
              // CHILD ITEM
              // TYPE: string
              // analyze selector
              // ---------------------------------------------
              switch( childSelector ) // 
              { //
                case MQCA_INSTALLATION_PATH: //
                { //
                  strcpy( pQmgrObjStatus->instPath, sBuffer );
                  break; //
                } //
                case MQCACF_LOG_PATH: //
                { //
                  strcpy( pQmgrObjStatus->logPath, sBuffer );
                  break; //
                } //
                default: //
                { //
                  break; //
                } // --- internal loop over child items
              } // --- analyze selector of type string
#ifdef        _LOGTERM_ 
              printf( " value %s\n", sBuffer );
#endif                                            
              break; // --- internal loop over child items    
            } // --- Item Type String

              // -----------------------------------------------
              // any other item type for child bag is an error
              // -----------------------------------------------
            default: //
            { //
              goto _door; //
            } // - switch child item type --- default 
          } // - switch child item type         
        } // for each child item type
        break; // --- switch parent item type
      } // --- case MQ item bag     

        // -----------------------------------------------------
        // all other parent item types are not expected
        // -----------------------------------------------------
      default: //
      { //
        goto _door; //
      } // --- switch parent item type --- default 
    } // --- switch parent item type     
  } // --- for each parent item       


_door:

  mqCloseBag( &cmdBag );
  mqCloseBag( &respBag );

  if( pQmgrObjStatus->instPath[0] == '\0' )
  {
    strcpy( pQmgrObjStatus->instPath, MQ_DEFAULT_INSTALLATION_PATH );
  }

#ifdef _LOGTERM_
  printf( "\n" );
#endif 

  logFuncExit( );
  return mqrc;
}

/******************************************************************************/
/*   G E T   Q U E U E   M A N A G E R   O B J E C T   S T A T U S            */

/******************************************************************************/
MQLONG getQmgrObject( MQHCONN Hconn, tQmgrObj* pQmgrObj )
{
  logFuncCall( );

  MQLONG mqrc = MQRC_NONE;

  MQHBAG cmdBag = MQHB_UNUSABLE_HBAG;
  MQHBAG respBag = MQHB_UNUSABLE_HBAG;
  MQHBAG attrBag;

  MQLONG parentItemCount;
  MQLONG parentItemType;
  MQLONG parentSelector;

  MQLONG childItemCount;
  MQLONG childItemType;
  MQLONG childSelector;

  MQINT32 selInt32Val;
  MQCHAR selStrVal[ITEM_LENGTH];
  MQLONG selStrLng;

  char sBuffer[ITEM_LENGTH + 1];

  int i;
  int j;

  // -------------------------------------------------------
  // open bags for MQ Execute
  // -------------------------------------------------------
  mqrc = mqOpenAdminBag( &cmdBag );
  switch( mqrc )
  {
    case MQRC_NONE: break;
    default: goto _door;
  }

  mqrc = mqOpenAdminBag( &respBag );
  switch( mqrc )
  {
    case MQRC_NONE: break;
    default: goto _door;
  }

  // -------------------------------------------------------
  // DISPLAY QMGR ALL 
  //   process command in two steps
  //   1. setup the list of arguments MQIACF_ALL = ALL
  //   2. send a command MQCMD_INQUIRE_Q_MGR = DISPLAY QMGR ALL
  // -------------------------------------------------------
  mqrc = mqSetInqAttr( cmdBag, MQIACF_ALL ); // set attribute 
  // to PCF command
  switch( mqrc ) // DISPLAY QMGR ALL
  { //
    case MQRC_NONE: break; //
    default: goto _door; //
  }                   //
        //
  mqrc = mqExecPcf( Hconn, // send a command to 
                    MQCMD_INQUIRE_Q_MGR, //  the command queue
                    cmdBag, //
                    respBag ); //
  //
  switch( mqrc ) //
  { //
    case MQRC_NONE: break; // 
    default: // evaluating mqErrBag,
    { // mqExecPcf includes 
      pQmgrObj->reason = (MQLONG) selInt32Val; // additional evaluating of 
      goto _door; // MQIASY_REASON is therefor
    } // not necessary
  } //
  //
  // ---------------------------------------------------------
  // count the items in response bag
  // -------------------------------------------------------
  mqrc = mqBagCountItem( respBag, // get the amount of items
                         MQSEL_ALL_SELECTORS ); //  for all selectors
  //
  if( mqrc > 0 ) // 
  { //
    goto _door; //
  } //
  else // if reason code is less 
  { //  then 0 then it is not 
    parentItemCount = -mqrc; //  a real reason code it's  
    mqrc = MQRC_NONE; //  the an item counter
  } //
  //
  // ---------------------------------------------------------
  // go through all items
  //  there are two loops, first one over parent items
  //  and the second one for child items
  //  child items are included in a internal (child) bag in one of parents item
  // ---------------------------------------------------------
  for( i = 0; i < parentItemCount; i++ ) // analyze all items
  { //
    mqrc = mqItemInfoInq( respBag, // find out the item type
                          MQSEL_ANY_SELECTOR, //
                          i, //
                          &parentSelector, //
                          &parentItemType ); //
    //
    switch( mqrc ) //
    { //
      case MQRC_NONE: break; //
      default: goto _door; //
    } //
    //
#ifdef  _LOGTERM_                                  //
    char* pBuffer; //
    printf( "%2d selector: %04d %-30.30s type %10.10s",
            i, //
            parentSelector, //
            mqSelector2str( parentSelector ), //
            mqItemType2str( parentItemType ) ); //
#endif                                             //
    //
    // -------------------------------------------------------
    // for each item:
    //    - get the item type
    //    - analyze selector depending on the type
    // -------------------------------------------------------
    switch( parentItemType ) //
    { //
        // -----------------------------------------------------
        // Parent Bag:
        // TYPE: 32 bit integer -> in this function only system 
        //       items will be needed, f.e. compilation and reason code 
        // -----------------------------------------------------
      case MQITEM_INTEGER: // in this program only 
      { //  system selectors will 
        mqrc = mqIntInq( respBag, //  be expected
                         MQSEL_ANY_SELECTOR, // out of system selectors 
                         i, //  only complition and 
                         &selInt32Val ); //  reason code are important
        switch( mqrc ) //  all other will be ignored  
        { //
          case MQRC_NONE: break; // analyze InquireInteger
          default: goto _door; //  reason code
        } //
        //
#ifdef _LOGTERM_                                   //
        pBuffer = (char*) itemValue2str( parentSelector,
                                         (MQLONG) selInt32Val );
        if( pBuffer ) //
        { //
          printf( " value %s\n", pBuffer ); //
        } //
        else //
        { //
          printf( " value %d\n", (int) selInt32Val ); //
        } //
#endif                                             //
        // ---------------------------------------------------
        // Parent Bag: 
        // TYPE: 32 bit integer; analyze selector
        // ---------------------------------------------------
        switch( parentSelector ) // all 32 bit integer 
        { //  selectors are system
          case MQIASY_COMP_CODE: //  selectors
          { //
            pQmgrObj->compCode = (MQLONG) selInt32Val; //
            break; // only mqExec completion 
          } //  code and reason code are 
          case MQIASY_REASON: //  interesting for later use
          { // 
            pQmgrObj->reason = (MQLONG) selInt32Val; //
            break; //
          } //
          default: //
          { // all other selectors can
            break; //  be ignored
          } //
        } //
        break; //
      } //
        //
        // -----------------------------------------------------
        // Parent Type:
        // TYPE: Bag -> Bag in Bag 
        //       cascaded bag contains real data 
        //       like Installation and Log Path 
        // -----------------------------------------------------
      case MQITEM_BAG: //
      { //
#ifdef  _LOGTERM_                                  //
        printf( "\n======================================================\n" );
#endif                                             //
        mqrc = mqBagInq( respBag, 0, &attrBag ); // usable data are located 
        switch( mqrc ) //  in cascaded (child) bag
        { // use only:
          case MQRC_NONE: break; //  - installation path
          default: goto _door; //  - log path
        } //
        //
        // ---------------------------------------------------
        // count the items in the child bag
        // ---------------------------------------------------
        mqrc = mqBagCountItem( attrBag, // get the amount of items
                               MQSEL_ALL_SELECTORS ); //  for all selectors in 
        if( mqrc > 0 ) //  child bag
        { //
          goto _door; //
        } //
        else //
        { //
          childItemCount = -mqrc; //
          mqrc = MQRC_NONE; //
        } //
        //
        // ---------------------------------------------------
        // go through all child items
        //  this is the internal loop
        // ---------------------------------------------------
        for( j = 0; j < childItemCount; j++ ) //
        { //
          mqrc = mqItemInfoInq( attrBag, //
                                MQSEL_ANY_SELECTOR, //
                                j, //
                                &childSelector, //
                                &childItemType ); //
          switch( mqrc ) //
          { //
            case MQRC_NONE: break; //
            default: goto _door; //
          } //
          //
#ifdef    _LOGTERM_                                //
          printf( "   %2d selector: %04d %-30.30s type %10.10s",
                  j, //
                  childSelector, //
                  mqSelector2str( childSelector ), //
                  mqItemType2str( childItemType ) ); //
#endif                                             //
          //
          // -------------------------------------------------
          // CHILD ITEM
          //   analyze each child item / selector
          // -------------------------------------------------
          switch( childItemType ) // main switch in 
          { //  internal loop
              // -----------------------------------------------
              // CHILD ITEM
              // TYPE: 32 bit integer
              // -----------------------------------------------
            case MQITEM_INTEGER: // not a single integer 
            { //  can be used in this 
              mqrc = mqIntInq( attrBag, //  program.
                               MQSEL_ANY_SELECTOR, // so the the selInt32Val
                               j, //  does not have to be 
                               &selInt32Val ); //  evaluated
              switch( mqrc ) //
              { //
                case MQRC_NONE: break; //
                default: goto _door; //
              } //
              //
#ifdef        _LOGTERM_                            //
              pBuffer = (char*) itemValue2str( childSelector,
                                               (MQLONG) selInt32Val );
              if( pBuffer ) //
              { //
                printf( " value %s\n", pBuffer ); //
              } //
              else //
              { //
                printf( " value %d\n", (int) selInt32Val );
              } //
#endif
              break; // --- internal loop over child items
            } // --- Item Type Integer

              // -----------------------------------------------
              // CHILD ITEM
              // TYPE: string
              // -----------------------------------------------
            case MQITEM_STRING: // installation path
            { //  and log path have
              mqrc = mqStrInq( attrBag, //  type STRING
                               MQSEL_ANY_SELECTOR, //
                               j, //  
                               ITEM_LENGTH, //
                               selStrVal, //
                               &selStrLng ); //
              switch( mqrc ) //
              { //
                case MQRC_NONE: break; //
                default: goto _door; //
              } //
              //
              mqrc = mqTrimStr( ITEM_LENGTH, // trim string 
                                selStrVal, //
                                sBuffer ); //
              switch( mqrc ) //
              { //
                case MQRC_NONE: break; //
                default: goto _door; //
              } //
              //
              // ---------------------------------------------
              // CHILD ITEM
              // TYPE: string
              // analyze selector
              // ---------------------------------------------
              switch( childSelector ) // 
              { //
                case MQCA_SSL_KEY_REPOSITORY: //
                { //
                  strcpy( pQmgrObj->sslPath, sBuffer );
                  break; //
                } //
                default: //
                { //
                  break; //
                } // --- internal loop over child items
              } // --- analyze selector of type string
#ifdef        _LOGTERM_ 
              printf( " value %s\n", sBuffer );
#endif                                            
              break; // --- internal loop over child items    
            } // --- Item Type String

              // -----------------------------------------------
              // any other item type for child bag is an error
              // -----------------------------------------------
            default: //
            { //
              goto _door; //
            } // - switch child item type --- default 
          } // - switch child item type         
        } // for each child item type
        break; // --- switch parent item type
      } // --- case MQ item bag     

        // -----------------------------------------------------
        // all other parent item types are not expected
        // -----------------------------------------------------
      default: //
      { //
        goto _door; //
      } // --- switch parent item type --- default 
    } // --- switch parent item type     
  } // --- for each parent item       


_door:

  mqCloseBag( &cmdBag );
  mqCloseBag( &respBag );


#ifdef _LOGTERM_
  printf( "\n" );
#endif 

  logFuncExit( );
  return mqrc;
}
