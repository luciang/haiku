/*
 * Copyright 2004-2007, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Copyright 2003, Jeff Ward, jeff@r2d2.stcloudstate.edu. All rights reserved.
 *
 * Distributed under the terms of the MIT License.
 */


#include <KernelExport.h>

#include <arch/real_time_clock.h>
#include <commpage.h>
#include <real_time_clock.h>
#include <real_time_data.h>
#include <syscalls.h>
#include <thread.h>

#include <stdlib.h>

#define TRACE_TIME
#ifdef TRACE_TIME
#	define TRACE(x) dprintf x
#else
#	define TRACE(x)
#endif


#define RTC_SECONDS_DAY 86400
#define RTC_EPOCH_JULIAN_DAY 2440588
	// January 1st, 1970

static struct real_time_data *sRealTimeData;
static bool sIsGMT = false;
static char sTimezoneFilename[B_PATH_NAME_LENGTH] = "";
static bigtime_t sTimezoneOffset = 0;
static bool sDaylightSavingTime = false;


/*! Write the system time to CMOS. */
static void
rtc_system_to_hw(void)
{
	uint32 seconds;

	seconds = (arch_rtc_get_system_time_offset(sRealTimeData) + system_time()
		+ (sIsGMT ? 0 : sTimezoneOffset)) / 1000000;

	arch_rtc_set_hw_time(seconds);
}


/*! Read the CMOS clock and update the system time accordingly. */
static void
rtc_hw_to_system(void)
{
	uint32 current_time;

	current_time = arch_rtc_get_hw_time();
	set_real_time_clock(current_time + (sIsGMT ? 0 : sTimezoneOffset));
}


bigtime_t
rtc_boot_time(void)
{
	return arch_rtc_get_system_time_offset(sRealTimeData);
}


static int
rtc_debug(int argc, char **argv)
{
	if (argc < 2) {
		// If no arguments were given, output all useful data.
		uint32 currentTime;
		bigtime_t systemTimeOffset
			= arch_rtc_get_system_time_offset(sRealTimeData);

		currentTime = (systemTimeOffset + system_time()) / 1000000;
		dprintf("system_time:  %Ld\n", system_time());
		dprintf("system_time_offset:    %Ld\n", systemTimeOffset);
		dprintf("current_time: %lu\n", currentTime);
	} else {
		// If there was an argument, reset the system and hw time.
		set_real_time_clock(strtoul(argv[1], NULL, 10));
	}

	return 0;
}


status_t
rtc_init(kernel_args *args)
{
	sRealTimeData = (struct real_time_data*)allocate_commpage_entry(
		COMMPAGE_ENTRY_REAL_TIME_DATA, sizeof(struct real_time_data));

	arch_rtc_init(args, sRealTimeData);
	rtc_hw_to_system();

	add_debugger_command("rtc", &rtc_debug, "Set and test the real-time clock");
	return B_OK;
}


//	#pragma mark - public kernel API


void
set_real_time_clock(uint32 currentTime)
{
	arch_rtc_set_system_time_offset(sRealTimeData,
		currentTime * 1000000LL - system_time());
	rtc_system_to_hw();
}


uint32
real_time_clock(void)
{
	return (arch_rtc_get_system_time_offset(sRealTimeData) + system_time())
		/ 1000000;
}


bigtime_t
real_time_clock_usecs(void)
{
	return arch_rtc_get_system_time_offset(sRealTimeData) + system_time();
}


status_t
get_rtc_info(rtc_info *info)
{
	if (info == NULL)
		return B_BAD_VALUE;

	info->time = real_time_clock();
	info->is_gmt = sIsGMT;
	info->tz_minuteswest = sTimezoneOffset / 1000000LL;
	info->tz_dsttime = sDaylightSavingTime;

	return B_OK;
}


// #pragma mark -


/*!	Converts the \a tm data to seconds. Note that the base year is not
	1900 as in POSIX, but 1970.
*/
uint32
rtc_tm_to_secs(const struct tm *tm)
{
	uint32 days;
	int year, month;

	month = tm->tm_mon + 1;
	year = tm->tm_year + RTC_EPOCH_BASE_YEAR;

	// Reference: Fliegel, H. F. and van Flandern, T. C. (1968).
	// Communications of the ACM, Vol. 11, No. 10 (October, 1968).
	days = tm->tm_mday - 32075 - RTC_EPOCH_JULIAN_DAY
		+ 1461 * (year + 4800 + (month - 14) / 12) / 4
		+ 367 * (month - 2 - 12 * ((month - 14) / 12)) / 12
		- 3 * ((year + 4900 + (month - 14) / 12) / 100) / 4;

	return days * RTC_SECONDS_DAY + tm->tm_hour * 3600 + tm->tm_min * 60
		+ tm->tm_sec;
}


void
rtc_secs_to_tm(uint32 seconds, struct tm *t)
{
	uint32 year, month, day, l, n;

	// Reference: Fliegel, H. F. and van Flandern, T. C. (1968).
	// Communications of the ACM, Vol. 11, No. 10 (October, 1968).
	l = seconds / 86400 + 68569 + RTC_EPOCH_JULIAN_DAY;
	n = 4 * l / 146097;
	l = l - (146097 * n + 3) / 4;
	year = 4000 * (l + 1) / 1461001;
	l = l - 1461 * year / 4 + 31;
	month = 80 * l / 2447;
	day = l - 2447 * month / 80;
	l = month / 11;
	month = month + 2 - 12 * l;
	year = 100 * (n - 49) + year + l;

	t->tm_mday = day;
	t->tm_mon = month - 1;
	t->tm_year = year - RTC_EPOCH_BASE_YEAR;

	seconds = seconds % RTC_SECONDS_DAY;
	t->tm_hour = seconds / 3600;
 
	seconds = seconds % 3600;
	t->tm_min = seconds / 60;
	t->tm_sec = seconds % 60;
}


//	#pragma mark -


/*!	This is called from the gettimeofday() implementation that's part of the
	kernel.
*/
status_t
_kern_get_timezone(time_t *_timezoneOffset, bool *_daylightSavingTime)
{
	*_timezoneOffset = (time_t)(sTimezoneOffset / 1000000LL);
	*_daylightSavingTime = sDaylightSavingTime;

	return B_OK;
}


//	#pragma mark - syscalls


bigtime_t
_user_system_time(void)
{
	syscall_64_bit_return_value();

	return system_time();
}


status_t
_user_set_real_time_clock(uint32 time)
{
	if (geteuid() != 0)
		return B_NOT_ALLOWED;

	set_real_time_clock(time);
	return B_OK;
}


status_t
_user_set_timezone(time_t timezoneOffset, bool daylightSavingTime)
{
	bigtime_t offset = (bigtime_t)timezoneOffset * 1000000LL;

	if (geteuid() != 0)
		return B_NOT_ALLOWED;

	TRACE(("old system_time_offset %Ld old %Ld new %Ld gmt %d\n",
		arch_rtc_get_system_time_offset(sRealTimeData), sTimezoneOffset,
		offset, sIsGMT));

	// We only need to update our time offset if the hardware clock
	// does not run in the local timezone.
	// Since this is shared data, we need to update it atomically.
	if (!sIsGMT) {
		arch_rtc_set_system_time_offset(sRealTimeData,
			arch_rtc_get_system_time_offset(sRealTimeData) + sTimezoneOffset
				- offset);
	}

	sTimezoneOffset = offset;
	sDaylightSavingTime = daylightSavingTime;

	TRACE(("new system_time_offset %Ld\n",
		arch_rtc_get_system_time_offset(sRealTimeData)));
	
	return B_OK;
}


status_t
_user_get_timezone(time_t *_timezoneOffset, bool *_daylightSavingTime)
{
	time_t offset = (time_t)(sTimezoneOffset / 1000000LL);

	if (!IS_USER_ADDRESS(_timezoneOffset)
		|| !IS_USER_ADDRESS(_daylightSavingTime)
		|| user_memcpy(_timezoneOffset, &offset, sizeof(time_t)) < B_OK
		|| user_memcpy(_daylightSavingTime, &sDaylightSavingTime,
				sizeof(bool)) < B_OK)
		return B_BAD_ADDRESS;

	return B_OK;
}


status_t
_user_set_tzfilename(const char *filename, size_t length, bool isGMT)
{
	// store previous value
	bool wasGMT = sIsGMT;
	if (geteuid() != 0)
		return B_NOT_ALLOWED;

	if (!IS_USER_ADDRESS(filename)
		|| filename == NULL
		|| user_strlcpy(sTimezoneFilename, filename,
				B_PATH_NAME_LENGTH) < B_OK)
		return B_BAD_ADDRESS;

	sIsGMT = isGMT;

	if (wasGMT != sIsGMT)
                arch_rtc_set_system_time_offset(sRealTimeData,
                        arch_rtc_get_system_time_offset(sRealTimeData) + (((sIsGMT) ? 1 : -1) * sTimezoneOffset));
 
	return B_OK;
}


status_t
_user_get_tzfilename(char *filename, size_t length, bool *_isGMT)
{
	if (filename == NULL || _isGMT == NULL
		|| !IS_USER_ADDRESS(filename) || !IS_USER_ADDRESS(_isGMT)
		|| user_strlcpy(filename, sTimezoneFilename, length) < B_OK
		|| user_memcpy(_isGMT, &sIsGMT, sizeof(bool)) < B_OK)
		return B_BAD_ADDRESS;

	return B_OK;
}

