/*******************************************************************************
 * Copyright (c) 2014, 2015 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 * The Eclipse Public License is available at
 *    http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Allan Stockdill-Mander - initial API and implementation and/or initial documentation
 *    Ian Craggs - convert to FreeRTOS
 *******************************************************************************/

#include "MQTTFreeRTOS.h"

int ThreadStart(TaskHandle_t task_handle, void (*fn)(void *), void *arg) {
    int rc = 0;
    uint16_t usTaskStackSize = (configMINIMAL_STACK_SIZE * 5);
    UBaseType_t uxTaskPriority = uxTaskPriorityGet(NULL); /* set the priority as the same as the calling task*/

    rc = xTaskCreate(
        fn,              /* The function that implements the task. */
        "MQTTTask",      /* Just a text name for the task to aid debugging. */
        usTaskStackSize, /* The stack size is defined in FreeRTOSIPConfig.h. */
        arg,             /* The task parameter, not used in this case. */
        uxTaskPriority,  /* The priority assigned to the task is defined in FreeRTOSConfig.h. */
        &task_handle);    /* The task handle is not used. */

    return rc;
}

void MutexInit(Mutex *mutex) {
    mutex->sem = xSemaphoreCreateMutex();
}

int MutexLock(Mutex *mutex) {
    return xSemaphoreTake(mutex->sem, portMAX_DELAY);
}

int MutexUnlock(Mutex *mutex) {
    return xSemaphoreGive(mutex->sem);
}

void TimerCountdownMS(Timer *timer, unsigned int timeout_ms) {
    timer->xTicksToWait = timeout_ms / portTICK_PERIOD_MS; /* convert milliseconds to ticks */
    vTaskSetTimeOutState(&timer->xTimeOut);                /* Record the time at which this function was entered. */
}

void TimerCountdown(Timer *timer, unsigned int timeout) {
    TimerCountdownMS(timer, timeout * 1000);
}

int TimerLeftMS(Timer *timer) {
    xTaskCheckForTimeOut(&timer->xTimeOut, &timer->xTicksToWait); /* updates xTicksToWait to the number left */
    return (timer->xTicksToWait < 0) ? 0 : (timer->xTicksToWait * portTICK_PERIOD_MS);
}

char TimerIsExpired(Timer *timer) {
    return xTaskCheckForTimeOut(&timer->xTimeOut, &timer->xTicksToWait) == pdTRUE;
}

void TimerInit(Timer *timer) {
    timer->xTicksToWait = 0;
    memset(&timer->xTimeOut, '\0', sizeof(timer->xTimeOut));
}

int FreeRTOS_read(Network *n, unsigned char *buffer, int len, int timeout_ms) {
    Timer timer;
    TimerInit(&timer);
    TimerCountdownMS(&timer, timeout_ms);
    int recvLen = 0;
    
    do {
        int rc = 0;

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = timeout_ms * 1000;
        if (lwip_setsockopt(n->my_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
             printf("Can't set socket RECV timeout");
        }
        rc = lwip_recv(n->my_socket, buffer + recvLen, len - recvLen, 0);
        if (rc > 0)
            recvLen += rc;
        else if (rc < 0) {
            recvLen = rc;
            break;
        }
    } while (recvLen < len && !TimerIsExpired(&timer));

    if (TimerIsExpired(&timer)) {
        return 0;
    }
    return recvLen;
}

int FreeRTOS_write(Network *n, unsigned char *buffer, int len, int timeout_ms) {
    Timer timer;
    TimerInit(&timer);
    TimerCountdownMS(&timer, timeout_ms);
    int sentLen = 0;

    do {
        int rc = 0;

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = timeout_ms * 1000;
        if (lwip_setsockopt(n->my_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
            printf("Can't set socket SEND timeout");
        }
        rc = lwip_send(n->my_socket, buffer + sentLen, len - sentLen, 0);
        if (rc > 0)
            sentLen += rc;
        else if (rc < 0) {
            sentLen = rc;
            break;
        }
    } while (sentLen < len && !TimerIsExpired(&timer));

    return sentLen;
}

void FreeRTOS_disconnect(Network *n) {
    lwip_close(n->my_socket);
}

void NetworkInit(Network *n) {
    n->my_socket = 0;
    n->mqttread = FreeRTOS_read;
    n->mqttwrite = FreeRTOS_write;
    n->disconnect = FreeRTOS_disconnect;
}

static bool dnsFound;
ip_addr_t server;

void dns_found_cb(const char *name, const ip_addr_t *ipaddr, void *callback_arg) {
    server = *ipaddr;
    dnsFound = true; 
    printf("Resolved hostname to: %s\n", ip4addr_ntoa(&server));
}

int NetworkConnect(Network *n, char *addr, int port) {
    n->my_socket = lwip_socket(AF_INET, SOCK_STREAM, 0); 
    if (n->my_socket < 0) {
        printf("Socket creation failed!\n");
        return -1;
    }    

    dns_gethostbyname(addr, &server, dns_found_cb, NULL);
    
    while (!dnsFound) {
        vTaskDelay(1000);
    }

    struct sockaddr_in server_addr;

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_len = sizeof(struct sockaddr_in), server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = server.addr;

    // Connect to the server
    if (lwip_connect(n->my_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        printf("Connection failed!\n");
        lwip_close(n->my_socket);
        return -1;
    }

    return ERR_OK;
}

#if 0
int NetworkConnectTLS(Network *n, char* addr, int port, SlSockSecureFiles_t* certificates, unsigned char sec_method, unsigned int cipher, char server_verify)
{
	SlSockAddrIn_t sAddr;
	int addrSize;
	int retVal;
	unsigned long ipAddress;

	retVal = sl_NetAppDnsGetHostByName(addr, strlen(addr), &ipAddress, AF_INET);
	if (retVal < 0) {
		return -1;
	}

	sAddr.sin_family = AF_INET;
	sAddr.sin_port = sl_Htons((unsigned short)port);
	sAddr.sin_addr.s_addr = sl_Htonl(ipAddress);

	addrSize = sizeof(SlSockAddrIn_t);

	n->my_socket = sl_Socket(SL_AF_INET, SL_SOCK_STREAM, SL_SEC_SOCKET);
	if (n->my_socket < 0) {
		return -1;
	}

	SlSockSecureMethod method;
	method.secureMethod = sec_method;
	retVal = sl_SetSockOpt(n->my_socket, SL_SOL_SOCKET, SL_SO_SECMETHOD, &method, sizeof(method));
	if (retVal < 0) {
		return retVal;
	}

	SlSockSecureMask mask;
	mask.secureMask = cipher;
	retVal = sl_SetSockOpt(n->my_socket, SL_SOL_SOCKET, SL_SO_SECURE_MASK, &mask, sizeof(mask));
	if (retVal < 0) {
		return retVal;
	}

	if (certificates != NULL) {
		retVal = sl_SetSockOpt(n->my_socket, SL_SOL_SOCKET, SL_SO_SECURE_FILES, certificates->secureFiles, sizeof(SlSockSecureFiles_t));
		if (retVal < 0)
		{
			return retVal;
		}
	}

	retVal = sl_Connect(n->my_socket, (SlSockAddr_t *)&sAddr, addrSize);
	if (retVal < 0) {
		if (server_verify || retVal != -453) {
			sl_Close(n->my_socket);
			return retVal;
		}
	}

	SysTickIntRegister(SysTickIntHandler);
	SysTickPeriodSet(80000);
	SysTickEnable();

	return retVal;
}
#endif
