#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
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
/*CURL *curl;*/
int proxyConnect(qvdclient *qvd)
{
  int proxyPair[2];
  if (socketpair(PF_UNIX, SOCK_STREAM, 0, proxyPair) < 0)
    {
      qvd_error(qvd, "Error creating proxy socket <%s>\n", strerror(errno));
      return -1;
    }
  /*  if (NXTransCreate(proxyPair[0], NX_MODE_SERVER, "nx/nx,data=0,delta=0,cache=16384,pack=0:0") < 0)*/
  if (NXTransCreate(proxyPair[0], NX_MODE_SERVER, "nx/nx,cache=16384:0") < 0)
    {
      qvd_error(qvd, "Error creating proxy transport <%s>\n", strerror(errno));
      return -1;
    }
  return proxyPair[1];
}

/*
 * qvdClientLoop
 *            -----------------
 *            |               |
 * proxyFd ---| qvdClientLoop |---connFd            
 * (X display)|               | (curl to remote host)
 *            -----------------
 * 
 *       -----   proxyRead  ---->
 *      <-----   proxyWrite ----
 *
 * We read from proxyFd and store it in the proxyRead buffer and then write it into connFd (curl)
 * We read from connFd and store it in the proxyWrite buffer and then write it ingo proxyFd (NX)
 *
 */
int qvdClientLoop(qvdclient *qvd, int connFd, int proxyFd)
{
  qvd_printf("qvdClientLoop\n");
  struct timeval timeout;
  fd_set rfds;
  fd_set wfds;
  int ret, err, maxfds;
  QvdBuffer proxyWrite, proxyRead;
  qvd_printf("qvdClientLoop(%p, %d, %d)\n", qvd, connFd, proxyFd);
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
	  qvd_printf("qvdClientLoop: executing select()\n");
#endif
	  NXTransSelect(&ret, &err, &maxfds, &rfds, &wfds, &timeout);
	  NXTransExecute(&ret, &err, &maxfds, &rfds, &wfds, &timeout);
	}
      if (ret == -1 && errno == EINTR)
	continue;

      if (ret < 0)
	{
	  qvd_error("Error in qvdClientLoop: select() %s\n", strerror(errno));
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

size_t
WriteBufferCallback(void *contents, size_t size, size_t nmemb, void *buffer) {
    size_t realsize = size*nmemb;
    size_t bytes_written = QvdBufferAppend((QvdBuffer*)buffer, contents, realsize);
    return bytes_written;
}

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
    qvd_error(qvd, "Error initializing userpwd\n");
    free(qvd);
    return NULL;
  }
  if (setBase64Auth(qvd)) {
    qvd_error(qvd, "Error initializing authdigest\n");
    free(qvd);
    return NULL;
    }

  if (snprintf(qvd->baseurl, MAX_BASEURL, "https://%s:%d", hostname, port) >= MAX_BASEURL) {
    qvd_error(qvd, "Error initializing baseurl\n");
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


  curl_easy_setopt(qvd->curl, CURLOPT_TCP_NODELAY, 1L);
  curl_easy_setopt(qvd->curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(qvd->curl, CURLOPT_SSL_VERIFYHOST, 0L);
  curl_easy_setopt(qvd->curl, CURLOPT_HTTPAUTH, (long)CURLAUTH_BASIC);
  curl_easy_setopt(qvd->curl, CURLOPT_USERPWD, qvd->userpwd);
  curl_easy_setopt(qvd->curl, CURLOPT_WRITEFUNCTION, WriteBufferCallback);
  curl_easy_setopt(qvd->curl, CURLOPT_WRITEDATA, &(qvd->buffer));

  /* Copy parameters */
  strncpy(qvd->hostname, hostname, MAX_BASEURL);
  qvd->hostname[MAX_BASEURL - 1] = '\0';
  qvd->port = port;
  strncpy(qvd->username, username, MAX_USERPWD);
  qvd->username[MAX_USERPWD - 1] = '\0';
  strncpy(qvd->password, password, MAX_USERPWD);
  qvd->password[MAX_USERPWD - 1] = '\0';
  qvd->numvms = 0;
  qvd->link = "adsl";
  qvd->geometry = "800x600";
  qvd->os = "linux";
  qvd->keyboard = "pc%2F105";
  qvd->fullscreen = 0;
  qvd->print_enabled = 0;
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
    qvd_error(qvd, "An error ocurred getting url <%s>: %d <%s>\n", url, qvd->res, curl_easy_strerror(qvd->res));
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

int setBase64Auth(qvdclient *qvd) {

  CURLcode error;
  char *ptr = NULL;
  size_t outlen;
  int result = 0;
  error = Curl_base64_encode((struct SessionHandle *) qvd,
                             qvd->userpwd, strlen(qvd->userpwd),
                             &ptr, &outlen);
  if (error != CURLE_OK) {
    qvd_error(qvd, "Error in getBase64Auth");
    return 1;
  }
  
  if (snprintf(qvd->authdigest, MAX_AUTHDIGEST, "%s", ptr) >= MAX_AUTHDIGEST) {
    qvd_error(qvd, "The authdigest string for %s is longer than %d\n", qvd->userpwd, MAX_AUTHDIGEST);
    result = 1;
  } else {
    qvd_printf("The conversion to base64 from <%s> is <%s>", qvd->userpwd, qvd->authdigest);
    result = 0;
  }
  free(ptr);
  
  /* hack for base64 encode of "nito@deiro.com:O3xTMCQ3" */
  /*  snprintf(qvd->authdigest, MAX_AUTHDIGEST, "%s", "bml0b0BkZWlyby5jb206TzN4VE1DUTM=");*/
  return result;
}


int switch_protocols(qvdclient *qvd, int id) {
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


  result = switch_protocols(qvd, id);
  _qvd_print_environ();
  /* if non zero return with error */
  if (result)
    return result;

  curl_easy_getinfo(qvd->curl, CURLINFO_LASTSOCKET, &curlsock);  
  fd = (int) curlsock;

  if ((proxyFd = proxyConnect(qvd)) < 0)
    return 4;

  qvd_printf("Remote fd: %d Local fd: %d\n", fd, proxyFd);
  qvd_printf("Before qvdClientLoop\n");
  qvdClientLoop(qvd, fd, proxyFd);
  shutdown(proxyFd, 2);
  return 0;
}

/*
 * TODO set general way to set options
 */
void qvd_set_geometry(qvdclient *qvd, const char *geometry) {
  qvd_printf("Setting geometry to %s", geometry);
  qvd->geometry = geometry;
}
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
