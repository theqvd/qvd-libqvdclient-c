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
/* Also used for vm list  normall y 16KB*2 */
#define BUFFER_SIZE CURL_MAX_WRITE_SIZE * 2
#include "qvdbuffer.h"

#define ABOUT "About: $Id$"
/* #define DEBUG 1 */
#define DEBUG_FLAG_ENV_VAR_NAME "QVD_DEBUG"
#define DEBUG_FILE_ENV_VAR_NAME "QVD_DEBUG_FILE"
#define MAX_USERPWD 128
#define MAX_AUTHDIGEST 4+MAX_USERPWD*4/3
#define MAX_BASEURL 1024
#define MAX_PARAM 32
#define MAX_ERROR_BUFFER 256
#define MAXDISPLAYSTRING 256
#define MAXHOMESTRING 128
#define MAX_HTTP_RESPONSES_FOR_UPGRADE 10
#define DEFAULT_USERAGENT_PRODUCT "QVD/3.1"
#define MAX_USERAGENT 128
#define MAX_OS 128
#define DEFAULT_OS "linux"
#define MAX_GEOMETRY 128
#define DEFAULT_GEOMETRY "800x600"
#define MAX_LINK 128
#define DEFAULT_LINK "adsl"
#define MAXCERTSTRING 256
#define HOME_ENV "HOME"
#define APPDATA_ENV "APPDATA"
#define CONF_DIR ".qvd"
#define CERT_DIR ".qvd/certs"
#define MAX_NX_OPTS_BUFFER 256

/* the buffer size is 32K */
#define MAX(x,y) ((x) > (y) ? (x) : (y))
#define MIN(x,y) ((x) < (y) ? (x) : (y))
/*#define TRACE*/

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

struct qvdclientstruct {
  CURL *curl;
  CURLcode res;
  char error_buffer[MAX_ERROR_BUFFER];
  char hostname[MAX_BASEURL];
  int port;
  char username[MAX_USERPWD];
  char password[MAX_USERPWD];
  char userpwd[MAX_USERPWD];
  char authdigest[MAX_AUTHDIGEST];
  char baseurl[MAX_BASEURL];
  int numvms;
  vmlist *vmlist;
  QvdBuffer buffer;
  char link[MAX_LINK];
  char geometry[MAX_GEOMETRY];
  char os[MAX_OS];
  const char *keyboard;
  int print_enabled;
  int fullscreen;
  char display[MAXDISPLAYSTRING];
  char home[MAXHOMESTRING];
  char certpath[MAXCERTSTRING];
  char useragent[MAX_USERAGENT];
  int ssl_no_cert_check;
  int (*ssl_verify_callback)(struct qvdclientstruct *qvd, const char *cert_pem_str, const char *cert_pem_data);
  /* You can use userdata for the ssl_verify_callback for example */
  void *userdata;
  char *nx_options;
} ;
typedef struct qvdclientstruct qvdclient;

qvdclient *qvd_init(const char *hostname, const int port, const char *username, const char *password);
vmlist *qvd_list_of_vm(qvdclient *qvd);
int qvd_connect_to_vm(qvdclient *qvd, int id);
void qvd_free(qvdclient *qvd);
void qvd_set_geometry(qvdclient *qvd, const char *geometry);
void qvd_set_fullscreen(qvdclient *qvd);
void qvd_set_nofullscreen(qvdclient *qvd);
void qvd_set_debug();
void qvd_set_display(qvdclient *qvd, const char *display);
void qvd_set_home(qvdclient *qvd, const char *home);
void qvd_set_useragent(qvdclient *qvd, const char *useragent);
void qvd_set_os(qvdclient *qvd, const char *os);
void qvd_set_geometry(qvdclient *qvd, const char *geometry);
void qvd_set_link(qvdclient *qvd, const char *link);
void qvd_set_no_cert_check(qvdclient *qvd);
void qvd_set_strict_cert_check(qvdclient *qvd);
void qvd_set_unknown_cert_callback(qvdclient *qvd, int (*ssl_verify_callback)(qvdclient *, const char *cert_pem_str, const char *cert_pem_data));
void qvd_set_nx_options(qvdclient *qvd, const char *nx_options);
char *qvd_get_last_error(qvdclient *qvd);
#endif
