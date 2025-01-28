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
#include "ssi.h"

#include "lwip/apps/httpd.h"

#include "lwip/def.h"
#include "lwip/mem.h"

#include <stdio.h>
#include <string.h>

#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>
#include "etl/string.h"
#include "etl/string_stream.h"

#define MAX_CGI_LEN   16

const char * ssi_tags[] = {
  "MACAddr",
  "counter",
  "MultPart"
#if LWIP_HTTPD_EXAMPLE_SSI_SIMPLE_CGI_INTEGRATION
  ,"CgiParam"
#endif
};

u16_t ssi_handler(
#if LWIP_HTTPD_SSI_RAW
                             const char* ssi_tag_name,
#else /* LWIP_HTTPD_SSI_RAW */
                             int iIndex,
#endif /* LWIP_HTTPD_SSI_RAW */
                             char *pcInsert, int iInsertLen
#if LWIP_HTTPD_SSI_MULTIPART
                             , u16_t current_tag_part, u16_t *next_tag_part
#endif /* LWIP_HTTPD_SSI_MULTIPART */
#if defined(LWIP_HTTPD_FILE_STATE) && LWIP_HTTPD_FILE_STATE
                             , void *connection_state
#endif /* LWIP_HTTPD_FILE_STATE */
                             )
{
  size_t printed;

#if LWIP_HTTPD_SSI_RAW
  /* a real application could use if(!strcmp) blocks here, but we want to keep
     the differences between configurations small, so translate string to index here */
  int iIndex;
  for (iIndex = 0; iIndex < LWIP_ARRAYSIZE(ssi_tags); iIndex++) {
    if(!strcmp(ssi_tag_name, ssi_tags[iIndex])) {
      break;
    }
  }
#endif
#if defined(LWIP_HTTPD_FILE_STATE) && LWIP_HTTPD_FILE_STATE
  LWIP_UNUSED_ARG(connection_state);
#endif

  switch (iIndex) {
  case 0: /* "MACAddr" */
  	uint8_t itf_sta_mac[6];
  	cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, itf_sta_mac);
    printed = snprintf(pcInsert, iInsertLen, "%02x:%02x:%02x:%02x:%02x:%02x\n",
	  itf_sta_mac[0], itf_sta_mac[1], itf_sta_mac[2], itf_sta_mac[3], itf_sta_mac[4], itf_sta_mac[5]);    
    break;
  case 1: /* "counter" */
    {
      static int counter;
      counter++;
      printed = snprintf(pcInsert, iInsertLen, "%d", counter);
    }
    break;
  case 2: /* "MultPart" */
#if LWIP_HTTPD_SSI_MULTIPART
    switch (current_tag_part) {
    case 0:
      printed = snprintf(pcInsert, iInsertLen, "part0");
      *next_tag_part = 1;
      break;
    case 1:
      printed = snprintf(pcInsert, iInsertLen, "part1");
      *next_tag_part = 2;
      break;
    case 2:
      printed = snprintf(pcInsert, iInsertLen, "part2");
      break;
    default:
      printed = snprintf(pcInsert, iInsertLen, "unhandled part: %d", (int)current_tag_part);
      break;
    }
#else
    printed = snprintf(pcInsert, iInsertLen, "LWIP_HTTPD_SSI_MULTIPART disabled");
#endif
    break;
#if LWIP_HTTPD_EXAMPLE_SSI_SIMPLE_CGI_INTEGRATION
  case 3:
    if (connection_state) {
      char *params = (char *)connection_state;
      if (*params) {
        printed = snprintf(pcInsert, iInsertLen, "%s", (char *)params);
      } else {
        printed = snprintf(pcInsert, iInsertLen, "none");
      }
    } else {
       printed = snprintf(pcInsert, iInsertLen, "NULL");
    }
    break;
#endif
  default: /* unknown tag */
    printed = 0;
    break;
  }
  LWIP_ASSERT("sane length", printed <= 0xFFFF);
  return (u16_t)printed;
}

void
ssi_init(void)
{
  int i;
  for (i = 0; i < LWIP_ARRAYSIZE(ssi_tags); i++) {
    LWIP_ASSERT("tag too long for LWIP_HTTPD_MAX_TAG_NAME_LEN",
      strlen(ssi_tags[i]) <= LWIP_HTTPD_MAX_TAG_NAME_LEN);
  }

  http_set_ssi_handler(ssi_handler,
#if LWIP_HTTPD_SSI_RAW
    NULL, 0
#else
    ssi_tags, LWIP_ARRAYSIZE(ssi_tags)
#endif
    );
}


