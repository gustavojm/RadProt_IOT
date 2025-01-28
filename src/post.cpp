/*
 * Copyright (c) 2017 Simon Goldschmidt
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT 
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT 
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING 
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 * 
 * Author: Simon Goldschmidt <goldsimon@gmx.de>
 *
 */

#include "lwip/opt.h"

#include "lwip/apps/httpd.h"
#include "lwip/def.h"
#include "lwip/mem.h"

#include <stdio.h>
#include <string.h>
#include <ArduinoJson.hpp>

#define USER_PASS_BUFSIZE 16

static void *current_connection;
static void *valid_connection;

err_t
httpd_post_begin(void *connection, const char *uri, const char *http_request,
                 u16_t http_request_len, int content_len, char *response_uri,
                 u16_t response_uri_len, u8_t *post_auto_wnd)
{
  LWIP_UNUSED_ARG(connection);
  LWIP_UNUSED_ARG(http_request);
  LWIP_UNUSED_ARG(http_request_len);
  LWIP_UNUSED_ARG(content_len);
  LWIP_UNUSED_ARG(post_auto_wnd);  
  if (!memcmp(uri, "/settings.cgi", 14)) {
    if (current_connection != connection) {
      current_connection = connection;
      // valid_connection = NULL;
      /* default page is "login failed" */
      snprintf(response_uri, response_uri_len, "/loginfail.html");
      /* e.g. for large uploads to slow flash over a fast connection, you should
         manually update the rx window. That way, a sender can only send a full
         tcp window at a time. If this is required, set 'post_aut_wnd' to 0.
         We do not need to throttle upload speed here, so: */
      *post_auto_wnd = 1;
      return ERR_OK;
    }
  }
  return ERR_VAL;
}

err_t
httpd_post_receive_data(void *connection, struct pbuf *p) {
  err_t ret;

  LWIP_ASSERT("NULL pbuf", p != NULL);

  struct pbuf *unique_pbuf = nullptr;
  if (p->next == NULL) {    // Single pbuf
    unique_pbuf = p;
  } else {
    unique_pbuf = pbuf_coalesce(p, PBUF_TRANSPORT);
    if (unique_pbuf == p) {
      printf("allocation failed");
      return -ENOMEM;
    }
  }

  namespace json = ArduinoJson;
  auto post_data = json::JsonDocument();
  json::DeserializationError error = json::deserializeJson(post_data, unique_pbuf->payload, unique_pbuf->len);

  if (error) {
      printf("Error json parse. %s", error.c_str());
  } else {
    char const *ssid = post_data["ssid"];
    printf("SSID: %s", ssid);
  }
  ret = ERR_OK;
  /* this function must ALWAYS free the pbuf it is passed or it will leak memory */
  pbuf_free(unique_pbuf);

  return ret;

}

void
httpd_post_finished(void *connection, char *response_uri, u16_t response_uri_len)
{
  struct http_state *hs = static_cast<struct http_state *>(connection);
    // hs->hdrs[HDR_STRINGS_IDX_HTTP_STATUS] = ;
    // hs->hdrs[HDR_STRINGS_IDX_CONTENT_LEN_KEEPALIVE] = g_psHTTPHeaderStrings[HTTP_HDR_CONN_CLOSE];
        
    // /* Set up to send the first header string. */
    // hs->hdr_index = 0;
    // hs->hdr_pos = 0;      
    // return ERR_OK;


  /* default page is "login failed" */
  snprintf(response_uri, response_uri_len, "/loginfail.html");
  if (current_connection == connection) {
    snprintf(response_uri, response_uri_len, "/session.html");
    current_connection = NULL;
  }
}
