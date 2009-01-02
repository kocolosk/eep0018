#ifndef EEP0018_H
#define EEP0018_H

/* commands */

#define EEP0018_JSON_PARSE              1
#define EEP0018_JSON_PARSE_EI           2
#define EEP0018_JSON_PARSE_VALUE        3
#define EEP0018_JSON_PARSE_VALUE_EI     4

/* simple types */
/* each simple type is followed by the actual data in a string buffer */

#define EEP0018_ATOM     10
#define EEP0018_NUMBER   11
#define EEP0018_STRING   12
#define EEP0018_KEY      13

/* complex types */

#define EEP0018_MAP      14
#define EEP0018_ARRAY    15
#define EEP0018_END      16

#define EEP0018_EI       17

/* project includes */

#include "log.h"    
#include <assert.h>    

/* YAJL includes and settings */

#include <yajl/yajl_parse.h>
#include <yajl/yajl_gen.h>

#define YAJL_ALLOW_COMMENTS 1
#define YAJL_CHECK_UTF8 1

/* ei includes */

#include "ei.h"    
#include <erl_driver.h>

static inline ErlDrvPort CtxToPort(void* ctx) {
  return (ErlDrvPort)ctx;
}

static inline void output(void* ctx, const char* data, int len) {
  driver_output(CtxToPort(ctx), (char*)data, len);
}

static inline void output2(void* ctx, const char* data, int len, const char* data2, int len2) {
  driver_output2(CtxToPort(ctx), (char*)data, len, (char*)data2, len2);
}

static inline int send_data(void* ctx, char type, const char* data, unsigned int len)
{ 
  flog(stderr, "->>>", type, data, len);

  assert(!data || len);

  if(!data)
    output(ctx, &type, 1);
  else
    output2(ctx, &type, 1, data, len); 
  
  return 1;
}

extern void json_parse(ErlDrvData session, const unsigned char* s, int len, int parseInArray);
extern void json_parse_ei(ErlDrvData session, const unsigned char* s, int len, int parseInArray);

#endif
