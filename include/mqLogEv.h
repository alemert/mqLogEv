#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <cmqc.h>
#include <cmqcfc.h>

//#include "msgCat.h"
//#include "mqLogUtil.h"

#include "mqbase.h"
#include "mqreason.h"
#include <mqtype.h>

#ifdef C_MODULE_MQLOGEV
const char progname[] = "mqLogEv" ;
#else
extern const char progname[] ;
#endif

/******************************************************************************/
/*   P R O T O T Y P E S                           */
/******************************************************************************/
int cleanupLog( const char* qmgrName,  
                const char* qName  );
