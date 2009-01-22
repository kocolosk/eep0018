#include "eep0018.h"
#include <string.h>

static int encode(const char *buf, int *index, yajl_gen g);

static int encode_long(const char *buf, int *index, yajl_gen g) {
    long number;
    if (ei_decode_long(buf, index, &number)) return -1;
    return yajl_gen_integer(g, number);
}

/* think about bignum support later, need gmp or similar */
static int encode_ulong(const char *buf, int *index, yajl_gen g) {
    unsigned long number;
    if (ei_decode_ulong(buf, index, &number)) return -1;
    /* yajl doesn't support ulong, so print to string first */
    char numstring[11];
    sprintf(numstring, "%lu", number);
    return yajl_gen_number(g, numstring, strlen(numstring));
}

static int encode_ulonglong(const char *buf, int *index, yajl_gen g) {
    unsigned long long number;
    if (ei_decode_ulonglong(buf, index, &number)) return -1;
    /* yajl doesn't support ulong, so print to string first */
    char numstring[21];
    sprintf(numstring, "%llu", number);
    return yajl_gen_number(g, numstring, strlen(numstring));
}

static int encode_longlong(const char *buf, int *index, yajl_gen g) {
    long long number;
    if (ei_decode_longlong(buf, index, &number)) {
        /* ei is stupid about signed/unsigned limits */
        return encode_ulonglong(buf, index, g);
    }
    /* yajl doesn't support ulong, so print to string first */
    char numstring[21];
    sprintf(numstring, "%lld", number);
    return yajl_gen_number(g, numstring, strlen(numstring));
}

static int encode_bignum(const char *buf, int *index, yajl_gen g) {
    return 0;
}

/* all floating-point numbers are sent with double-precision */
static int encode_double(const char *buf, int *index, yajl_gen g) {
    double number;
    if (ei_decode_double(buf, index, &number)) return -1;
    return yajl_gen_double(g, number);
}

/* this might not get called, instead just check true/false in decode_atom */
static int encode_boolean(const char *buf, int *index, yajl_gen g) {
    fprintf(stderr, "called encode_boolean\r\n");
    int boolean;
    if (ei_decode_boolean(buf, index, &boolean)) return -1;
    return yajl_gen_bool(g, boolean);
}

/* this is a list of integers -- real strings must be binaries */
static int encode_string(const char *buf, int *index, int len, yajl_gen g) {
    char thestring[len+1];
    int stat, i;
    if (ei_decode_string(buf, index, thestring)) return -1;
    if (stat = yajl_gen_array_open(g)) return stat;
    for (i=0; i<len; ++i) {
        if (stat = yajl_gen_integer(g, (int)(thestring[i]))) return stat;
    }
    return yajl_gen_array_close(g);
}

static int encode_atom(const char *buf, int *index, int len, yajl_gen g) {
    char thestring[len+1];
    if (ei_decode_atom(buf, index, thestring)) return -1;
    if (!strcmp(thestring, "true")) {
        return yajl_gen_bool(g, 1);
    }
    else if (!strcmp(thestring, "false")) {
        return yajl_gen_bool(g, 0);
    }
    else if (!strcmp(thestring, "null")) {
        return yajl_gen_null(g);
    }
    else {
        return yajl_gen_string(g, (unsigned char*)thestring, len);
    }
}

/* assume all binaries are strings */
static int encode_binary(const char *buf, int *index, int len, yajl_gen g) {
    char thestring[len+1];
    long actual_length;
    if (ei_decode_binary(buf, index, thestring, &actual_length)) return -1;
    assert(actual_length == len);
    thestring[len] = '\0';
    return yajl_gen_string(g, (unsigned char*)thestring, len);
}

static int encode_list_elements(const char *buf, int *index, yajl_gen g) {
    int stat, arity, i;
    if (ei_decode_list_header(buf, index, &arity)) return -1;
    if (arity > 0) {
        for (i=0; i<arity; ++i) {
            if (stat = encode(buf, index, g)) return stat;
        }

        /* now for the tail: if it's an empty list, ignore */
        if (ei_decode_list_header(buf, index, &arity)) return -1;
        if (arity > 0) {
            if (stat = encode(buf, index, g)) return stat;
        }
    }
    return yajl_gen_status_ok;
}

static int encode_list(const char *buf, int *index, int len, yajl_gen g) {
    int stat;
    yajl_gen_array_open(g);
    if (stat = encode_list_elements(buf, index, g)) return stat;
    return yajl_gen_array_close(g);
}

/* tuples must have arity 1 or 2.  If 1, the element must be a list of 
 * 2-arity tuples; this is an Object.  If 2, this is a key-value pair */
static int encode_tuple(const char *buf, int *index, int len, yajl_gen g) {
    int type, size, stat, arity;
    if (ei_decode_tuple_header(buf, index, &arity)) return -1;
    assert(arity == len);
    if (arity == 1) {
        if (ei_get_type(buf, index, &type, &size)) return -1;
        assert(type == ERL_LIST_EXT || type == ERL_NIL_EXT);
        yajl_gen_map_open(g);
        if (stat = encode_list_elements(buf, index, g)) return stat;
        return yajl_gen_map_close(g);
    }
    else if (arity == 2) {
        if (stat = encode(buf, index, g)) return stat;
        if (stat = encode(buf, index, g)) return stat;
        return 0;
    }
    return 10;
}

static int encode(const char *buf, int *index, yajl_gen g) {
    int type, size, stat;
    if (ei_get_type(buf, index, &type, &size)) return -1;
    switch(type) {
        case ERL_SMALL_INTEGER_EXT:
        case ERL_INTEGER_EXT: 
        stat = encode_long(buf, index, g); break;
        
        case ERL_SMALL_BIG_EXT:
        stat = encode_longlong(buf, index, g); break;
        
        case ERL_LARGE_BIG_EXT:
        stat = encode_ulonglong(buf, index, g); break;
        
        case ERL_FLOAT_EXT:
        stat = encode_double(buf, index, g); break;
        
        case ERL_ATOM_EXT:
        stat = encode_atom(buf, index, size, g); break;
        
        case ERL_SMALL_TUPLE_EXT:
        stat = encode_tuple(buf, index, size, g); break;
        
        case ERL_STRING_EXT:
        stat = encode_string(buf, index, size, g); break;
        
        case ERL_NIL_EXT:
        case ERL_LIST_EXT:
        stat = encode_list(buf, index, size, g); break;
        
        case ERL_BINARY_EXT:
        stat = encode_binary(buf, index, size, g); break;
        
        default:
        stat = type;
    }
    return stat;
}

void json_generate(ErlDrvData session, const char* buf, int len, int opts) {
    yajl_gen_config conf = { 0, "    " };
    yajl_gen g = yajl_gen_alloc(&conf);
    int stat, index, version;
    char *msg = NULL;
    ErlDrvPort port = (ErlDrvPort) session;
    
    index = 0;
    if (ei_decode_version(buf, &index, &version)) {
        stat = -1;
        flog(stderr, "decode failed", 0, buf, len);
    } else {
        flog(stderr, "try encode", 0, buf, len);
        stat = encode(buf, &index, g);
        flog(stderr, "encode complete", 0, buf, len);
    }
    
    const unsigned char *output;
    unsigned int outlen;
    if (stat == yajl_gen_status_ok) {
        stat = yajl_gen_get_buf(g, &output, &outlen);
    }
    
    switch(stat) {
        case yajl_gen_status_ok: 
            break;
        case -1: 
            msg = "EI decoding failed"; break;
        case yajl_gen_keys_must_be_strings: 
            msg = "Object keys must be strings"; break;
        case yajl_max_depth_exceeded:
            msg = "YAJL's max generation depth exceeded"; break;
        case yajl_gen_in_error_state:
            msg = "A generator was called while YAJL was in error state"; break;
        case yajl_gen_generation_complete:
            msg = "More than one JSON document was received"; break;
        default:
            msg = "Unknown status message"; break;
    }
    
    ei_x_buff result;
    ei_x_new_with_version(&result);
    
    /* 
     * if result is not ok: we write {error, "reason"} instead. This is 
     * something that will never be encoded from any JSON data.
     */
    if(msg) {
        char outmsg[100];
        sprintf(outmsg, "{\"error\":%s}", msg);
        ei_x_encode_binary(&result, (unsigned char*) outmsg, strlen(outmsg));
    } 
    else {
        ei_x_encode_binary(&result, output, outlen);
    }
    
    send_data(port, EEP0018_EI, result.buff, result.index);
    yajl_gen_free(g);
    ei_x_free(&result);
}
