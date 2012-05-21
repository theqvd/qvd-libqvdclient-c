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

#ifdef STRICTSSL
/* See http://www.openssl.org/docs/ssl/SSL_get_ex_new_index.html# */
static int _qvd_ssl_index;
#endif


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

  qvd->curl = curl_easy_init();
  if (!qvd->curl) {
    qvd_error(qvd, "Error initializing curl\n");
    free(qvd);
    return NULL;
  }
  if (get_debug_level()) 
    curl_easy_setopt(qvd->curl, CURLOPT_VERBOSE, 1L);
  /* TODO fix ssl settings */
  /* Set CURLOPT_SSL_VERIFYPEER to 1 (default) */
  /* Set CURLOPT_SSL_VERIFYHOST to 2 (default, certificate issuer must match and match cn) */
  /* Set CURLOPT_SSL_CAPATH to a location of the path with certificates */
  /* Set CURLOPT_CERTINFO to 1 (be able to get certificate info with curl_easy_getinfo and CURLINFO_CERTINFO) */
  /* Set up also a function in qvdclient.h to check for certificateerror if null was returned */

#ifdef STRICTSSL
  /* curl_easy_setopt(qvd->curl, CURLOPT_SSL_VERIFYPEER, 1L); */
  /* curl_easy_setopt(qvd->curl, CURLOPT_SSL_VERIFYHOST, 2L); */
  curl_easy_setopt(qvd->curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(qvd->curl, CURLOPT_SSL_VERIFYHOST, 0L);
  curl_easy_setopt(qvd->curl, CURLOPT_CERTINFO, 1L);
  curl_easy_setopt(qvd->curl, CURLOPT_CAPATH, "/home/nito/.qvd/certs");
  curl_easy_setopt(qvd->curl, CURLOPT_SSL_CTX_FUNCTION, _qvd_sslctxfun);
  curl_easy_setopt(qvd->curl, CURLOPT_SSL_CTX_DATA, (void *)qvd);
  /*  curl_easy_setopt(qvd->curl, CURLOPT_CAINFO, NULL);*/
  _qvd_ssl_index = SSL_CTX_get_ex_new_index(0, (void *)qvd, NULL, NULL, NULL);
#else
  curl_easy_setopt(qvd->curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(qvd->curl, CURLOPT_SSL_VERIFYHOST, 0L);
#endif
  curl_easy_setopt(qvd->curl, CURLOPT_TCP_NODELAY, 1L);
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
  free(qvd);
}

vmlist *qvd_list_of_vm(qvdclient *qvd) {
  char url[MAX_BASEURL];
  int i;
  json_error_t error;
  char *command = "/qvd/list_of_vm";

  if (snprintf(url, MAX_BASEURL, "%s%s", qvd->baseurl, command) >= MAX_BASEURL) {
    qvd_error(qvd, "Error initializing url in list_of_vm, length is longer than %d\n", MAX_BASEURL);
    return NULL;
  }

  curl_easy_setopt(qvd->curl, CURLOPT_URL, url);
  /*  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &jsonBuffer); */
  qvd->res = curl_easy_perform(qvd->curl);
  if (qvd->res) {

#ifdef STRICTSSL
    qvd_printf("STRICTSSL is defined\n");
#endif

    /*    qvd_error(qvd, "An error ocurred getting url <%s>: %d <%s>\n", url, qvd->res, curl_easy_strerror(qvd->res));*/
    qvd_error(qvd, "An error ocurred getting url <%s>: %d\n", url, qvd->res);
    /*    qvd_printf("An error ocurred getting url <%s>: %d\n", url, qvd->res);*/
    struct curl_certinfo certinfo;
    curl_easy_getinfo(qvd->curl, CURLINFO_CERTINFO, &certinfo);
    qvd_printf("Something\n");
    qvd_printf("Number of certs: %d\n", certinfo.num_of_certs);
    return NULL;
  }

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
    qvd_error(qvd, "No virtual machines available for user %s", qvd->username);
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

void qvd_set_unknown_cert_callback(qvdclient *qvd, int (*ssl_verify_callback)(const char *cert_pem_str, const char *cert_pem_data))
{
  qvd->ssl_verify_callback = ssl_verify_callback;
}

/*
 * Internal funcs for qvd_init
 */
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
    if (NXTransCreate(proxyPair[0], NX_MODE_SERVER, NULL) < 0)
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
		/* TODO if cul recvd is 0 then end */
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
	    if (res != CURLE_OK)
	      {
		qvd_error(qvd, "Error sending curl_easy_send: %d", res);
		connFd = -1;
	      }
	    else
	      {
		proxyRead.offset += written;
#ifdef TRACE
		qvd_printf("curl: send'd %ld\n", written);
#endif
		if (proxyRead.offset >= proxyRead.size) {
		  QvdBufferReset(&proxyRead);
		}
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
    qvd_error(qvd, "An error ocurred in first curl_easy_send: %d <%s>\n", qvd->res, curl_easy_strerror(qvd->res));
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
      qvd_error(qvd, "An error ocurred in curl_easy_recv: %d <%s>\n", qvd->res, curl_easy_strerror(qvd->res));
      return 2;
    }
    qvd->buffer.data[bytes_sent] = 0;
    qvd_printf(qvd, "%d input received was <%s>\n", i, qvd->buffer.data);
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

#ifndef ANDROID
extern char **environ;
#endif

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

#ifdef STRICTSSL

/* arrays for certificate chain and errors */
#define MAX_CERTS 20
X509 *certificate[MAX_CERTS];
long certificate_error[MAX_CERTS]; 

/* static void _qvd_dumpcert(X509 *x) */
/* { */
/*   BIO *bio_out = BIO_new(BIO_s_mem()); */
/*   BUF_MEM *biomem; */

/*   /\* this outputs the cert in this 64 column wide style with newlines and */
/*      -----BEGIN CERTIFICATE----- texts and more *\/ */
/*   PEM_write_bio_X509(bio_out, x); */

/*   BIO_get_mem_ptr(bio_out, &biomem); */

/*   qvd_printf("Cert:\n%s\n", biomem->data); */

/*   /\*  push_certinfo_len(data, numcert, "Cert", biomem->data, biomem->length);*\/ */

/*   BIO_free(bio_out); */

/* } */


/* void _qvd_print_certificate(X509 *cert) */
/* { */
/*   char s[256]; */
/*   qvd_printf("Printing certificate info:\n"); */

/*   qvd_printf(" version %li\n", X509_get_version(cert)); */
/*   qvd_printf(" not before %s\n", X509_get_notBefore(cert)->data); */
/*   qvd_printf(" not after %s\n", X509_get_notAfter(cert)->data); */
/*   qvd_printf(" signature type %i\n", X509_get_signature_type(cert)); */
/*   qvd_printf(" serial no %ld\n", */
/*       ASN1_INTEGER_get(X509_get_serialNumber(cert))); */
/*   X509_NAME_oneline(X509_get_issuer_name(cert), s, 256); */
/*   qvd_printf(" issuer %s\n", s); */
/*   X509_NAME_oneline(X509_get_subject_name(cert), s, 256); */
/*   qvd_printf(" subject %s\n", s); */
/*   qvd_printf(" cert type %i\n", */
/*       X509_certificate_type(cert, X509_get_pubkey(cert))); */
/*   qvd_printf(" subject hash %ul\n", */
/*       X509_subject_name_hash(cert)); */
/*   qvd_printf(" subject hash 0x%x (hex)\n", */
/*       X509_subject_name_hash(cert)); */
/*   _qvd_dumpcert(cert); */
  
/* }  */


int _qvd_save_certificate(qvdclient *qvd, X509 *cert, int depth)
{
  char path[1024];
  char *home = getenv("HOME");
  struct stat fs_stat;
  int result;
  if (home == NULL)
    {
      qvd_error(qvd, "Error HOME environment var is not defined, cannot save to $HOME/.qvd/certs");
      return 0;
    }
  snprintf(path, 1023, "%s/%s", home, ".qvd");
  path[1023] = '\0';
  result = stat(path, &fs_stat);
  if (result == -1)
    {
      if (errno != ENOENT)
	{
	  qvd_error(qvd, "Error accessing directory $HOME/.qvd (%s), with error: %s\n", path, strerror(errno));
	  return 0;
	}
      result = mkdir(path, 0);
      if (result)
	{
	  qvd_error(qvd, "Error creating directory $HOME/.qvd (%s), with error: %s\n", path, strerror(errno));
	  return 0;
	}
    }

  /* Define .qvd/certs in qvdclient.h */
  snprintf(path, 1023, "%s/%s", home, ".qvd/certs");
  path[1023] = '\0';
  result = stat(path, &fs_stat);
  if (result == -1)
    {
      if (errno != ENOENT)
	{
	  qvd_error(qvd, "Error accessing directory $HOME/.qvd (%s), with error: %s\n", path, strerror(errno));
	  return 0;
	}
      result = mkdir(path, 0);
      if (result)
	{
	  qvd_error(qvd, "Error creating directory $HOME/.qvd (%s), with error: %s\n", path, strerror(errno));
	  return 0;
	}
    }

  int fd;
  snprintf(path, 1023, "%s/%s/%lx.%d", home, ".qvd/certs", X509_subject_name_hash(cert), depth);
  path[1023] = '\0';
  /* TODO save certificate open + write*/
  fd = open(path, O_CREAT|O_TRUNC|O_WRONLY, 0644);
  if (fd == -1)
    {
      qvd_error(qvd, "Error creating file %s: %s", path, strerror(errno));
      return 0;
    }
  /* TODO write pem data, reuse method for _qvd_dumpcert in several places*/ 
  BIO *bio_out = BIO_new(BIO_s_mem());
  BUF_MEM *biomem;
  PEM_write_bio_X509(bio_out, cert);
  BIO_get_mem_ptr(bio_out, &biomem);
  
  result = write(fd, biomem->data, strlen(biomem->data));
  if (result == -1)
    {
      qvd_error(qvd, "Error writing file %s: %s", path, strerror(errno));
      BIO_free(bio_out);
      return 0;
	}
  if (result != strlen(biomem->data))
    {
      qvd_error(qvd, "Error writing file not enough bytes written in %s: %d vs %d", path, result, strlen(biomem->data));
      BIO_free(bio_out);
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
  /*  qvd_printf("The qvd parameter received in _qvd_verify_cert_callback has user: %s\n", qvd->username);*/
 
  X509 *cert = X509_STORE_CTX_get_current_cert(x509_ctx);
  int depth = X509_STORE_CTX_get_error_depth(x509_ctx);
  int err = X509_STORE_CTX_get_error(x509_ctx);

  /* save the certificate by incrementing the reference count and
   * keeping a pointer */
  if (depth < MAX_CERTS && !certificate[depth]) {
    certificate[depth] = cert;
    certificate_error[depth] = err;
    /* _qvd_print_certificate(certificate[depth]); */    
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
  result = qvd->ssl_verify_callback(cert_info, biomem->data);
  if (result)
    {
      _qvd_save_certificate(qvd, certificate[depth], depth);
    }

  BIO_free(bio_out);
  return result;
}

CURLcode _qvd_sslctxfun(CURL *curl, SSL_CTX *sslctx, void *parm)
{
  qvdclient *qvd = (qvdclient *) parm;
  /*qvd_printf("The qvd parameter received in sslctxfun has user: %s\n", qvd->username);*/
  /* TODO Call SSL_set_ex_data and http://www.openssl.org/docs/ssl/SSL_get_ex_new_index.htm*/
  /* parm is qvdclient *qvd, the qvd object, set with  CURLOPT_SSL_CTX_DATA */
  /* TODO get SSL * from SSL_CTX * */
  /*  SSL_set_ex_data(SSL *ssl, _qvd_ssl_index, parm);  */
  /*  or SSL_CTX_set_ex_data */
  SSL_CTX_set_ex_data(sslctx, _qvd_ssl_index, parm);

  SSL_CTX_set_verify(sslctx, SSL_VERIFY_PEER, _qvd_verify_cert_callback);

  return CURLE_OK;
} 

#endif


