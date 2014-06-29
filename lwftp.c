/*
 * lwftp.c : a lightweight FTP client using raw API of LWIP
 *
 * Copyright (c) 2014 GEZEDO
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
 * Author: Laurent GONZALEZ <lwip@gezedo.com>
 *
 */

#include <string.h>
#include "lwftp.h"
#include "lwip/tcp.h"

/** Enable debugging for LWFTP */
#ifndef LWFTP_DEBUG
#define LWFTP_DEBUG   LWIP_DBG_ON
#endif

#define LWFTP_TRACE   (LWFTP_DEBUG|LWIP_DBG_TRACE)
#define LWFTP_WARNING (LWFTP_DEBUG|LWIP_DBG_LEVEL_WARNING)
#define LWFTP_SERIOUS (LWFTP_DEBUG|LWIP_DBG_LEVEL_SERIOUS)
#define LWFTP_SEVERE  (LWFTP_DEBUG|LWIP_DBG_LEVEL_SEVERE)

#define PTRNLEN(s)  s,(sizeof(s)-1)

/** Close control or data pcb
 * @param pointer to lwftp session data
 */
static err_t lwftp_pcb_close(struct tcp_pcb *tpcb)
{
  err_t error;

  tcp_err(tpcb, NULL);
  tcp_recv(tpcb, NULL);
  tcp_sent(tpcb, NULL);
  error = tcp_close(tpcb);
  if ( error != ERR_OK ) {
    LWIP_DEBUGF(LWFTP_SEVERE, ("lwtcp:pcb close failure, not implemented\n"));
  }
  return ERR_OK;
}

/** Send data
 * @param pointer to lwftp session data
 * @param pointer to PCB
 * @param number of bytes sent
 */
static err_t lwftp_send_next_data(lwftp_session_t *s)
{
  const char *data;
  int len = 0;
  err_t error = ERR_OK;

  if (s->data_source) {
    len = s->data_source(&data, s->data_pcb->mss);
    if (len) {
      LWIP_DEBUGF(LWFTP_TRACE, ("lwtcp:sending %d bytes of data\n",len));
      error = tcp_write(s->data_pcb, data, len, 0);
      if (error!=ERR_OK) {
        LWIP_DEBUGF(LWFTP_SEVERE, ("lwtcp:write failure (%s), not implemented\n",lwip_strerr(error)));
      }
    }
  }
  if (!len) {
    LWIP_DEBUGF(LWFTP_TRACE, ("lwtcp:end of file\n"));
    lwftp_pcb_close(s->data_pcb);
    s->data_pcb = NULL;
  }
  return ERR_OK;
}

/** Handle data connection incoming data
 * @param pointer to lwftp session data
 * @param pointer to PCB
 * @param pointer to incoming pbuf
 * @param state of incoming process
 */
static err_t lwftp_data_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
  LWIP_DEBUGF(LWFTP_SEVERE, ("lwtcp:nothing implemented (line %d)\n",__LINE__));
  return ERR_ABRT;
}

/** Handle data connection acknowledge of sent data
 * @param pointer to lwftp session data
 * @param pointer to PCB
 * @param number of bytes sent
 */
static err_t lwftp_data_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
  lwftp_session_t *s = (lwftp_session_t*)arg;

  if ( s->data_source ) {
    s->data_source(NULL, len);
  }
  return lwftp_send_next_data(s);
}

/** Handle data connection error
 * @param pointer to lwftp session data
 * @param state of connection
 */
static void lwftp_data_err(void *arg, err_t err)
{
  LWIP_DEBUGF(LWFTP_SEVERE, ("lwtcp:nothing implemented (line %d)\n",__LINE__));
}

/** Process newly connected PCB
 * @param pointer to lwftp session data
 * @param pointer to PCB
 * @param state of connection
 */
static err_t lwftp_data_connected(void *arg, struct tcp_pcb *tpcb, err_t err)
{
  lwftp_session_t *s = (lwftp_session_t*)arg;

  if ( err == ERR_OK ) {
    LWIP_DEBUGF(LWFTP_TRACE, ("lwtcp:connected for data to server\n"));
    s->data_state = LWFTP_CONNECTED;
  } else {
    LWIP_DEBUGF(LWFTP_WARNING, ("lwtcp:failed to connect for data to server (%s)\n",lwip_strerr(err)));
  }
  return err;
}

/** Open data connection for passive transfer
 * @param pointer to lwftp session data
 * @param pointer to incoming PASV response
 */
static err_t lwftp_data_open(lwftp_session_t *s, struct pbuf *p)
{
  err_t error;
  char *ptr;
  ip_addr_t data_server;
  u16 data_port;

  // Find server connection parameter
  ptr = strchr(p->payload, '(');
  if (!ptr) return ERR_BUF;
  ip4_addr1(&data_server) = strtoul(ptr+1,&ptr,10);
  ip4_addr2(&data_server) = strtoul(ptr+1,&ptr,10);
  ip4_addr3(&data_server) = strtoul(ptr+1,&ptr,10);
  ip4_addr4(&data_server) = strtoul(ptr+1,&ptr,10);
  data_port  = strtoul(ptr+1,&ptr,10) << 8;
  data_port |= strtoul(ptr+1,&ptr,10) & 255;
  if (*ptr!=')') return ERR_BUF;

  // Open data session
  tcp_arg(s->data_pcb, s);
  tcp_err(s->data_pcb, lwftp_data_err);
  tcp_recv(s->data_pcb, lwftp_data_recv);
  tcp_sent(s->data_pcb, lwftp_data_sent);
  error = tcp_connect(s->data_pcb, &data_server, data_port, lwftp_data_connected);
  return error;
}

/** Send a message to control connection
 * @param pointer to lwftp session data
 * @param pointer to message string
 */
static err_t lwftp_send_msg(lwftp_session_t *s, char* msg, size_t len)
{
  err_t error;

  LWIP_DEBUGF(LWFTP_TRACE,("lwtcp:sending %s",msg));
  error = tcp_write(s->control_pcb, msg, len, 0);
  if ( error != ERR_OK ) {
      LWIP_DEBUGF(LWFTP_WARNING, ("lwtcp:cannot write (%s)\n",lwip_strerr(error)));
  }
  return error;
}

/** Close control connection
 * @param pointer to lwftp session data
 */
static void lwftp_control_close(lwftp_session_t *s)
{
  if (s->data_pcb) {
    lwftp_pcb_close(s->data_pcb);
    s->data_pcb = NULL;
  }
  if (s->control_pcb) {
    lwftp_pcb_close(s->control_pcb);
    s->control_pcb = NULL;
  }
  s->control_state = LWFTP_CLOSED;
}

/** Main client state machine
 * @param pointer to lwftp session data
 * @param pointer to PCB
 * @param pointer to incoming data
 */
static void lwftp_control_process(lwftp_session_t *s, struct tcp_pcb *tpcb, struct pbuf *p)
{
  uint response = 0;

  // Try to get response number
  if (p) {
    response = strtoul(p->payload, NULL, 10);
    LWIP_DEBUGF(LWFTP_TRACE, ("lwtcp:got response %d\n",response));
  }

  switch (s->control_state) {
    case LWFTP_CONNECTED:
      if (response>0) {
        if (response==220) {
          lwftp_send_msg(s, PTRNLEN("USER anonymous\n"));
          s->control_state = LWFTP_USER_SENT;
        } else {
          s->control_state = LWFTP_QUIT;
        }
      }
      break;
    case LWFTP_USER_SENT:
      if (response>0) {
        if (response==331) {
          lwftp_send_msg(s, PTRNLEN("PASS none@nowhere.net\n"));
          s->control_state = LWFTP_PASS_SENT;
        } else {
          s->control_state = LWFTP_QUIT;
        }
      }
      break;
    case LWFTP_PASS_SENT:
      if (response>0) {
        if (response==230) {
          lwftp_send_msg(s, PTRNLEN("TYPE I\n"));
          s->control_state = LWFTP_TYPE_SENT;
        } else {
          s->control_state = LWFTP_QUIT;
        }
      }
      break;
    case LWFTP_TYPE_SENT:
      if (response>0) {
        if (response==200) {
          lwftp_send_msg(s, PTRNLEN("PASV\n"));
          s->control_state = LWFTP_PASV_SENT;
        } else {
          s->control_state = LWFTP_QUIT;
        }
      }
      break;
    case LWFTP_PASV_SENT:
      if (response>0) {
        if (response==227) {
          lwftp_data_open(s,p);
          lwftp_send_msg(s, PTRNLEN("STOR "));
          lwftp_send_msg(s, s->remote_path, strlen(s->remote_path));
          lwftp_send_msg(s, PTRNLEN("\n"));
          s->control_state = LWFTP_STOR_SENT;
        } else {
          s->control_state = LWFTP_QUIT;
        }
      }
      break;
    case LWFTP_STOR_SENT:
      if (response>0) {
        if (response==150) {
          s->control_state = LWFTP_STORING;
          lwftp_data_sent(s,NULL,0);
        } else {
          s->control_state = LWFTP_QUIT;
        }
      }
      break;
    case LWFTP_STORING:
      if (response>0) {
        if (response!=226) {
          LWIP_DEBUGF(LWFTP_WARNING, ("lwftp:expected 226, received %d\n",response));
        }
        s->control_state = LWFTP_QUIT;
      }
      break;
    case LWFTP_QUIT_SENT:
      if (response>0) {
        if (response!=221) {
          LWIP_DEBUGF(LWFTP_WARNING, ("lwftp:expected 221, received %d\n",response));
        }
        lwftp_control_close(s);
      }
      break;
    default:
      LWIP_DEBUGF(LWFTP_SEVERE, ("lwtcp:unhandled state (%d)\n",s->control_state));
  }

  // Quit when required to do so
  if ( s->control_state == LWFTP_QUIT ) {
    lwftp_send_msg(s, PTRNLEN("QUIT\n"));
    s->control_state = LWFTP_QUIT_SENT;
  }

}

/** Handle control connection incoming data
 * @param pointer to lwftp session data
 * @param pointer to PCB
 * @param pointer to incoming pbuf
 * @param state of incoming process
 */
static err_t lwftp_control_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
  lwftp_session_t *s = (lwftp_session_t*)arg;

  if ( err == ERR_OK ) {
    if (p) {
      tcp_recved(tpcb, p->tot_len);
      lwftp_control_process(s, tpcb, p);
    } else {
      LWIP_DEBUGF(LWFTP_WARNING, ("lwtcp:connection closed by remote host\n"));
      lwftp_control_close(s);
    }
  } else {
    LWIP_DEBUGF(LWFTP_SERIOUS, ("lwtcp:failed to receive (%s)\n",lwip_strerr(err)));
    lwftp_control_close(s);
  }
  return err;
}

/** Handle control connection acknowledge of sent data
 * @param pointer to lwftp session data
 * @param pointer to PCB
 * @param number of bytes sent
 */
static err_t lwftp_control_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
  LWIP_DEBUGF(LWFTP_TRACE, ("lwtcp:successfully sent %d bytes\n",len));
  return ERR_OK;
}

/** Handle control connection error
 * @param pointer to lwftp session data
 * @param state of connection
 */
static void lwftp_control_err(void *arg, err_t err)
{
  LWIP_DEBUGF(LWFTP_SEVERE, ("lwtcp:nothing implemented (line %d)\n",__LINE__));
}


/** Process newly connected PCB
 * @param pointer to lwftp session data
 * @param pointer to PCB
 * @param state of connection
 */
static err_t lwftp_control_connected(void *arg, struct tcp_pcb *tpcb, err_t err)
{
  lwftp_session_t *s = (lwftp_session_t*)arg;

  if ( err == ERR_OK ) {
    LWIP_DEBUGF(LWFTP_TRACE, ("lwtcp:connected to server\n"));
      s->control_state = LWFTP_CONNECTED;
  } else {
    LWIP_DEBUGF(LWFTP_WARNING, ("lwtcp:failed to connect to server (%s)\n",lwip_strerr(err)));
  }
  return err;
}


/** Store data to a remote file
 * @param Session structure
 */
err_t lwftp_store(lwftp_session_t *s)
{
  err_t error;

  // Check user supplied data
  if ((s->control_state!=LWFTP_CLOSED) || !s->remote_path || s->control_pcb || s->data_pcb) {
    LWIP_DEBUGF(LWFTP_WARNING, ("lwtcp:invalid session data\n"));
    return ERR_ARG;
  }
  // Get sessions pcb
  s->control_pcb = tcp_new();
  if (!s->control_pcb) {
    LWIP_DEBUGF(LWFTP_SERIOUS, ("lwtcp:cannot alloc control_pcb (low memory?)\n"));
    error = ERR_MEM;
    goto exit;
  }
  s->data_pcb = tcp_new();
  if (!s->data_pcb) {
    LWIP_DEBUGF(LWFTP_SERIOUS, ("lwtcp:cannot alloc data_pcb (low memory?)\n"));
    error = ERR_MEM;
    goto close_pcb;
  }
  // Open control session
  tcp_arg(s->control_pcb, s);
  tcp_err(s->control_pcb, lwftp_control_err);
  tcp_recv(s->control_pcb, lwftp_control_recv);
  tcp_sent(s->control_pcb, lwftp_control_sent);
  error = tcp_connect(s->control_pcb, &s->server_ip, s->server_port, lwftp_control_connected);
  if ( error == ERR_OK ) goto exit;

  LWIP_DEBUGF(LWFTP_SERIOUS, ("lwtcp:cannot connect control_pcb (%s)\n", lwip_strerr(error)));

close_pcb:
  // Release pcbs in case of failure
  lwftp_control_close(s);

exit:
  return error;
}