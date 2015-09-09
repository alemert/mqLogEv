
/******************************************************************************/
/*   I N C L U D E S                                                          */
/******************************************************************************/

// ---------------------------------------------------------
// system
// ---------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

// ---------------------------------------------------------
// MQ
// ---------------------------------------------------------
#include <cmqc.h>
#include <cmqcfc.h>

// ---------------------------------------------------------
// own 
// ---------------------------------------------------------
#include "mqbase.h"
#include "mqreason.h"
#include <mqtype.h>

/******************************************************************************/
/*   G L O B A L S                                                            */
/******************************************************************************/
#ifdef C_MODULE_MQLOGEV
const char progname[] = "mqLogEv" ;
#else
extern const char progname[] ;
#endif

/******************************************************************************/
/*   D E F I N E S                                                            */
/******************************************************************************/
#define LOGGER_QUEUE "SYSTEM.ADMIN.LOGGER.EVENT"

/******************************************************************************/
/*   S T R U C T                                                              */
/******************************************************************************/
struct sQmgrObj
{
  MQLONG compCode ;
  MQLONG reason   ;
  MQCHAR logPath [MQ_LOG_PATH_LENGTH+1]         ; // transactional log path
  MQCHAR instPath[MQ_INSTALLATION_PATH_LENGTH+1]; // installation path
  MQCHAR sslPath[MQ_SSL_KEY_REPOSITORY_LENGTH+1]; // path to SSL repository
  MQCHAR dataPath[MQ_SSL_KEY_REPOSITORY_LENGTH+1]; // path to QM.INI 
};

/******************************************************************************/
/*   T Y P E S                                                                */
/******************************************************************************/
typedef struct sQmgrObj tQmgrObj;

/******************************************************************************/
/*   P R O T O T Y P E S                                                      */
/******************************************************************************/
int cleanupLog( const char* qmgrName,  // queue manager name
                const char* qName   ,  // logger event name
                const char* bckPath ,  // backup path 
                const char* zipBin );  // zip binary
