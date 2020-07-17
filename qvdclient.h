/*
 * libqvdclient qvdclient.h
 *
 * Copyright (C) 2016  theqvd.com trade mark of Qindel Formacion y Servicios SL
 *
 * libqvdclient is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _QVDCLIENT_H
#define _QVDCLIENT_H
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <curl/curl.h>
/* Also used for vm list  normally 16KB*2 */
/* #define BUFFER_SIZE CURL_MAX_WRITE_SIZE * 2 */
#define BUFFER_SIZE 65536
#include "qvdbuffer.h"
#define QVDVERSION 125
#define QVDABOUT "Version: 1.2.5. $Id$"
#define QVDCHANGELOG "1.2.5 17/10/2018 Unhardcode keyboard layout\n" \
                     "1.2.4 04/04/2018 Added support for environment variables QVDPORT QVDVMID\n" \
                     "1.2.3 30/04/2016 Allow to preselect VM\n" \
                     "1.2.2 30/04/2016 Change license to GPLv2\n" \
                     "1.2.1 26/08/2015 Use ASL logging for Apple, openssl: 1.0.2d, jansson: 2.7\n" \
                     "1.2.0 07/05/2015 Improve Select on write file descriptors\n" \
                     "1.1.9 06/05/2015 Use NXTransCleanupForReconnect for IOS\n" \
                     "1.1.8 06/05/2015 Include nxcomp version in the -v flag\n" \
                     "1.1.7 03/05/2015 During debug show correct version text\n" \
                     "1.1.6 28/04/2015 More time to session takeover\n" \
                     "1.1.5  5/12/2014 Support for HTTP code 402\n" \
                     "1.1.4 23/11/2014 Extend pass length to 256\n" \
                     "1.1.3 21/07/2014 Fix -r switch (not in the qvdclient binary only in the lib\n" \
                     "1.1.2 18/07/2014 Fix hardcoded ip in reconnect (option -2)\n" \
                     "1.1.1 26/06/2014 Upgraded curl to 7.37.0 and nxcomp to 3.5.0.22 and openssl to 1.0.1h. Use implicit curl_global_init. Debug now goes to stderr.\n" \
                     "1.1.0 21/06/2014 Added support for restart -r, and to reconnect twice\n"\
                     "1.0.1 12/10/2013 in the client added support for environment vars QVDLOGIN QVDPASSWORD and QVDHOST\n"\
                     "                 and in the library support for qvd_get_changelog\n"\
                     "1.0   ////////// Initial version\n"
/* #define DEBUG 1 */
#define DEBUG_FLAG_ENV_VAR_NAME "QVD_DEBUG"
#define DEBUG_FILE_ENV_VAR_NAME "QVD_DEBUG_FILE"
#define MAX_USERPWD 256
#define MAX_BEARER 4+MAX_USERPWD*4/3
#define MAX_AUTHDIGEST (MAX_BEARER*2)
#define MAX_BASEURL 1024
#define MAX_PARAM 32
#define MAX_ERROR_BUFFER 256
#define MAXDISPLAYSTRING 256
#define MAX_PATH_STRING 256
/* This can be also a session takeover or VM start */
#define MAX_HTTP_RESPONSES_FOR_UPGRADE 60
#define DEFAULT_USERAGENT_PRODUCT "QVD/3.1"
#define MAX_USERAGENT 512
#define MAX_OS 128
#define DEFAULT_OS "linux"
#define MAX_KB_LAYOUT 16
#define DEFAULT_KB_LAYOUT "pc105/en"
#define MAX_GEOMETRY 128
#define DEFAULT_GEOMETRY "800x600"
#define MAX_LINK 128
#define DEFAULT_LINK "adsl"
#define HOME_ENV "HOME"
#define DISPLAY_ENV "DISPLAY"
#define DEFAULT_DISPLAY "127.0.0.1:0"
#define APPDATA_ENV "APPDATA"
#define QVDLOGIN_ENV "QVDLOGIN"
#define QVDPASSWORD_ENV "QVDPASSWORD"
#define QVDBEARER_ENV "QVDBEARER"
#define QVDHOST_ENV "QVDHOST"
#define QVDPORT_ENV "QVDPORT"
#define QVDVMID_ENV "QVDVMID"
#define CONF_DIR ".qvd"
#define CERT_DIR ".qvd/certs"
#define MAX_NX_OPTS_BUFFER 256
#define MAX_STRING_VERSION 256
/* 200 milliseconds sleep */
#define QVDLOOP_TIMEOUT_SEC 0
#define QVDLOOP_TIMEOUT_USEC 200000000

#ifndef MAX
#define MAX(x,y) ((x) > (y) ? (x) : (y))
#endif
#ifndef MIN
#define MIN(x,y) ((x) < (y) ? (x) : (y))
#endif
/* #define TRACE 1 */
#ifdef __cplusplus
extern "C" {
#endif

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
  char bearer[MAX_BEARER];
  int use_bearer;
  char baseurl[MAX_BASEURL];
  int numvms;
  vmlist *vmlist;
  QvdBuffer buffer;
  char link[MAX_LINK];
  char geometry[MAX_GEOMETRY];
  char os[MAX_OS];
  char keyboard[MAX_KB_LAYOUT];
  int print_enabled;
  int fullscreen;
  char display[MAXDISPLAYSTRING];
  char home[MAX_PATH_STRING];
  char certpath[MAX_PATH_STRING];
  char useragent[MAX_USERAGENT];
  int ssl_no_cert_check;
  int (*ssl_verify_callback)(struct qvdclientstruct *qvd, const char *cert_pem_str, const char *cert_pem_data);
  int (*progress_callback)(struct qvdclientstruct *, const char *message);
  /* You can use userdata for the ssl_verify_callback for example */
  void *userdata;
  char *nx_options;
  int use_client_cert;
  char client_cert[MAX_PATH_STRING]; /* PEM format */
  char client_key[MAX_PATH_STRING];
  int end_connection;
  int payment_required;
} ;
typedef struct qvdclientstruct qvdclient;


int qvd_get_version(void);
const char *qvd_get_version_text(void);
const char *qvd_get_changelog(void);
qvdclient *qvd_init(const char *hostname, const int port, const char *username, const char *password, const char *bearer);
vmlist *qvd_list_of_vm(qvdclient *qvd);
int qvd_stop_vm(qvdclient *qvd, int vm);
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
void qvd_set_link(qvdclient *qvd, const char *link);
void qvd_set_kb_layout(qvdclient *qvd, const char *kb_layout);
void qvd_set_no_cert_check(qvdclient *qvd);
void qvd_set_strict_cert_check(qvdclient *qvd);
void qvd_set_unknown_cert_callback(qvdclient *qvd, int (*ssl_verify_callback)(qvdclient *, const char *cert_pem_str, const char *cert_pem_data));
void qvd_set_progress_callback(qvdclient *qvd, int (*progress_callback)(qvdclient *, const char *message));
void qvd_set_nx_options(qvdclient *qvd, const char *nx_options);
void qvd_set_cert_files(qvdclient *qvd, const char *client_cert, const char *client_key);
char *qvd_get_last_error(qvdclient *qvd);

int qvd_curl_debug_callback(CURL *handle, curl_infotype type,
			    unsigned char *data, size_t size,
			    void *userp);
void qvd_printf(const char *format, ...);
void qvd_error(qvdclient *qvd, const char *format, ...);
void qvd_progress(qvdclient *qvd, const char *message);
void set_debug_level(int level);
int get_debug_level(void);
void qvd_end_connection(qvdclient *qvd);
int qvd_payment_required(qvdclient *qvd);
#ifdef __cplusplus
}
#endif

#endif
