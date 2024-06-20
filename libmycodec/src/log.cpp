/******************************************************************************
 * Author: liguoqiang
 * Date: 2021-06-15 17:16:24
 * LastEditors: liguoqiang
 * LastEditTime: 2024-03-31 19:59:53
 * Description: 
********************************************************************************/

extern "C" {
#include "libswscale/swscale.h"
#include "libavcodec/avcodec.h"
#include "libavdevice/avdevice.h"
#include "libavutil/avutil.h"
#include "libavutil/timestamp.h"
#include "libavcodec/jni.h"
}

#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>


static int _log_level = AV_LOG_INFO;
static FILE * _file=NULL;
#define MAX_LOG 2048

int log_init(const char* logfile)
{
	if(!_file) {
		_file = fopen(logfile, "w");
	}
	return (_file == NULL ? -1 : 0);
}

void log_unini()
{
	if(_file) {
		fclose(_file);
		_file = NULL;
	}
}

void log_set_level(int level)
{
	_log_level = level;
	av_log_set_level(level);
}

int put_log(int level, const char* fmt,...)
{
	if (level > _log_level) {
		return 0;
	}

	char buf[MAX_LOG] = {0};
	va_list ap;
	va_start (ap, fmt);
	vsnprintf(buf, MAX_LOG, fmt, ap);
	va_end (ap);
		
	strcat(buf, "\n");

	if(_file) {
		char * levelStr = "";
		switch(level) {
		case AV_LOG_FATAL:
			levelStr = "AV_LOG_FATAL";
			break;
		case AV_LOG_ERROR:
			levelStr = "AV_LOG_ERROR";
			break;
		case AV_LOG_WARNING:
			levelStr = "AV_LOG_WARNING";
			break;
		case AV_LOG_INFO:
			levelStr = "AV_LOG_INFO";
			break;
		case AV_LOG_VERBOSE:
			levelStr = "AV_LOG_VERBOSE";
			break;
		case AV_LOG_DEBUG:
			levelStr = "AV_LOG_DEBUG";
			break;
		case AV_LOG_TRACE:
			levelStr = "AV_LOG_TRACE";
			break;
		default:
			levelStr = "OTHRE";
			break;
		}
		time_t t = time(NULL);
        tm *ptm = localtime(&t);
        fprintf(_file, "%s %s: %s\n", asctime(ptm), levelStr, buf);
        fflush(_file);
	}
	return 0;
}

char * av_err2str2(int errnum)
{
	static char buf[AV_ERROR_MAX_STRING_SIZE];
	av_strerror(errnum, buf, AV_ERROR_MAX_STRING_SIZE);
	return buf;
}