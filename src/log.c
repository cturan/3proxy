/*
   3APA3A simpliest proxy server
   (c) 2002-2020 by Vladimir Dubrovin <3proxy@3proxy.ru>

   please read License Agreement

*/

#include "proxy.h"
pthread_mutex_t log_mutex;
int havelog = 0;


struct clientparam logparam;
struct srvparam logsrv;

struct LOGGER;

void(*prelog)(struct clientparam * param) = NULL;

#ifdef WITHMAIN
#define HAVERADIUS 0
#define HAVESQL 0
#else
int raddobuf(struct clientparam * param, unsigned char * buf, const unsigned char *s);
void logradius(const char * buf, int len, struct LOGGER *logger);
#define HAVERADIUS 1

#ifndef NOODBC
#undef HAVESQL
#define HAVESQL 1
static int sqlinit(const char * selector, int logtype, struct LOGGER *logger);
static void sqllog(const char * buf, int len, struct LOGGER *logger);
static void sqlrotate(struct LOGGER *logger);
static void sqlclose(struct LOGGER *logger);
#endif
#endif

#ifdef _WIN32
#define HAVESYSLOG 0
#else
#define HAVESYSLOG 1
static int sysloginit(const char * selector, int logtype, struct LOGGER *logger);
static void logsyslog(const char * buf, int len, struct LOGGER *logger);
static void syslogrotate(struct LOGGER *logger);
static void syslogclose(struct LOGGER *logger);
#endif

static int stdloginit(const char * selector, int logtype, struct LOGGER *logger);
static void stdlog(const char * buf, int len, struct LOGGER *logger);
static void stdlogrotate(struct LOGGER *logger);
static void stdlogclose(struct LOGGER *logger);



struct LOGFUNC logfuncs = {
#if HAVESYSLOG > 0
		{logfuncs+1+HAVESYSLOG, sysloginit, stddobuf, logsyslog, syslogrotate, syslogclose, "@"},
#endif
#if HAVERADIUS > 0
		{logfuncs+1+HAVESYSLOG+HAVERADIUS, NULL, raddobuf, logradius, NULL, NULL, "radius"},
#endif
#if HAVESQL > 0
		{logfuncs+1+HAVESYSLOG+HAVERADIUS+HAVESQL, sqlinit, sqldobuf, sqllog, sqlrotate, sqlclose, "&"},
#endif
		{NULL, stdloginit, stddobuf, stdlog, stdlogrotate, stdlogclose, ""}
	     };



struct LOGGER *loggers = NULL;

struct stdlogdata{
	FILE *fp;
} errld= {stderr};

struct LOGGER {
	char * selector;
	void * data;
	struct LOGFUNC *logfunc;
	int rotate;
	time_t rotated;
	int registered;
} errlogger = {"errlogger", &errld, logfuncs+1+HAVESYSLOG+HAVERADIUS+HAVESQL, 0, 0, 1};


void initlog(void){
	srvinit(&logsrv, &logparam);
	pthread_mutex_init(&log_mutex, NULL);
}

void dolog(struct clientparam * param, const unsigned char *s){
	static int init = 0;

/* TODO: dobuf */
/* TODO: spooling */
	if(!param){
		stdlog(s, strlen(s), &stdlogger);
	}
	else if(!param->nolog && param->srv->logtarget){
		if(prelog)prelog(param);
		param->srv->logfunc(param, s);
	}
	if(param->trafcountfunc)(*param->trafcountfunc)(param);
	clearstat(param);
}


void clearstat(struct clientparam * param) {

#ifdef _WIN32
	struct timeb tb;

	ftime(&tb);
	param->time_start = (time_t)tb.time;
	param->msec_start = (unsigned)tb.millitm;

#else
	struct timeval tv;
	struct timezone tz;
	gettimeofday(&tv, &tz);

	param->time_start = (time_t)tv.tv_sec;
	param->msec_start = (tv.tv_usec / 1000);
#endif
	param->statscli64 = param->statssrv64 = param->nreads = param->nwrites =
		param->nconnects = 0;
}


char months[12][4] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};


int dobuf2(struct clientparam * param, unsigned char * buf, const unsigned char *s, const unsigned char * doublec, struct tm* tm, char * format){
	int i, j;
	int len;
	time_t sec;
	unsigned msec;

	long timezone;
	unsigned delay;



#ifdef _WIN32
	struct timeb tb;

	ftime(&tb);
	sec = (time_t)tb.time;
	msec = (unsigned)tb.millitm;
	timezone = tm->tm_isdst*60 - tb.timezone;

#else
	struct timeval tv;
	struct timezone tz;
	gettimeofday(&tv, &tz);

	sec = (time_t)tv.tv_sec;
	msec = tv.tv_usec / 1000;
#ifdef _SOLARIS
	timezone = -altzone / 60;
#else
	timezone = tm->tm_gmtoff / 60;
#endif
#endif

	delay = param->time_start?((unsigned) ((sec - param->time_start))*1000 + msec) - param->msec_start : 0;
	*buf = 0;
	for(i=0, j=0; format[j] && i < 4040; j++){
		if(format[j] == '%' && format[j+1]){
			j++;
			switch(format[j]){
				case '%':
				 buf[i++] = '%';
				 break;
				case 'y':
				 sprintf((char *)buf+i, "%.2d", tm->tm_year%100);
				 i+=2;
				 break;
				case 'Y':
				 sprintf((char *)buf+i, "%.4d", tm->tm_year+1900);
				 i+=4;
				 break;
				case 'm':
				 sprintf((char *)buf+i, "%.2d", tm->tm_mon+1);
				 i+=2;
				 break;
				case 'o':
				 sprintf((char *)buf+i, "%s", months[tm->tm_mon]);
				 i+=3;
				 break;
				case 'd':
				 sprintf((char *)buf+i, "%.2d", tm->tm_mday);
				 i+=2;
				 break;
				case 'H':
				 sprintf((char *)buf+i, "%.2d", tm->tm_hour);
				 i+=2;
				 break;
				case 'M':
				 sprintf((char *)buf+i, "%.2d", tm->tm_min);
				 i+=2;
				 break;
				case 'S':
				 sprintf((char *)buf+i, "%.2d", tm->tm_sec);
				 i+=2;
				 break;
				case 't':
				 sprintf((char *)buf+i, "%.10u", (unsigned)sec);
				 i+=10;
				 break;
				case 'b':
				 i+=sprintf((char *)buf+i, "%u", delay?(unsigned)(param->statscli64 * 1000./delay):0);
				 break;
				case 'B':
				 i+=sprintf((char *)buf+i, "%u", delay?(unsigned)(param->statssrv64 * 1000./delay):0);
				 break;				 
				case 'D':
				 i+=sprintf((char *)buf+i, "%u", delay);
				 break;
				case '.':
				 sprintf((char *)buf+i, "%.3u", msec);
				 i+=3;
				 break;
				case 'z':
				 sprintf((char *)buf+i, "%+.2ld%.2u", timezone / 60, (unsigned)(timezone%60));
				 i+=5;
				 break;
				case 'U':
				 if(param->username && *param->username){
					for(len = 0; i< 4000 && param->username[len]; len++){
					 buf[i] = param->username[len];
					 if(param->srv->nonprintable && (buf[i] < 0x20 || strchr((char *)param->srv->nonprintable, buf[i]))) buf[i] = param->srv->replace;
					 if(doublec && strchr((char *)doublec, buf[i])) {
						buf[i+1] = buf[i];
						i++;
					 }
					 i++;
					}
				 }
				 else {
					buf[i++] = '-';
				 }
				 break;
				case 'n':
					len = param->hostname? (int)strlen((char *)param->hostname) : 0;
					if (len > 0 && !strchr((char *)param->hostname, ':')) for(len = 0; param->hostname[len] && i < 256; len++, i++){
						buf[i] = param->hostname[len];
					 	if(param->srv->nonprintable && (buf[i] < 0x20 || strchr((char *)param->srv->nonprintable, buf[i]))) buf[i] = param->srv->replace;
						if(doublec && strchr((char *)doublec, buf[i])) {
							buf[i+1] = buf[i];
							i++;
						}
					}
					else {
						buf[i++] = '[';
						i += myinet_ntop(*SAFAMILY(&param->req), SAADDR(&param->req), (char *)buf + i, 64);
						buf[i++] = ']';
					}
					break;

				case 'N':
				 if(param->service < 15) {
					 len = (conf.stringtable)? (int)strlen((char *)conf.stringtable[SERVICES + param->service]) : 0;
					 if(len > 20) len = 20;
					 memcpy(buf+i, (len)?conf.stringtable[SERVICES + param->service]:(unsigned char*)"-", (len)?len:1);
					 i += (len)?len:1;
				 }
				 break;
				case 'E':
				 sprintf((char *)buf+i, "%.05d", param->res);
				 i += 5;
				 break;
				case 'T':
				 if(s){
					for(len = 0; i<4000 && s[len]; len++){
					 buf[i] = s[len];
					 if(param->srv->nonprintable && (buf[i] < 0x20 || strchr((char *)param->srv->nonprintable, buf[i]))) buf[i] = param->srv->replace;
					 if(doublec && strchr((char *)doublec, buf[i])) {
						buf[i+1] = buf[i];
						i++;
					 }
					 i++;
					}
				 }
				 break;
				case 'e':
				 i += myinet_ntop(*SAFAMILY(&param->sinsl), SAADDR(&param->sinsl), (char *)buf + i, 64);
				 break;
				case 'i':
				 i += myinet_ntop(*SAFAMILY(&param->sincl), SAADDR(&param->sincl), (char *)buf + i, 64);
				 break;
				case 'C':
				 i += myinet_ntop(*SAFAMILY(&param->sincr), SAADDR(&param->sincr), (char *)buf + i, 64);
				 break;
				case 'R':
				 i += myinet_ntop(*SAFAMILY(&param->sinsr), SAADDR(&param->sinsr), (char *)buf + i, 64);
				 break;
				case 'Q':
				 i += myinet_ntop(*SAFAMILY(&param->req), SAADDR(&param->req), (char *)buf + i, 64);
				 break;
				case 'p':
				 sprintf((char *)buf+i, "%hu", ntohs(*SAPORT(&param->srv->intsa)));
				 i += (int)strlen((char *)buf+i);
				 break;
				case 'c':
				 sprintf((char *)buf+i, "%hu", ntohs(*SAPORT(&param->sincr)));
				 i += (int)strlen((char *)buf+i);
				 break;
				case 'r':
				 sprintf((char *)buf+i, "%hu", ntohs(*SAPORT(&param->sinsr)));
				 i += (int)strlen((char *)buf+i);
				 break;
				case 'q':
				 sprintf((char *)buf+i, "%hu", ntohs(*SAPORT(&param->req)));
				 i += (int)strlen((char *)buf+i);
				 break;
				case 'L':
				 sprintf((char *)buf+i, "%"PRINTF_INT64_MODIFIER"u", param->cycles);
				 i += (int)strlen((char *)buf+i);
				 break;
				case 'I':
				 sprintf((char *)buf+i, "%"PRINTF_INT64_MODIFIER"u", param->statssrv64);
				 i += (int)strlen((char *)buf+i);
				 break;
				case 'O':
				 sprintf((char *)buf+i, "%"PRINTF_INT64_MODIFIER"u", param->statscli64);
				 i += (int)strlen((char *)buf+i);
				 break;
				case 'h':
				 sprintf((char *)buf+i, "%d", param->redirected);
				 i += (int)strlen((char *)buf+i);
				 break;
				case '1':
				case '2':
				case '3':
				case '4':
				case '5':
				case '6':
				case '7':
				case '8':
				case '9':
					{
						int k, pmin=0, pmax=0;
						for (k = j; isnumber(format[k]); k++);
						if(format[k] == '-' && isnumber(format[k+1])){
							pmin = atoi(format + j) - 1;
							k++;
							pmax = atoi(format + k) -1;
							for (; isnumber(format[k]); k++);
							j = k;
						}
						if(!s || format[k]!='T') break;
						for(k = 0, len = 0; s[len] && i < 4000; len++){
							if(isspace(s[len])){
								k++;
								while(isspace(s[len+1]))len++;
								if(k == pmin) continue;
							}
							if(k>=pmin && k<=pmax) {
								buf[i] = s[len];
								if(param->srv->nonprintable && (buf[i] < 0x20 || strchr((char *)param->srv->nonprintable, buf[i]))) buf[i] = param->srv->replace;
								if(doublec && strchr((char *)doublec, buf[i])) {
									buf[i+1] = buf[i];
									i++;
				 				}
								i++;
							}
						}
						break;

					}
				default:
				 buf[i++] = format[j];
			}
		}
		else buf[i++] = format[j];
	}
	buf[i] = 0;
	return i;
}

int dobuf(struct clientparam * param, unsigned char * buf, const unsigned char *s, const unsigned char * doublec){
	struct tm* tm;
	int i;
	char * format;
	time_t t;

	time(&t);
	if(!param) return 0;
	format = param->srv->logformat?(char *)param->srv->logformat : DEFLOGFORMAT;
	tm = (*format == 'G' || *format == 'g')?
		gmtime(&t) : localtime(&t);
	i = dobuf2(param, buf, s, doublec, tm, format + 1);
	return i;
}


static int stdloginit(const char * selector, int logtype, struct LOGGER *logger){
	char tmpuf[1024];
	struct stdlogdata *lp;
	lp = myalloc(sizeof(struct stdlogdata));
	if(!lp) return 1;
	logger->data = lp;
	if(!selector || !*selector){
		logger-rotate = NONE;
		lp->fp = stdout;
	}
	else {
		logger->rotate = logtype;
		lp->fp = fopen((char *)dologname (tmpbuf, conf.logname, NULL, logtype, time(NULL)), "a");
		if(!lp->fp){
			myfree(lp);
			return(2);
		}
	}
	return 0;
}

int stddobuf(struct clientparam * param, unsigned char * buf, const unsigned char *s){
	return dobuf(param, buf, s, NULL);
}

void stdlog(struct clientparam * param, const unsigned char *s, struct LOGGER *logger) {
	FILE *log = (struct stdlogdata *)logger->data;

	fprintf(log, "%s\n", buf);
	if(log == stdout || log == stderr)fflush(log);
}

static void stdlogrotate(struct LOGGER *logger){
 char tmpuf[1024];
 struct stdlogdata *lp = (struct stdlogdata)logger->data;
 if(lp->fp) lp->fp = freopen((char *)dologname (tmpbuf, logger->selector, NULL, logger->rotate, conf.time), "a", lp->fp);
 else lp->fp = fopen((char *)dologname (tmpbuf, logger->selector, NULL, logger->rotate, conf.time), "a");
 conf.logtime = conf.time;
 if(logger->rotate) {
	int t;
	t = 1;
	switch(logger->rotate){
		case ANNUALLY:
			t = t * 12;
		case MONTHLY:
			t = t * 4;
		case WEEKLY:
			t = t * 7;
		case DAILY:
			t = t * 24;
		case HOURLY:
			t = t * 60;
		case MINUTELY:
			t = t * 60;
		default:
			break;
	}
	dologname (tmpbuf, logger->selector, (conf.archiver)?conf.archiver[1]:NULL, logger->rotate, (conf.logtime - t * conf.rotate));
	remove ((char *) tmpbuf);
	if(conf.archiver) {
		int i;
		*tmpbuf = 0;
		for(i = 2; i < conf.archiverc && strlen((char *)tmpbuf) < 512; i++){
			strcat((char *)tmpbuf, " ");
			if(!strcmp((char *)conf.archiver[i], "%A")){
				strcat((char *)tmpbuf, "\"");
				dologname (tmpbuf + strlen((char *)tmpbuf), logger->selector, conf.archiver[1], logger->rotate, (conf.logtime - t));
				strcat((char *)tmpbuf, "\"");
			}
			else if(!strcmp((char *)conf.archiver[i], "%F")){
				strcat((char *)tmpbuf, "\"");
				dologname (tmpbuf+strlen((char *)tmpbuf), logger->selector, NULL, logger->rotate, (conf.logtime-t));
				strcat((char *)tmpbuf, "\"");
			}
			else
				strcat((char *)tmpbuf, (char *)conf.archiver[i]);
		}
		system((char *)tmpbuf+1);
	}
 }
}

static void stdlogclose(struct LOGGER *logger){
	fclose(((struct stdlogdata *)logger->data)->fp);
	myfree(((struct stdlogdata *)logger->data)->fp);
}

#if HAVESYSLOG > 0

static int sysloginit(const char * selector, int logtype, struct LOGGER *logger){
	openlog(selector+1, LOG_PID, LOG_DAEMON);
	logger->rotate = logtype;
	logger->data = NULL;
}

static void logsyslog(const char * buf, int len, struct LOGGER *logger) {

	syslog((param->res >= 90 && param->res<=99)?LOG_NOTICE:(param->res?LOG_WARNING:LOG_INFO), "%s", buf);
}

static void syslogrotate(struct LOGGER *logger){
	closelog();
	openlog(logger->selector+1, LOG_PID, LOG_DAEMON);
}

static void syslogclose(struct LOGGER *logger){
	closelog();
}


#endif

#if HAVESQL > 0

struct sqldata {
	SQLHENV  henv;
	SQLHSTMT hstmt;
	SQLHDBC hdbc;
	int attempt;
	time_t attempt_time;
};



static int sqlinit(const char * selector, int logtype, struct LOGGER *logger);
static void sqllog(struct clientparam * param, const unsigned char *s, LOGGER *logger);
static void sqlrotate(struct LOGGER *logger);


int sqlinit2(struct sqldata * sd, char * source){
	SQLRETURN  retcode;
	char * datasource;
	char * username;
	char * password;
	char * string;
	int ret = 0;

	retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &sd->henv);
	if (!henv || (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO)){
		return 1;
	}
	retcode = SQLSetEnvAttr(sd->henv, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0); 
	if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) {
		ret = 2;
		goto CLOSEENV:
	}
	retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &sd->hdbc); 
	if (!sd->hdbc || (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO)) {
		ret = 3;
		goto CLOSEENV:	
	}
       	SQLSetConnectAttr(sd->hdbc, SQL_LOGIN_TIMEOUT, (void*)15, 0);

	string = mystrdup(source);
	if(!string) goto CLOSEHDBC;
	datasource = strtok(string, ",");
	username = strtok(NULL, ",");
	password = strtok(NULL, ",");
	

         /* Connect to data source */
        retcode = SQLConnect(sd->hdbc, (SQLCHAR*) datasource, (SQLSMALLINT)strlen(datasource),
                (SQLCHAR*) username, (SQLSMALLINT)((username)?strlen(username):0),
                (SQLCHAR*) password, (SQLSMALLINT)((password)?strlen(password):0));

	myfree(string);



	if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO){
		ret = 4;
		goto CLOSEHDBC;
	}

        retcode = SQLAllocHandle(SQL_HANDLE_STMT, sd->hdbc, &sd->hstmt); 
        if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO){
		sd->hstmt = 0;
		ret = 5;
		goto CLOSEHDBC;
	}

	return 0;

CLOSEHDBC:
	SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
	sd->hdbc = 0;
CLOSEENV:
	SQLFreeHandle(SQL_HANDLE_ENV, henv);
	sd->henv = 0;
	return ret;
}

static int sqlinit(const char * selector, int logtype, struct LOGGER *logger){
	struct sqldata *sd;
	int res
	
	logger->rotate = logtype;
	sd = (struct sqldata *)myalloc(sizeof(struct sqldata));
	memset(sd, 0, sizeof(struct sqldata));
	loger->data = sd;
	if(!(res = sqlinit2(sd, selector+1))) {
		myfree(sd);
		return res;
	}
}

int sqldobuf(struct clientparam * param, unsigned char * buf, const unsigned char *s){
	return dobuf(param, buf, s, (unsigned char *)"\'");
}


static void sqllog(const char * buf, int len, struct LOGGER *logger){
	SQLRETURN ret;
	struct sqldata *sd = (struct sqldata *)logger->data;


	if(sd->attempt > 5){
		if (conf.time - sd->attempt_time < 180){
			return;
		}
	}
	if(sd->attempt){
		sd->attempt++;
		sqlrotate(logger);

		if(!sd->hstmt){
			sd->attempt_time=conf.time;
			return;
		}
	}
	ret = SQLExecDirect(sd->hstmt, (SQLCHAR *)buf, (SQLINTEGER)len);
	if(ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO){
		sqlrotate(logger);
		if(sd->hstmt) {
			ret = SQLExecDirect(hstmt, (SQLCHAR *)buf, (SQLINTEGER)len);
			if(ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO){
				sd->attempt++;
				sd->attempt_time=conf.time;
				return;
			}
		}
	}
	sd->attempt=0;
}

static void sqlrotate(struct LOGGER *logger){
	struct sqldata * sd;
	sqlclose(logger);
	sd = (struct sqldata *)myalloc(sizeof(struct sqldata));
	memset(sd, 0, sizeof(struct sqldata));
	loger->data = sd;
	sqlinit2(sd, logger->selector+1)
}

static void sqlclose(struct LOGGER *logger){
	struct sqldata *sd = (struct sqldata *)loger->data;
	if(sd->hstmt) {
		SQLFreeHandle(SQL_HANDLE_STMT, sd->hstmt);
		sd->hstmt = NULL;
	}
	if(sd->hdbc){
		SQLDisconnect(sd->hdbc);
		SQLFreeHandle(SQL_HANDLE_DBC, sd->hdbc);
		sd->hdbc = NULL;
	}
	if(sd->henv) {
		SQLFreeHandle(SQL_HANDLE_ENV, sd->henv);
		sd->henv = NULL;
	}
	myfree(sd);
}


#endif