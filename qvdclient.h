/*
 *
 */
#ifndef _QVDCLIENT_H
#define _QVDCLIENT_H
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <curl/curl.h>
/*#define BUFFER_SIZE CURL_MAX_WRITE_SIZE * 2 * 100 */
#define BUFFER_SIZE CURL_MAX_WRITE_SIZE * 2 * 100
#include "qvdbuffer.h"

#define ABOUT "About: $Id$"
/* #define DEBUG 1 */
#define DEBUG_FLAG_ENV_VAR_NAME "QVD_DEBUG"
#define DEBUG_FILE_ENV_VAR_NAME "QVD_DEBUG_FILE"
#define MAX_USERPWD 128
#define MAX_AUTHDIGEST 4+MAX_USERPWD*4/3
#define MAX_BASEURL 1024
#define MAX_PARAM 32
/* the buffer size is 32K */
#define MAX(x,y) ((x) > (y) ? (x) : (y))
#define MIN(x,y) ((x) < (y) ? (x) : (y))
#define TRACE

typedef struct {
  int id;
  char *name;
  char *state;
  int blocked;
} vm;

struct vmliststruct {
  vm *data;
  struct vmliststruct *next;
};

typedef struct vmliststruct vmlist;

typedef struct {
  CURL *curl;
  CURLcode res;
  /*  char error_buffer[CURL_ERROR_SIZE]; */
  const char *hostname;
  int port;
  const char *username;
  const char *password;
  char userpwd[MAX_USERPWD];
  char authdigest[MAX_AUTHDIGEST];
  char baseurl[MAX_BASEURL];
  int numvms;
  vmlist *vmlist;
  QvdBuffer buffer;
  const char *link;
  const char *geometry;
  const char *os;
  const char *keyboard;
  int print_enabled;
  int fullscreen;

} qvdclient;


int launch_qvd(void);
qvdclient *qvd_init(const char *hostname, const int port, const char *username, const char *password);
vmlist *qvd_list_of_vm(qvdclient *qvd);
int qvd_connect_to_vm(qvdclient *qvd, int id);
void qvd_free(qvdclient *qvd);

#endif
