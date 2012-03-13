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

int proxyConnect()
{
  int proxyPair[2];
  if (socketpair(PF_UNIX, SOCK_STREAM, 0, proxyPair) < 0)
    {
      qvd_printf("Creating proxy socket <%s>\n", strerror(errno));
      return -1;
    }
  if (NXTransCreate(proxyPair[0], NX_MODE_SERVER, "nx/nx,data=0,delta=0,cache=16384,pack=0:0") < 0)
    {
      qvd_printf("Creating proxy transport <%s>\n", strerror(errno));
      return -1;
    }
  return proxyPair[1];
}

/*
 * clientLoop
 *            --------------
 *            |            |
 * proxyFd ---| clientLoop |---connFd            
 * (X display)|            | (curl to remote host)
 *            --------------
 * 
 *       -----   proxyRead  ---->
 *      <-----   proxyWrite ----
 *
 * We read from proxyFd and store it in the proxyRead buffer and then write it into connFd (curl)
 * We read from connFd and store it in the proxyWrite buffer and then write it ingo proxyFd (NX)
 *
 */
int clientLoop(qvdclient *qvd, int connFd, int proxyFd)
{
  struct timeval timeout;
  fd_set rfds;
  fd_set wfds;
  int ret, err, maxfds;
  QvdBuffer proxyWrite, proxyRead;
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
	  qvd_printf("clientLoop: executing select()\n");
#endif
	  NXTransSelect(&ret, &err, &maxfds, &rfds, &wfds, &timeout);
	  NXTransExecute(&ret, &err, &maxfds, &rfds, &wfds, &timeout);
	}
      if (ret == -1 && errno == EINTR)
	continue;

      if (ret < 0)
	{
	  qvd_printf("clientLoop: select() %s\n", strerror(errno));
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
		qvd_printf("Error receiving curl_easy_recv: %d\n", res);
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
		qvd_printf("proxyFd read error: %s\n", strerror(errno));
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
		qvd_printf("Error sending curl_easy_send: %d", res);
		connFd = -1;
	      }
	    else
	      {
		proxyRead.offset += written;
		qvd_printf("curl: send'd %ld\n", written);
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
	      qvd_printf("Error reading from proxyFd: %d %s\n", errno, strerror(errno));
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
    qvd_printf("Length of username and password + 2 is longer than %d\n", MAX_USERPWD);
    return NULL;
  }

  if (strlen(hostname) + 6 + strlen("https:///") + 2 > MAX_BASEURL) {
    qvd_printf("Length of hostname and port + scheme  + 2 is longer than %d\n", MAX_BASEURL);
    return NULL;
  }

  if (! (qvd = (qvdclient *) malloc(sizeof(qvdclient)))) {
    qvd_printf("Error allocating memory: %s", strerror(errno));
    return NULL;
  }
  
  if (snprintf(qvd->userpwd, MAX_USERPWD, "%s:%s", username, password) >= MAX_USERPWD) {
    qvd_printf("Error initializing userpwd\n");
    free(qvd);
    return NULL;
  }
  if (setBase64Auth(qvd)) {
    qvd_printf("Error initializing authdigest\n");
    free(qvd);
    return NULL;
    }

  if (snprintf(qvd->baseurl, MAX_BASEURL, "https://%s:%d", hostname, port) >= MAX_BASEURL) {
    qvd_printf("Error initializing baseurl\n");
    free(qvd);
    return NULL;
  }
  qvd->curl = curl_easy_init();
  if (!qvd->curl) {
    qvd_printf("Error initializing curl\n");
    free(qvd);
    return NULL;
  }
  if (get_debug_level()) 
    curl_easy_setopt(qvd->curl, CURLOPT_VERBOSE, 1L);
  /*curl_easy_setopt(qvd->curl, CURLOPT_ERRORBUFFER, qvd->error_buffer);*/
  /* TODO fix ssl settings */

  curl_easy_setopt(qvd->curl, CURLOPT_TCP_NODELAY, 1L);
  curl_easy_setopt(qvd->curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(qvd->curl, CURLOPT_SSL_VERIFYHOST, 0L);
  curl_easy_setopt(qvd->curl, CURLOPT_HTTPAUTH, (long)CURLAUTH_BASIC);
  curl_easy_setopt(qvd->curl, CURLOPT_USERPWD, qvd->userpwd);
  curl_easy_setopt(qvd->curl, CURLOPT_WRITEFUNCTION, WriteBufferCallback);
  curl_easy_setopt(qvd->curl, CURLOPT_WRITEDATA, &(qvd->buffer));

  /* Copy parameters */
  qvd->hostname = hostname;
  qvd->port = port;
  qvd->username = username;
  qvd->password = password;
  qvd->numvms = 0;
  qvd->link = "adsl";
  qvd->geometry = "800x600";
  qvd->os = "linux";
  qvd->keyboard = "pc%2F105";
  qvd->fullscreen = 0;
  qvd->print_enabled = 0;

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
  /*  QvdBuffer jsonBuffer;
  QvdBufferInit(&jsonBuffer);
  */
  if (snprintf(url, MAX_BASEURL, "%s%s", qvd->baseurl, command) >= MAX_BASEURL) {
    qvd_printf("Error initializing url in list_of_vm, length is longer than %d\n", MAX_BASEURL);
    return NULL;
  }

  curl_easy_setopt(qvd->curl, CURLOPT_URL, url);
  /*  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &jsonBuffer); */
  qvd->res = curl_easy_perform(qvd->curl);
  if (qvd->res) {
    qvd_printf("An error ocurred geting url <%s>: %d <%s>\n", url, qvd->res, curl_easy_strerror(qvd->res));
    return NULL;
  }

  qvd->buffer.data[qvd->buffer.size] = 0;
  json_t *vmList = json_loads(qvd->buffer.data, 0, &error);
  int arrayLength = json_array_size(vmList);
  qvd_printf("VMs available: %d\n", arrayLength);

  qvd->numvms = arrayLength;
  QvdVmListFree(qvd->vmlist);
  if (!(qvd->vmlist = malloc(sizeof(vmlist)))) {
    qvd_printf("Error allocating memory for vmlist\n");
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
    QvdVmListAppendVm(qvd->vmlist, QvdVmNew(id, name, state, blocked));
  }
  QvdBufferReset(&(qvd->buffer));

  /* TODO return the list of ids of vms , parsing of JSON*/

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
    qvd_printf("Error in getBase64Auth");
    return 1;
  }
  
  if (snprintf(qvd->authdigest, MAX_AUTHDIGEST, "%s", ptr) >= MAX_AUTHDIGEST) {
    qvd_printf("The authdigest string for %s is longer than %d\n", qvd->userpwd, MAX_AUTHDIGEST);
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
  if (snprintf(url, MAX_BASEURL, "GET /qvd/connect_to_vm?id=%d&qvd.client.os=%s&qvd.client.geometry=%s&qvd.client.link=%s&qvd.client.keyboard=%s HTTP/1.1\nAuthorization: Basic %s\nConnection: Upgrade\nUpgrade: QVD/1.0\n\n", id, qvd->os, qvd->geometry, qvd->link, qvd->keyboard, qvd->authdigest) >= MAX_BASEURL) {
    qvd_printf("Error initializing authdigest\n");
    return 1;
  }
  qvd_printf("Switch protocols the url is: <%s>\n", url);

  /*  char *url = "GET /qvd/connect_to_vm?id=1&qvd.client.os=linux&qvd.client.fullscreen=&qvd.client.geometry=800x600&qvd.client.link=local&qvd.client.keyboard=pc105%2Fus&qvd.client.printing.enabled=0 HTTP/1.1\nAuthorization: Basic bml0bzpuaXRv\nConnection: Upgrade\nUpgrade: QVD/1.0\n\n"; */
  if ((qvd->res = curl_easy_send(qvd->curl, url, strlen(url) , &bytes_sent )) != CURLE_OK ) {
    qvd_printf("An error ocurred in first curl_easy_send: %d <%s>\n", qvd->res, curl_easy_strerror(qvd->res));
    return 1;
  }


  FD_ZERO(&myset);
  FD_ZERO(&zero);
  FD_SET(socket, &myset);
  qvd_printf("Before select on send socket is: %d\n", socket);
  for (i=0; i<10; ++i) {
    /* TODO define timeouts perhaps in qvd_init */
    select(socket+1, &myset, &zero, &zero, NULL);
    if ((qvd->res = curl_easy_recv(qvd->curl, qvd->buffer.data, BUFFER_SIZE, &bytes_sent)) != CURLE_OK ) {
      qvd_printf("An error ocurred in curl_easy_recv: %d <%s>\n", qvd->res, curl_easy_strerror(qvd->res));
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
    qvd_printf("Error not received http/1.1\n");
    return 3;
  }

  return 0;
}


int qvd_connect_to_vm(qvdclient *qvd, int id)
{
  int result, proxyFd, fd;
  long curlsock;

  result = switch_protocols(qvd, id);
  /* if non zero return with error */
  if (result)
    return result;

  curl_easy_getinfo(qvd->curl, CURLINFO_LASTSOCKET, &curlsock);  
  fd = (int) curlsock;

  if ((proxyFd = proxyConnect()) < 0)
    return 4;

  qvd_printf("Remote fd: %d Local fd: %d\n", fd, proxyFd);
  clientLoop(qvd, fd, proxyFd);
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
