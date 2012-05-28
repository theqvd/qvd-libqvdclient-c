#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <curl/curl.h>
#include <jansson.h>
#include <NX.h>
#include "qvdclient.h"
#include "qvdbuffer.h"
#include "qvdvm.h"

/* See http://www.openssl.org/docs/ssl/SSL_get_ex_new_index.html# */
#ifndef ANDROID
extern char **environ;
#endif

static int _qvd_ssl_index;

int _qvd_proxy_connect(qvdclient *qvd);
int _qvd_client_loop(qvdclient *qvd, int connFd, int proxyFd);
size_t _qvd_write_buffer_callback(void *contents, size_t size, size_t nmemb, void *buffer);
/* static void _qvd_dumpcert(X509 *x); */
/* void _qvd_print_certificate(X509 *cert); */
int _qvd_verify_cert_callback(int preverify_ok, X509_STORE_CTX *x509_ctx);
CURLcode _qvd_sslctxfun(CURL *curl, SSL_CTX *sslctx, void *parm);
int _qvd_set_base64_auth(qvdclient *qvd);
int _qvd_switch_protocols(qvdclient *qvd, int id);
void _qvd_print_environ();

/* Init and free functions */
qvdclient *qvd_init(const char *hostname, const int port, const char *username, const char *password) {
  qvdclient *qvd;
  if (strlen(username) + strlen(password) + 2 > MAX_USERPWD) {
    qvd_error(qvd, "Length of username and password + 2 is longer than %d\n", MAX_USERPWD);
    return NULL;
  }

  if (strlen(hostname) + 6 + strlen("https:///") + 2 > MAX_BASEURL) {
    qvd_error(qvd, "Length of hostname and port + scheme  + 2 is longer than %d\n", MAX_BASEURL);
    return NULL;
  }

  if (! (qvd = (qvdclient *) malloc(sizeof(qvdclient)))) {
    qvd_error(qvd, "Error allocating memory: %s", strerror(errno));
    return NULL;
  }
  
  if (snprintf(qvd->userpwd, MAX_USERPWD, "%s:%s", username, password) >= MAX_USERPWD) {
    qvd_error(qvd, "Error initializing userpwd (string too long)\n");
    free(qvd);
    return NULL;
  }
  if (_qvd_set_base64_auth(qvd)) {
    qvd_error(qvd, "Error initializing authdigest\n");
    free(qvd);
    return NULL;
    }

  if (snprintf(qvd->baseurl, MAX_BASEURL, "https://%s:%d", hostname, port) >= MAX_BASEURL) {
    qvd_error(qvd, "Error initializing baseurl(string too long)\n");
    free(qvd);
    return NULL;
  }
  if (snprintf(qvd->useragent, MAX_USERAGENT, "%s %s", DEFAULT_USERAGENT_PRODUCT, curl_version()) >= MAX_USERAGENT) {
    qvd_error(qvd, "Error initializing useragent (string too long)\n");
    free(qvd);
    return NULL;
  }

  if (!_qvd_set_certdir(qvd)) {
    free(qvd);
    return NULL;
  }

  qvd->curl = curl_easy_init();
  if (!qvd->curl) {
    qvd_error(qvd, "Error initializing curl\n");
    free(qvd);
    return NULL;
  }
  if (get_debug_level()) 
    curl_easy_setopt(qvd->curl, CURLOPT_VERBOSE, 1L);

  /* curl_easy_setopt(qvd->curl, CURLOPT_SSL_VERIFYPEER, 1L); */
  /* curl_easy_setopt(qvd->curl, CURLOPT_SSL_VERIFYHOST, 2L); */
  curl_easy_setopt(qvd->curl, CURLOPT_CERTINFO, 1L);
  curl_easy_setopt(qvd->curl, CURLOPT_CAPATH, qvd->certpath);
  curl_easy_setopt(qvd->curl, CURLOPT_SSL_CTX_FUNCTION, _qvd_sslctxfun);
  curl_easy_setopt(qvd->curl, CURLOPT_SSL_CTX_DATA, (void *)qvd);
  /*  curl_easy_setopt(qvd->curl, CURLOPT_CAINFO, NULL);*/
  _qvd_ssl_index = SSL_CTX_get_ex_new_index(0, (void *)qvd, NULL, NULL, NULL);
  curl_easy_setopt(qvd->curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(qvd->curl, CURLOPT_SSL_VERIFYHOST, 0L);
  curl_easy_setopt(qvd->curl, CURLOPT_TCP_NODELAY, 1L);
  /*  curl_easy_setopt(qvd->curl, CURLOPT_FAILONERROR, 1L);*/
  curl_easy_setopt(qvd->curl, CURLOPT_HTTPAUTH, (long)CURLAUTH_BASIC);
  curl_easy_setopt(qvd->curl, CURLOPT_USERPWD, qvd->userpwd);
  curl_easy_setopt(qvd->curl, CURLOPT_WRITEFUNCTION, _qvd_write_buffer_callback);
  curl_easy_setopt(qvd->curl, CURLOPT_WRITEDATA, &(qvd->buffer));
  curl_easy_setopt(qvd->curl, CURLOPT_USERAGENT, qvd->useragent);
  /* Copy parameters */
  strncpy(qvd->hostname, hostname, MAX_BASEURL);
  qvd->hostname[MAX_BASEURL - 1] = '\0';
  qvd->port = port;
  strncpy(qvd->username, username, MAX_USERPWD);
  qvd->username[MAX_USERPWD - 1] = '\0';
  strncpy(qvd->password, password, MAX_USERPWD);
  qvd->password[MAX_USERPWD - 1] = '\0';
  qvd->numvms = 0;
  qvd_set_link(qvd, DEFAULT_LINK);
  qvd_set_geometry(qvd, DEFAULT_GEOMETRY);
  qvd_set_os(qvd, DEFAULT_OS);
  qvd->keyboard = "pc%2F105";
  qvd->fullscreen = 0;
  qvd->print_enabled = 0;
  qvd->ssl_no_cert_check = 0;
  qvd->ssl_verify_callback = NULL;
  qvd->userdata = NULL;
  qvd->nx_options = NULL;

  *(qvd->display) = '\0';
  *(qvd->home) = '\0';
  strcpy(qvd->error_buffer, "");
  QvdBufferInit(&(qvd->buffer));

  if (!(qvd->vmlist = malloc(sizeof(vmlist)))) {
    free(qvd);
    return NULL;
  }
  QvdVmListInit(qvd->vmlist);
  
  return qvd;
}

void qvd_free(qvdclient *qvd) {
  curl_easy_cleanup(qvd->curl);
  QvdVmListFree(qvd->vmlist);
  /* nx_options should be null */
  free(qvd->nx_options);
  free(qvd);
}

vmlist *qvd_list_of_vm(qvdclient *qvd) {
  char url[MAX_BASEURL];
  int i;
  long http_code = 0;
  json_error_t error;
  char *command = "/qvd/list_of_vm";

  if (snprintf(url, MAX_BASEURL, "%s%s", qvd->baseurl, command) >= MAX_BASEURL) {
    qvd_error(qvd, "Error initializing url in list_of_vm, length is longer than %d\n", MAX_BASEURL);
    return NULL;
  }

  curl_easy_setopt(qvd->curl, CURLOPT_URL, url);
  /*  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &jsonBuffer); */
  qvd->res = curl_easy_perform(qvd->curl);
  qvd_printf("After easy_perform: %ul\n", qvd->res);
  if (qvd->res)
    {
      /*    qvd_error(qvd, "An error ocurred getting url <%s>: %d <%s>\n", url, qvd->res, curl_easy_strerror(qvd->res));*/
      /* qvd_error(qvd, "An error ocurred getting url <%s>\n", url); */
      qvd_error(qvd, "An error ocurred getting url <%s>: %ul (%s)\n", url, qvd->res, curl_easy_strerror(qvd->res)); 
      /*    qvd_printf("An error ocurred getting url <%s>: %d\n", url, qvd->res);*/
      /* struct curl_certinfo certinfo; */
      /* curl_easy_getinfo(qvd->curl, CURLINFO_CERTINFO, &certinfo); */
      /* qvd_printf("Number of certs: %d\n", certinfo.num_of_certs);*\/ */
      return NULL;
    }
  
  curl_easy_getinfo (qvd->curl, CURLINFO_RESPONSE_CODE, &http_code);
  if (http_code == 401)
    {
      qvd_error(qvd, "Error authenticating user\n");
      return NULL;
    }
  qvd_printf("No error and no auth error after curl_easy_perform\n");
  /*  QvdBufferInit(&(qvd->buffer)); */

  json_t *vmList = json_loads(qvd->buffer.data, 0, &error);
  int arrayLength = json_array_size(vmList);
  qvd->numvms = arrayLength;
  qvd_printf("VMs available: %d\n", qvd->numvms);

  QvdVmListFree(qvd->vmlist);
  if (!(qvd->vmlist = malloc(sizeof(vmlist)))) {
    qvd_error(qvd, "Error allocating memory for vmlist");
    return NULL;
  }
  QvdVmListInit(qvd->vmlist);

  for (i = 0; i < arrayLength; i++) {
    json_t *obj = json_array_get(vmList, i);
    int id, blocked;
    char *name, *state;
    json_unpack(obj, "{s:i,s:s,s:i,s:s}", 
		"id", &id,
		"state", &state,
		"blocked", &blocked,
		"name", &name);
    qvd_printf("VM ID:%d NAME:%s STATE:%s BLOCKED:%d\n", 
	       id, name, state, blocked);
    QvdVmListAppendVm(qvd, qvd->vmlist, QvdVmNew(id, name, state, blocked));
  }
  /*  QvdBufferReset(&(qvd->buffer));*/
  if (qvd->numvms <= 0) {
    qvd_error(qvd, "No virtual machines available for user %s\n", qvd->username);
  }

  return qvd->vmlist;
}

int qvd_connect_to_vm(qvdclient *qvd, int id)
{
  int result, proxyFd, fd;
  long curlsock;

  qvd_printf("qvd_connect_to_vm(%p,%d)", qvd, id);
  if (qvd->display && (*(qvd->display)) != '\0') {
    qvd_printf("Setting DISPLAY to %s", qvd->display);
    if (setenv("DISPLAY", qvd->display, 1)) {
      qvd_error(qvd, "Error setting DISPLAY to %s. errno: %d (%s)", qvd->display, errno, strerror(errno));
    }
  }
  if (qvd->home && (*(qvd->home)) != '\0') {
    qvd_printf("Setting NX_HOME to %s", qvd->display);
    if (setenv("NX_HOME", qvd->home, 1)) {
      qvd_error(qvd, "Error setting NX_HOME to %s. errno: %d (%s)", qvd->home, errno, strerror(errno));
    }
  }

  result = _qvd_switch_protocols(qvd, id);
  _qvd_print_environ();
  /* if non zero return with error */
  if (result)
    return result;

  curl_easy_getinfo(qvd->curl, CURLINFO_LASTSOCKET, &curlsock);  
  fd = (int) curlsock;

  if ((proxyFd = _qvd_proxy_connect(qvd)) < 0)
    return 4;

  qvd_printf("Remote fd: %d Local fd: %d\n", fd, proxyFd);
  qvd_printf("Before _qvd_client_loop\n");
  _qvd_client_loop(qvd, fd, proxyFd);
  shutdown(proxyFd, 2);
  return 0;
}


/*
 * TODO set general way to set options
 */
void qvd_set_fullscreen(qvdclient *qvd) {
  qvd->fullscreen = 1;
}
void qvd_set_nofullscreen(qvdclient *qvd) {
  qvd->fullscreen = 0;
}
void qvd_set_debug() {
  set_debug_level(2);
}

void qvd_set_display(qvdclient *qvd, const char *display) {
  strncpy(qvd->display, display, MAXDISPLAYSTRING);
  qvd->display[MAXDISPLAYSTRING - 1] = '\0';
}

void qvd_set_home(qvdclient *qvd, const char *home) {
  strncpy(qvd->home, home, MAXHOMESTRING);
  qvd->home[MAXHOMESTRING - 1] = '\0';
}

char *qvd_get_last_error(qvdclient *qvd) {
  return qvd->error_buffer;
}

void qvd_set_useragent(qvdclient *qvd, const char *useragent) {
  strncpy(qvd->useragent, useragent, MAX_USERAGENT);
  qvd->useragent[MAX_USERAGENT - 1] = '\0';
  curl_easy_setopt(qvd->curl, CURLOPT_USERAGENT, qvd->useragent);
}
void qvd_set_os(qvdclient *qvd, const char *os) {
  strncpy(qvd->os, os, MAX_OS);
  qvd->os[MAX_OS - 1] = '\0';
}
void qvd_set_geometry(qvdclient *qvd, const char *geometry) {
  strncpy(qvd->geometry, geometry, MAX_GEOMETRY);
  qvd->os[MAX_GEOMETRY - 1] = '\0';
}
void qvd_set_link(qvdclient *qvd, const char *link) {
  strncpy(qvd->link, link, MAX_LINK);
  qvd->os[MAX_LINK - 1] = '\0';
}

void qvd_set_no_cert_check(qvdclient *qvd) {
  qvd->ssl_no_cert_check = 1;
}
void qvd_set_strict_cert_check(qvdclient *qvd) {
  qvd->ssl_no_cert_check = 0;
}


void qvd_set_unknown_cert_callback(qvdclient *qvd, int (*ssl_verify_callback)(qvdclient *qvd, const char *cert_pem_str, const char *cert_pem_data))
{
  qvd->ssl_verify_callback = ssl_verify_callback;
}

void qvd_set_nx_options(qvdclient *qvd, const char *nx_options) {
  /*  MAX_NX_OPTS_BUFFER */
  /* Should be null in case it was never defined */
  free(qvd->nx_options);
  qvd->nx_options = malloc(strlen(nx_options) + 1);
  strcpy(qvd->nx_options, nx_options);
}

/*
 * Internal funcs for qvd_init
 */

int _qvd_set_certdir(qvdclient *qvd)
{
  char *home = getenv(HOME_ENV);
  char *appdata = getenv(APPDATA_ENV);
  int result;
  if (home == NULL && appdata == NULL)
    {
      qvd_error(qvd, "Error %s and %s environment var were not defined, cannot save to $HOME/.qvd/certs", HOME_ENV, APPDATA_ENV);
      return 0;
    }

  if (home == NULL)
    {
      home = appdata;
      qvd_printf("%s was not defined using %s environment var", HOME_ENV, APPDATA_ENV);
    }
    

  /* Define .qvd/certs in qvdclient.h */
  if (!_qvd_create_dir(qvd, home, CONF_DIR))
    return 0;

  if (!_qvd_create_dir(qvd, home, CERT_DIR))
    return 0;

  snprintf(qvd->certpath, MAXCERTSTRING, "%s/%s", home, CERT_DIR);
  qvd->certpath[MAXCERTSTRING] = '\0';
  if (strlen(qvd->certpath) == MAXCERTSTRING)
    {
      qvd_error(qvd, "Cert string too long (%d) recompile program. Path is %s", MAXCERTSTRING, qvd->certpath);
      return 0;
    }
  return 1;
}

int _qvd_set_base64_auth(qvdclient *qvd)
{
  CURLcode error;
  char *ptr = NULL;
  size_t outlen;
  int result = 0;
  error = Curl_base64_encode((struct SessionHandle *) qvd,
                             qvd->userpwd, strlen(qvd->userpwd),
                             &ptr, &outlen);
  if (error != CURLE_OK)
    {
      qvd_error(qvd, "Error in getBase64Auth");
      return 1;
    }
  
  if (snprintf(qvd->authdigest, MAX_AUTHDIGEST, "%s", ptr) >= MAX_AUTHDIGEST)
    {
      qvd_error(qvd, "The authdigest string for %s is longer than %d\n", qvd->userpwd, MAX_AUTHDIGEST);
      result = 1;
    } else 
    {
#ifdef TRACE
      qvd_printf("The conversion to base64 from <%s> is <%s>", qvd->userpwd, qvd->authdigest);
#endif
      result = 0;
    }
  free(ptr);
  
  /* hack for base64 encode of "nito@deiro.com:O3xTMCQ3" */
  /*  snprintf(qvd->authdigest, MAX_AUTHDIGEST, "%s", "bml0b0BkZWlyby5jb206TzN4VE1DUTM=");*/
  return result;
}

/*
 * Internal funcs for qvd_connect_to_vm
 */
int _qvd_proxy_connect(qvdclient *qvd)
{
  int proxyPair[2];
  if (socketpair(PF_UNIX, SOCK_STREAM, 0, proxyPair) < 0)
    {
      qvd_error(qvd, "Error creating proxy socket <%s>\n", strerror(errno));
      return -1;
    }
  /*  if (NXTransCreate(proxyPair[0], NX_MODE_SERVER, "nx/nx,data=0,delta=0,cache=16384,pack=0:0") < 0)*/
  if (NXTransCreate(proxyPair[0], NX_MODE_SERVER, qvd->nx_options) < 0)
    {
      qvd_error(qvd, "Error creating proxy transport <%s>\n", strerror(errno));
      return -1;
    }
  return proxyPair[1];
}

/*
 * _qvd_client_loop
 *            --------------------
 *            |                  |
 * proxyFd ---| _qvd_client_loop |---connFd            
 * (X display)|                  | (curl to remote host)
 *            --------------------
 * 
 *       -----   proxyRead  ---->
 *      <-----   proxyWrite ----
 *
 * We read from proxyFd and store it in the proxyRead buffer and then write it into connFd (curl)
 * We read from connFd and store it in the proxyWrite buffer and then write it ingo proxyFd (NX)
 *
 */
int _qvd_client_loop(qvdclient *qvd, int connFd, int proxyFd)
{
  qvd_printf("_qvd_client_loop\n");
  struct timeval timeout;
  fd_set rfds;
  fd_set wfds;
  int ret, err, maxfds;
  QvdBuffer proxyWrite, proxyRead;
  qvd_printf("_qvd_client_loop(%p, %d, %d)\n", qvd, connFd, proxyFd);
  QvdBufferInit(&proxyWrite);
  QvdBufferInit(&proxyRead);
  do
    {
      timeout.tv_sec = 5;
      timeout.tv_usec = 0;
      maxfds = 1+MAX(connFd, proxyFd);
      FD_ZERO(&rfds);
      FD_ZERO(&wfds);
      if (proxyFd > 0 && QvdBufferCanRead(&proxyRead))
	  FD_SET(proxyFd, &rfds);

      if (connFd > 0 && QvdBufferCanRead(&proxyWrite))
	FD_SET(connFd, &rfds);

      if (NXTransPrepare(&maxfds, &rfds, &wfds, &timeout))
	{
#ifdef TRACE
	  qvd_printf("_qvd_client_loop: executing select()\n");
#endif
	  NXTransSelect(&ret, &err, &maxfds, &rfds, &wfds, &timeout);
	  NXTransExecute(&ret, &err, &maxfds, &rfds, &wfds, &timeout);
	}
      if (ret == -1 && errno == EINTR)
	continue;

      if (ret < 0)
	{
	  qvd_error("Error in _qvd_client_loop: select() %s\n", strerror(errno));
	  return 1;
	}
#ifdef TRACE
	qvd_printf("isset proxyfd read: %d; connfd read: %d\n",
		   FD_ISSET(proxyFd, &rfds), FD_ISSET(connFd, &rfds));
#endif
	/* Read from curl socket and store in proxyWrite buffer */
	if (connFd > 0 && FD_ISSET(connFd, &rfds))
	  {
	    size_t read;
	    int res;
	    res = curl_easy_recv(qvd->curl, proxyWrite.data+proxyWrite.offset,
				 BUFFER_SIZE-proxyWrite.size, &read);

	    switch (res)
	      {
	      case CURLE_OK:
#ifdef TRACE
		qvd_printf("curl: recv'd %ld\n", read);
#endif
		/* TODO if curl recvd is 0 then end */
		proxyWrite.size += read;
		if (read == 0)
		  {
		    qvd_printf("Setting connFd to 0, End of stream\n");
		    connFd = -1; 
		  }
		break;
	      case CURLE_AGAIN:
		qvd_printf("Nothing read. receiving curl_easy_recv: %d CURLE_AGAIN, read %d\n", res, read);
		break;
	      case CURLE_UNSUPPORTED_PROTOCOL:
		qvd_printf("Unsupported protocol. receiving curl_easy_recv: %d CURLE_AGAIN (wait for next iteration), read %d\n", res, read);
		break;
	      default:
		qvd_error(qvd, "Error receiving curl_easy_recv: %d\n", res);
		connFd = -1;
	      }
	  }
	/* Read from NX and store in proxyRead buffer */
	if (proxyFd > 0 && FD_ISSET(proxyFd, &rfds))
	  {
	    ret = QvdBufferRead(&proxyRead, proxyFd);
	    if (ret == 0)
	      {
		qvd_printf("No more bytes to read from proxyFd ending\n");
		proxyFd = -1;
	      }
	    if (ret < 0)
	      {
		qvd_error(qvd, "Error proxyFd read error: %s\n", strerror(errno));
		proxyFd = -1;
	      }
	  }
	if (connFd > 0 && QvdBufferCanWrite(&proxyRead))
	  {
	    /*QvdBufferWrite(&proxyRead, connFd);*/
	    size_t written = 0;
	    int res;
	    res = curl_easy_send(qvd->curl, proxyRead.data+proxyRead.offset,
				 proxyRead.size-proxyRead.offset, &written);
	    switch (res)
	      {
	      case CURLE_OK:
		proxyRead.offset += written;
#ifdef TRACE
		qvd_printf("curl: send'd %ld\n", written);
#endif
		if (proxyRead.offset >= proxyRead.size)
		  QvdBufferReset(&proxyRead);
		break;
	      case CURLE_AGAIN:
		/* TODO create loop to write different data */ 
		qvd_printf("Nothing written, wait for next iteration. curl_easy_send: %d CURLE_AGAIN, written %d\n", res, written);
		break;
	      case CURLE_UNSUPPORTED_PROTOCOL:
		qvd_printf("Unsupported protocol. receiving curl_easy_recv: %d CURLE_AGAIN (wait for next iteration), written %d\n", res, written);
		break;
	      default:
		qvd_error(qvd, "Error sending curl_easy_send: %d", res);
		connFd = -1;
	      }
	  }
	if (proxyFd > 0 && QvdBufferCanWrite(&proxyWrite))
	  {
	    int ret;
	    ret = QvdBufferWrite(&proxyWrite, proxyFd);
	    if (ret < 0 && errno != EINTR) {
	      qvd_error(qvd, "Error reading from proxyFd: %d %s\n", errno, strerror(errno));
	      proxyFd = -1;
	    }
	  }
    } while (connFd > 0 && proxyFd > 0);
  
  NXTransDestroy(NX_FD_ANY);
  return 0;
}

size_t _qvd_write_buffer_callback(void *contents, size_t size, size_t nmemb, void *buffer) 
{
    size_t realsize = size*nmemb;
    size_t bytes_written = QvdBufferAppend((QvdBuffer*)buffer, contents, realsize);
    return bytes_written;
}

int _qvd_switch_protocols(qvdclient *qvd, int id)
{
  fd_set myset, zero;
  size_t bytes_sent;
  int socket, i;
  char url[MAX_BASEURL];
  char base64auth[MAX_PARAM];
  curl_easy_setopt(qvd->curl, CURLOPT_URL, qvd->baseurl);
  curl_easy_setopt(qvd->curl, CURLOPT_CONNECT_ONLY, 1L);
  curl_easy_perform(qvd->curl);
  curl_easy_getinfo(qvd->curl, CURLINFO_LASTSOCKET, &socket);

  /*  if (snprintf(url, MAX_BASEURL, "GET /qvd/connect_to_vm?id=%d&qvd.client.os=%s&qvd.client.fullscreen=%d&qvd.client.geometry=%s&qvd.client.link=%s&qvd.client.keyboard=%s&qvd.client.printing.enabled=%d HTTP/1.1\nAuthorization: Basic %s\nConnection: Upgrade\nUpgrade: QVD/1.0\n\n", id, qvd->os, qvd->fullscreen, qvd->geometry, qvd->link, qvd->keyboard, qvd->print_enabled, qvd->authdigest) >= MAX_BASEURL) { */
  if (snprintf(url, MAX_BASEURL, "GET /qvd/connect_to_vm?id=%d&qvd.client.os=%s&qvd.client.geometry=%s&qvd.client.link=%s&qvd.client.keyboard=%s&qvd.client.fullscreen=%d HTTP/1.1\nAuthorization: Basic %s\nConnection: Upgrade\nUpgrade: QVD/1.0\n\n", id, qvd->os, qvd->geometry, qvd->link, qvd->keyboard, qvd->fullscreen, qvd->authdigest) >= MAX_BASEURL) {
    qvd_error(qvd, "Error initializing authdigest\n");
    return 1;
  }
  qvd_printf("Switch protocols the url is: <%s>\n", url);

  /*  char *url = "GET /qvd/connect_to_vm?id=1&qvd.client.os=linux&qvd.client.fullscreen=&qvd.client.geometry=800x600&qvd.client.link=local&qvd.client.keyboard=pc105%2Fus&qvd.client.printing.enabled=0 HTTP/1.1\nAuthorization: Basic bml0bzpuaXRv\nConnection: Upgrade\nUpgrade: QVD/1.0\n\n"; */
  if ((qvd->res = curl_easy_send(qvd->curl, url, strlen(url) , &bytes_sent )) != CURLE_OK ) {
    qvd_error(qvd, "An error ocurred in first curl_easy_send: %ul <%s>\n", qvd->res, curl_easy_strerror(qvd->res));
    return 1;
  }


  FD_ZERO(&myset);
  FD_ZERO(&zero);
  FD_SET(socket, &myset);
  qvd_printf("Before select on send socket is: %d\n", socket);
  for (i=0; i<MAX_HTTP_RESPONSES_FOR_UPGRADE; ++i) {
    /* TODO define timeouts perhaps in qvd_init */
    select(socket+1, &myset, &zero, &zero, NULL);
    if ((qvd->res = curl_easy_recv(qvd->curl, qvd->buffer.data, BUFFER_SIZE, &bytes_sent)) != CURLE_OK ) {
      qvd_error(qvd, "An error ocurred in curl_easy_recv: %ul <%s>\n", qvd->res, curl_easy_strerror(qvd->res));
      return 2;
    }
    qvd->buffer.data[bytes_sent] = 0;
    qvd_printf("%d input received was <%s>\n", i, qvd->buffer.data);
    if (strstr(qvd->buffer.data, "HTTP/1.1 101")) {
      qvd_printf("Upgrade of protocol was done\n");
      break;
    }
  }
  if (i >=10 ) {
    qvd_error(qvd, "Error not received response for protocol upgrade in %d tries http/1.1\n", i);
    return 3;
  }

  return 0;
}

void _qvd_print_environ()
{
  if (environ == NULL)
    qvd_printf("Environment variable not defined (NULL)");
  char **ptr;
  qvd_printf("Printing environment variables\n");
  for (ptr=environ; *ptr != NULL; ptr ++)
      qvd_printf("Environment var %s\n", *ptr);

}


/*
 * Internal funcs for qvd_init callbacks for qvd_list_of_vm
 * Really generic
 */


/* arrays for certificate chain and errors */
#define MAX_CERTS 20
X509 *certificate[MAX_CERTS];
long certificate_error[MAX_CERTS]; 

int _qvd_create_dir(qvdclient *qvd, const char *home, const char *subdir)
{
  char path[MAXCERTSTRING];
  struct stat fs_stat;
  int result;
  snprintf(path, MAXCERTSTRING - 1, "%s/%s", home, subdir);
  path[MAXCERTSTRING - 1] = '\0';
  result = stat(path, &fs_stat);
  if (result == -1)
    {
      if (errno != ENOENT)
	{
	  qvd_error(qvd, "Error accessing directory $HOME/%s (%s), with error: %s\n", subdir, path, strerror(errno));
	  return 0;
	}
      result = mkdir(path, 0755);
      if (result)
	{
	  qvd_error(qvd, "Error creating directory $HOME/%s (%s), with error: %s\n", subdir, path, strerror(errno));
	  return 0;
	}
      return 1;
    }
  if (!S_ISDIR(fs_stat.st_mode))
    {
      qvd_error("Error accessing dir $HOME/%s (%s) the file is not a directory", subdir, path);
      return 0;
    }
  return 1;
}
int _qvd_save_certificate(qvdclient *qvd, X509 *cert, int depth, BUF_MEM *biomem)
{
  char path[MAXCERTSTRING];

  int fd, result;
  snprintf(path, MAXCERTSTRING - 1, "%s/%lx.%d", qvd->certpath, X509_subject_name_hash(cert), depth);
  path[MAXCERTSTRING - 1] = '\0';
  if (strlen(path) == MAXCERTSTRING)
    {
      qvd_error(qvd, "Cert string too long (%d) recompile program. Path is %s", MAXCERTSTRING, path);
      return 0;
    }

  fd = open(path, O_CREAT|O_TRUNC|O_WRONLY, 0644);
  if (fd == -1)
    {
      qvd_error(qvd, "Error creating file %s: %s", path, strerror(errno));
      return 0;
    }

  result = write(fd, biomem->data, strlen(biomem->data));
  if (result == -1)
    {
      qvd_error(qvd, "Error writing file %s: %s", path, strerror(errno));
      return 0;
	}
  if (result != strlen(biomem->data))
    {
      qvd_error(qvd, "Error writing file not enough bytes written in %s: %d vs %d", path, result, strlen(biomem->data));
      return 0;
    }

  result = close(fd);
  if (result == -1)
    {
	  qvd_error(qvd, "Error closing file %s: %s", path, strerror(errno));
	  return 0;
    }
  qvd_printf("Successfully saved cert in %s\n", path);
  return 1;
}

int _qvd_verify_cert_callback(int preverify_ok, X509_STORE_CTX *x509_ctx)
{
  
  SSL    *ssl;
  SSL_CTX *sslctx;
  qvdclient *qvd ;

  ssl = X509_STORE_CTX_get_ex_data(x509_ctx, SSL_get_ex_data_X509_STORE_CTX_idx());
  sslctx = SSL_get_SSL_CTX(ssl);
  qvd = SSL_CTX_get_ex_data(sslctx, _qvd_ssl_index); 
 
  X509 *cert = X509_STORE_CTX_get_current_cert(x509_ctx);
  int depth = X509_STORE_CTX_get_error_depth(x509_ctx);
  int err = X509_STORE_CTX_get_error(x509_ctx);

  /* save the certificate by incrementing the reference count and
   * keeping a pointer */
  if (depth < MAX_CERTS && !certificate[depth]) {
    certificate[depth] = cert;
    certificate_error[depth] = err;
    cert->references++;
  }

  /* See http://www.openssl.org/docs/ssl/SSL_CTX_set_verify.html# */
  if (preverify_ok)
    {
      qvd_printf("_qvd_verify_cert_callback: Certificate was validated\n");
      return preverify_ok;
    }
  if (qvd->ssl_verify_callback == NULL)
    {
      qvd_printf("_qvd_verify_cert_callback: No callback specified returning false (specify if you wissh callbacks for unknown certs with qvd_set_unknown_cert_callback)\n");
      return 0;
    }

  BIO *bio_out = BIO_new(BIO_s_mem());
  BUF_MEM *biomem;
  int result;
  PEM_write_bio_X509(bio_out, certificate[depth]);
  BIO_get_mem_ptr(bio_out, &biomem);
  char cert_info[1024];
  char issuer[256], subject[256];
  X509_NAME_oneline(X509_get_issuer_name(certificate[depth]), issuer, 256);
  X509_NAME_oneline(X509_get_subject_name(certificate[depth]), subject, 256);

  snprintf(cert_info, 1023, "Serial: %lu\n\nIssuer: %s\n\nValidity:\n\tNot before: %s\n\tNot after: %s\n\nSubject: %s\n",
	   ASN1_INTEGER_get(X509_get_serialNumber(certificate[depth])), issuer, 
	   X509_get_notBefore(certificate[depth])->data, X509_get_notAfter(cert)->data, subject);
  cert_info[1023] = '\0';
  result = qvd->ssl_verify_callback(qvd, cert_info, biomem->data);
  if (result)
    {
      _qvd_save_certificate(qvd, certificate[depth], depth, biomem);
    }

  BIO_free(bio_out);
  return result;
}

CURLcode _qvd_sslctxfun(CURL *curl, SSL_CTX *sslctx, void *parm)
{

  qvdclient *qvd = (qvdclient *) parm;
  if (qvd->ssl_no_cert_check)
    {
      qvd_printf("No strict certificate checking. Accepting any server certificate\n");
      return CURLE_OK;
    }
  /* See SSL_set_ex_data and http://www.openssl.org/docs/ssl/SSL_get_ex_new_index.htm*/
  /* parm is qvdclient *qvd, the qvd object, set with  CURLOPT_SSL_CTX_DATA */
  SSL_CTX_set_ex_data(sslctx, _qvd_ssl_index, parm);

  SSL_CTX_set_verify(sslctx, SSL_VERIFY_PEER, _qvd_verify_cert_callback);

  return CURLE_OK;
} 


