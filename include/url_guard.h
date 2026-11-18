#ifndef URL_GUARD_H
#define URL_GUARD_H

#include <curl/curl.h>
#include "nevercode.h"

/* curl OPENSOCKETFUNCTION: refuse to create a socket for a non-public address. */
curl_socket_t nc_ssrf_opensocket(void *clientp, curlsocktype purpose,
                                 struct curl_sockaddr *address);

#endif
