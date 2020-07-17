/*
 * libqvdclient qvdclient.c
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
 *
 */
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <openssl/ssl.h>
#include <signal.h>
#include "qvdclient.h"

void help(const char *program)
{
  printf("%s [-?] [-d] -h host [-p port] [-b bearer|-u username -w pass] [-g wxh] [-f] [-s n]\n\n"
         "  -? : shows this help\n"
         "  -v : shows version and exits\n"
	 "  -d : Enables debugging\n"
	 "  -h : indicates the host to connect to. You can also set it up in the env var %s.\n"
	 "       The command line argument takes precedence, if specified\n"
	 "  -p : indicates the port to connect to, if not specified 8443 is used. You can also set it up in the env var %s.\n"
	 "       The command line argument takes precedence, if specified\n"
	 "  -u : indicates the username for the connection. You can also set it up in the env var %s\n"
	 "       The command line argument takes precedence, if specified\n"
	 "  -w : indicates the password for the user. You can also set it up in the env var %s\n"
	 "       The command line argument takes precedence, if specified\n"
	 "  -b : indicates the bearer that will be used to stablish the connection. You can also set it up in the env var %s\n"
	 "       The command line argument takes precedence, if specified\n"
	 "       In this case the virtual machine must be provided with option -s\n"
	 "  -g : indicates the geometry wxh. Example -g 1024x768\n"
	 "  -f : Use fullscreen\n"
	 "  -l : Use only list_of_vm (don't try to connect, useful for debugging)\n"
	 "  -o : Assume One VM, that is connect always to the first VM (useful for debugging)\n"
	 "  -n : No strict certificate checking, always accept certificate\n"
	 "  -x : NX client options. Example: nx/nx,data=0,delta=0,cache=16384,pack=0:0\n"
	 "       Example: nx/nx,media=4713,cups=631,limit=5m,images=268435456,cache=67108864:0\n"
	 "  -K : indicates the desired keyboard layout. Example -K pc105/es. Default: pc105/en\n"
	 "  -c : Specify client certificate (PEM), it requires also -k. Example -c $HOME/.qvd/client.crt -k $HOME/.qvd/client.key\n"
	 "  -k : Specify client certificate key (PEM), requires -c. Example $HOME/.qvd/client.crt -k $HOME/.qvd/client.key\n"
	 "  -r : Restart session. That is stop the VM before issuing a vm_connect\n"
	 "  -2 : Specify to reconnect after the connection has finished. This is for testing only.\n"
	 "  -s : Select the specific vm id. Usually used for testing. You can also set it up in the env var %s.\n"
	 "       The command line argument takes precedence, if specified\n"
	 "\n"
	 "Environment variables:\n"
	 "  %-14s : Specifies the host to connect to, if not specified with -h\n"
	 "  %-14s : Specifies the port, if not specified with -p\n"
	 "  %-14s : Specifies the username, if not specified with -u\n"
	 "  %-14s : Specifies the password, if not specified with -w\n"
	 "  %-14s : Specifies the the specific vm id, if not specified with -s\n"
	 "  %-14s : Specifies the bearer, if not specified with -b\n"
	 "  %-14s : Enables debugging, can also be enabled with -d\n"
	 "  %-14s : Enables the file were debugging should go to\n"
	 "  http_proxy/https_proxy: Optional variables to use for proxy settings\n"
     "       See http://curl.haxx.se/docs/manpage.html for more information\n"
	 "  DISPLAY : Needed to be correctly setup. In some environments you might need to run one of the following:\n"
	 "            export DISPLAY=localhost:0; xhost + localhost\n"
	 "            xhost +si:localuser:$LOGNAME\n"
	 "\nChangelog:\n%s\n",
	 program, QVDHOST_ENV, QVDPORT_ENV, QVDLOGIN_ENV, QVDPASSWORD_ENV, QVDBEARER_ENV, QVDVMID_ENV,
	 QVDHOST_ENV, QVDPORT_ENV, QVDLOGIN_ENV, QVDPASSWORD_ENV, QVDVMID_ENV, QVDBEARER_ENV, DEBUG_FLAG_ENV_VAR_NAME, DEBUG_FILE_ENV_VAR_NAME,
	 QVDCHANGELOG
	 );
}

int getintenv(const char *env, int defaultvalue) {
    int parsedvalue;
    char *parsedstr = getenv(env);
    if (parsedstr == NULL)
        return defaultvalue;

    errno = 0;
    parsedvalue = strtol(parsedstr, NULL, 0);
    if (errno != 0) {
        return defaultvalue;
    }

    return parsedvalue;
}

int parse_params(int argc, char **argv, const char **host, int *port, const char **user, const char **pass, const char **bearer, const char **geometry, int *fullscreen, int *only_list_of_vm, int *one_vm, int *no_cert_check, int *restart_session, const char **nx_options, const char **kb_layout, const char **client_cert, const char **client_key, int *twice, int *preselectedvm)
{
  int opt, error = 0, version = 0;
  const char *program = argv[0];
  char *endptr;

  *host = getenv(QVDHOST_ENV);
  *user = getenv(QVDLOGIN_ENV);
  *pass = getenv(QVDPASSWORD_ENV);
  *bearer = getenv(QVDBEARER_ENV);
  *port = getintenv(QVDPORT_ENV, *port);
  *preselectedvm = getintenv(QVDVMID_ENV, *preselectedvm);

  while ((opt = getopt(argc, argv, "?dvrh:p:u:w:b:g:flonx:K:c:k:2s:")) != -1 )
    {
      switch (opt)
	{
	case '?':
	  error = 1;
	  break;
	case 'v':
	  version = 1;
	  break;
	case 'd':
	  qvd_set_debug(2);
	  break;
	case 'h':
	  *host = optarg;
	  break;
	case 'p':
	  errno = 0;
	  *port = (int) strtol(optarg, &endptr, 10);
	  if ((errno == ERANGE && (*port == LONG_MAX || *port == LONG_MIN))
	      || optarg == endptr)
	      *port = -1;
	  break;
	case 'u':
	  *user = optarg;
	  break;
	case 'w':
	  *pass = optarg;
	  break;
	case 'b':
	  *bearer = optarg;
	  break;
	case 'g':
	  *geometry = optarg;
	  break;
	case 'f':
	  *fullscreen = 1;
	  break;
	case 'l':
	  *only_list_of_vm = 1;
	  break;
	case 'o':
	  *one_vm = 1;
	  break;
	case 'n':
	  *no_cert_check = 1;
	  break;
	case 'x':
	  *nx_options = optarg;
	  break;
	case 'K':
	  *kb_layout = optarg;
	  break;
	case 'c':
	  *client_cert = optarg;
	  break;
	case 'k':
	  *client_key = optarg;
	  break;
        case 'r':
          *restart_session = 1;
          break;
        case '2':
          *twice = 1;
          break;
        case 's':
          *preselectedvm = (int) strtol(optarg, &endptr, 10);
	  if ((errno == ERANGE && (*preselectedvm == LONG_MAX || *preselectedvm == LONG_MIN))
	      || optarg == endptr)
	    {
	      fprintf(stderr, "Ignoring preselected parameter -s, unable to parse <%s>\n", optarg);
	      *preselectedvm = 0;
	    }
          break;
	default:
	  fprintf(stderr, "Parameter not recognized <%c>\n", opt);
	  error = 1;
	}
    }

  if (error) {
    help(program);
    exit(0);
  }

  if (version) {
    printf("%s\nChangelog:\n%s", qvd_get_version_text(), qvd_get_changelog());
    exit(0);
  }
  if (*host == NULL)
    {
      fprintf(stderr, "The host paramter -h is required\n");
      error = 1;
    }
  if (*bearer == NULL && (*user == NULL || *pass == NULL))
    {
      fprintf(stderr, "Either bearer parameter -b, or user parameter -u and password parameter -w is required\n");
      error = 1;
    }
  if (*port < 1 || *port > 65535)
    {
      fprintf(stderr, "The port parameter must be between 1 and 65535\n");
      error = 1;
    }

  if (*client_cert != NULL || *client_key != NULL)
    {

      if (*client_cert == NULL)
	{
	  fprintf(stderr, "If you specify -k then you must specify also -c\n");
	  error = 1;
	}
      else
	{
	  if (access(*client_cert, R_OK) != 0)
	    {
	      fprintf(stderr, "Cert file %s is not accessible: %s\n", *client_cert, strerror(errno));
	      error = 1;
	    }
	}

      if (*client_key == NULL)
	{
	  fprintf(stderr, "If you specify -c then you must specify also -k\n");
	  error = 1;
	}
      else
	{
	  if (access(*client_key, R_OK) != 0)
	    {
	      fprintf(stderr, "key file %s is not accessible: %s\n", *client_key, strerror(errno));
	      error = 1;
	    }
	}
    }

  if (error)
    help(program);

  return error;
}

#define YES_NO_SIZE 20
int accept_unknown_cert_callback(qvdclient *qvd, const char *cert_pem_str, const char *cert_pem_data)
{
  char answer[YES_NO_SIZE];
  int result;
  printf("Unknown cert:\n%s\n\nDo you want to accept it? ", cert_pem_str);
  scanf("%20s", answer);
  result = (strncmp(answer, "y", YES_NO_SIZE) == 0  ||
	    strncmp(answer, "yes", YES_NO_SIZE) == 0  ||
	    strncmp(answer, "Y", YES_NO_SIZE) == 0  ||
	    strncmp(answer, "Yes", YES_NO_SIZE) == 0  ||
	    strncmp(answer, "YES", YES_NO_SIZE) == 0);
  return result;
}


void print_vmids(vmlist *vm)
{
  vmlist *ptr;
  printf("List of vms:\n");
  for (ptr=vm; ptr != NULL; ptr = ptr->next)
    printf("VM ID:%d NAME:%s STATE:%s BLOCKED:%d\n",
	   ptr->data->id, ptr->data->name, ptr->data->state, ptr->data->blocked);
}
int choose_vmid(vmlist *vm)
{
  int vm_id = -1;
  vmlist *ptr;

  while (vm_id == -1)
    {
      print_vmids(vm);

      printf("Choose vmid: ");
      scanf("%d", &vm_id);
      for (ptr=vm; ptr != NULL; ptr = ptr->next)
	if (ptr->data->id == vm_id)
	  {
	    printf("You have chosen VM ID:%d NAME:%s STATE:%s BLOCKED:%d\n",
		   ptr->data->id, ptr->data->name, ptr->data->state, ptr->data->blocked);
	    return vm_id;
	  }
      printf("VM id not found. Please try again\n");
      vm_id = -1;
    }
}

int progress_callback(qvdclient *qvd, const char *message) {
  qvd_printf("Progress Callback: %s\n", message);
}

int _set_display_if_not_set(qvdclient *qvd) {
  char *display = getenv(DISPLAY_ENV);
  if (display == NULL || *display == '\0') {
    qvd_error(qvd, "The display variable was not set, setting %s to %s", DISPLAY_ENV, DEFAULT_DISPLAY);
    setenv(DISPLAY_ENV, DEFAULT_DISPLAY, 1);
    return 1;
  }
  return 0;
}

int qvd_connection(const char *host, int port, const char *user, const char *pass, const char *bearer, const char *geometry, int fullscreen, int only_list_of_vm, int one_vm, int no_cert_check, int restart_session, const char *nx_options, const char *kb_layout, const char *client_cert, const char *client_key, int preselectedvm) {
  int vm_id;
  qvdclient *qvd;

  qvd = qvd_init(host, port, user, pass, bearer);
  if (!qvd) {
    printf("Error initializing QVD object, aborting");
    return 9;
  }

  if (no_cert_check)
    qvd_set_no_cert_check(qvd);

  if (geometry)
    qvd_set_geometry(qvd, geometry);
  if (fullscreen)
    qvd_set_fullscreen(qvd);
  if (nx_options)
    qvd_set_nx_options(qvd, nx_options);
  if (kb_layout)
    qvd_set_kb_layout(qvd, kb_layout);

  qvd_set_cert_files(qvd, client_cert, client_key);

  qvd_set_unknown_cert_callback(qvd, accept_unknown_cert_callback);
  qvd_set_progress_callback(qvd, progress_callback);

  if (qvd_list_of_vm(qvd) == NULL)
    {
      printf("Error fetching vm in host %s: %s\n", host, qvd->error_buffer);
      qvd_free(qvd);
      return 5;
    }
  if (qvd->numvms <= 0)
    {
      printf("No vms found in host %s\n", host);
      qvd_free(qvd);
      return 2;
    }

  if (only_list_of_vm)
    {
      print_vmids(qvd->vmlist);
      printf("No more acctions, -l has been specified\n");
      return 0;
    }


  if (one_vm || qvd->numvms == 1 || preselectedvm)
    {
      if (preselectedvm)
	{
	  vm_id = preselectedvm;
	  printf("Connecting to preselected vm: vm_id %d\n", vm_id);
	}
      else
	{
	  vm_id = qvd->vmlist->data->id;
	  printf("Connecting to the first vm: vm_id %d\n", vm_id);
	}
    }
  else
    {
      vm_id = choose_vmid(qvd->vmlist);
    }
  if (vm_id < 0)
    {
      printf("Error choosing vm_id: %d\n", vm_id);
      return 6;
    }

  if ( restart_session )
     qvd_stop_vm(qvd, vm_id);

  /* Set display if not set */
  _set_display_if_not_set(qvd);

  qvd_connect_to_vm(qvd, vm_id);
  printf("after qvd_connect_to_vm\n");
  qvd_free(qvd);
  return 0;


}

int main(int argc, char *argv[], char *envp[]) {
  const char *host = NULL, *user = NULL, *pass = NULL, *bearer = NULL, *geometry = NULL, *nx_options = NULL, *kb_layout = NULL, *cert_file = NULL, *key_file = NULL;
  int port = 8443, fullscreen=0, only_list_of_vm=0, one_vm=0, no_cert_check=0, restart_session = 0, twice = 0, preselectedvm=0;
  int result, vm_id;
  signal(SIGPIPE, SIG_IGN);
  if (parse_params(argc, argv, &host, &port, &user, &pass, &bearer, &geometry, &fullscreen, &only_list_of_vm, &one_vm, &no_cert_check, &restart_session, &nx_options, &kb_layout, &cert_file, &key_file, &twice, &preselectedvm))
    return 1;

  result = qvd_connection(host, port, user, pass, bearer, geometry, fullscreen, only_list_of_vm, one_vm, no_cert_check, restart_session, nx_options, kb_layout, cert_file, key_file, preselectedvm);
  if (twice) {
    printf("Two connections requested. Result of first connection was %d\n", result);
    result = qvd_connection(host, port, user, pass, bearer, geometry, fullscreen, only_list_of_vm, one_vm, no_cert_check, restart_session, nx_options, kb_layout, cert_file, key_file, preselectedvm);
  }

  return result;
}
