#include <time.h>  // rtc
#include "rtc_time.h"

void GetNowTime(INT *tm_year, INT *tm_hour, INT *tm_min, INT *tm_wday)
{
	time_t now = time(NULL);
	struct tm timeIn4 = {0};
	localtime_r(&now, &timeIn4);
	if(tm_year) *tm_year = timeIn4.tm_year;
	if(tm_hour) *tm_hour = timeIn4.tm_hour;
	if(tm_min) *tm_min = timeIn4.tm_min;
	if(tm_wday) *tm_wday = timeIn4.tm_wday;
	// printf("Current time: %s, wDay: %d \n", asctime(&timeIn4), *tm_wday);
}
