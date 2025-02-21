#pragma once

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

typedef struct {
	TickType_t xTicksToWait = 0;
	TimeOut_t xTimeOut = {};
} Timer;

char TimerIsExpired(Timer*);

void TimerCountdownMS(Timer*, unsigned int);

void TimerCountdown(Timer*, unsigned int);

int TimerLeftMS(Timer*);

