/* 
** Copyright 2003, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
** Distributed under the terms of the OpenBeOS License.
*/


#include <Country.h>

#include <assert.h>

#include <String.h>

#include <unicode/datefmt.h>
#include <unicode/dcfmtsym.h>
#include <unicode/decimfmt.h>
#include <unicode/dtfmtsym.h>
#include <unicode/smpdtfmt.h>
#include <ICUWrapper.h>

#include <monetary.h>
#include <stdarg.h>


const char* gStrings[] = {
	// date/time format
	"",
	"",
	// short date/time format
	"",
	"",
	// am/pm string
	"AM",
	"PM",
	// separators
	".",
	":",

	// currency/monetary
	"."
	","
};


BCountry::BCountry(const char* languageCode, const char* countryCode)
	:
	fStrings(gStrings)
{
	fICULocale = new icu_4_2::Locale(languageCode, countryCode);
}


BCountry::BCountry(const char* languageAndCountryCode)
	:
	fStrings(gStrings)
{
	fICULocale = new icu_4_2::Locale(languageAndCountryCode);
	fICULongDateFormatter = DateFormat::createDateInstance(
		DateFormat::FULL, *fICULocale);
 	fICUShortDateFormatter = DateFormat::createDateInstance(
		DateFormat::SHORT, *fICULocale);
}


BCountry::~BCountry()
{
	delete fICULocale;
}


bool
BCountry::Name(BString& name) const
{
	UnicodeString uString;
	fICULocale->getDisplayName(uString);
	BStringByteSink stringConverter(&name);
	uString.toUTF8(stringConverter);
	return true;
}


const char*
BCountry::Code() const
{
	return fICULocale->getName();
}


// TODO use ICU backend keywords instead
const char*
BCountry::GetString(uint32 id) const
{
	if (id < B_COUNTRY_STRINGS_BASE || id >= B_NUM_COUNTRY_STRINGS)
		return NULL;

	return gStrings[id - B_COUNTRY_STRINGS_BASE];
}


void
BCountry::FormatDate(char* string, size_t maxSize, time_t time, bool longFormat)
{
	BString fullString;
	FormatDate(&fullString, time, longFormat);
	strncpy(string, fullString.String(), maxSize);
}


void
BCountry::FormatDate(BString *string, time_t time, bool longFormat)
{
	// TODO: ICU allows for 4 different levels of expansion :
	// short, medium, long, and full. Our bool parameter is not enough...
	icu_4_2::DateFormat* dateFormatter
		= longFormat ? fICULongDateFormatter : fICUShortDateFormatter;
	UnicodeString ICUString;
	ICUString = dateFormatter->format((UDate)time * 1000, ICUString);

	string->Truncate(0);
	BStringByteSink stringConverter(string);

	ICUString.toUTF8(stringConverter);
}


void
BCountry::FormatTime(char* string, size_t maxSize, time_t time, bool longFormat)
{
	BString fullString;
	FormatTime(&fullString, time, longFormat);
	strncpy(string, fullString.String(), maxSize);
}


void
BCountry::FormatTime(BString* string, time_t time, bool longFormat)
{
	// TODO: ICU allows for 4 different levels of expansion :
	// short, medium, long, and full. Our bool parameter is not enough...
	icu_4_2::DateFormat* timeFormatter;
 	timeFormatter = DateFormat::createTimeInstance(
		longFormat ? DateFormat::FULL : DateFormat::SHORT,
		*fICULocale);
	UnicodeString ICUString;
	ICUString = timeFormatter->format((UDate)time * 1000, ICUString);

	string->Truncate(0);
	BStringByteSink stringConverter(string);

	ICUString.toUTF8(stringConverter);
}


bool
BCountry::DateFormat(BString& format, bool longFormat) const
{
	icu_4_2::DateFormat* dateFormatter
		= longFormat ? fICULongDateFormatter : fICUShortDateFormatter;
	SimpleDateFormat* dateFormatterImpl
		= static_cast<SimpleDateFormat*>(dateFormatter);

	UnicodeString ICUString;
	ICUString = dateFormatterImpl->toPattern(ICUString);

	BStringByteSink stringConverter(&format);

	ICUString.toUTF8(stringConverter);

	return true;
}


void
BCountry::SetDateFormat(const char* formatString, bool longFormat)
{
	icu_4_2::DateFormat* dateFormatter
		= longFormat ? fICULongDateFormatter : fICUShortDateFormatter;
	SimpleDateFormat* dateFormatterImpl
		= static_cast<SimpleDateFormat*>(dateFormatter);

	UnicodeString pattern(formatString);
	dateFormatterImpl->applyPattern(pattern);
}


bool
BCountry::TimeFormat(BString& format, bool longFormat) const
{
	icu_4_2::DateFormat* dateFormatter;
 	dateFormatter = DateFormat::createTimeInstance(
		longFormat ? DateFormat::FULL : DateFormat::SHORT,
		*fICULocale);
	SimpleDateFormat* dateFormatterImpl
		= static_cast<SimpleDateFormat*>(dateFormatter);

	UnicodeString ICUString;
	ICUString = dateFormatterImpl->toPattern(ICUString);

	BStringByteSink stringConverter(&format);

	ICUString.toUTF8(stringConverter);

	return true;
}


// TODO find how to get it from ICU (setting it is ok, we use the pattern-string
// for that)
// Or remove this function ?
const char*
BCountry::DateSeparator() const
{
	return fStrings[B_DATE_SEPARATOR];
}


const char*
BCountry::TimeSeparator() const
{
	return fStrings[B_TIME_SEPARATOR];
}


void
BCountry::FormatNumber(char* string, size_t maxSize, double value)
{
	BString fullString;
	FormatNumber(&fullString, value);
	strncpy(string, fullString.String(), maxSize);
}


status_t
BCountry::FormatNumber(BString* string, double value)
{
	UErrorCode err = U_ZERO_ERROR;
	NumberFormat* numberFormatter
		= NumberFormat::createInstance(*fICULocale, NumberFormat::kNumberStyle,
			err);

	// Warning: we're returning an ICU error here but the type is status_t.
	if (U_FAILURE(err)) return err;

	UnicodeString ICUString;
	ICUString = numberFormatter->format(value, ICUString);

	string->Truncate(0);
	BStringByteSink stringConverter(string);

	ICUString.toUTF8(stringConverter);

	return U_ZERO_ERROR;
}


void
BCountry::FormatNumber(char* string, size_t maxSize, int32 value)
{
	BString fullString;
	FormatNumber(&fullString, value);
	strncpy(string, fullString.String(), maxSize);
}


void
BCountry::FormatNumber(BString* string, int32 value)
{
	UErrorCode err;
	NumberFormat* numberFormatter
		= NumberFormat::createInstance(*fICULocale, err);

	assert(err == U_ZERO_ERROR);

	UnicodeString ICUString;
	ICUString = numberFormatter->format((int32_t)value, ICUString);

	string->Truncate(0);
	BStringByteSink stringConverter(string);

	ICUString.toUTF8(stringConverter);
}


// This will only work for locales using the decimal system...
bool
BCountry::DecimalPoint(BString& format) const
{
	UErrorCode err;
	NumberFormat* numberFormatter
		= NumberFormat::createInstance(*fICULocale, err);

	assert(err == U_ZERO_ERROR);

	DecimalFormat* decimalFormatter
		= dynamic_cast<DecimalFormat*>(numberFormatter);

	assert(decimalFormatter != NULL);

	const DecimalFormatSymbols* syms
		= decimalFormatter->getDecimalFormatSymbols();

	UnicodeString ICUString;
	ICUString = syms->getSymbol(DecimalFormatSymbols::kDecimalSeparatorSymbol);

	BStringByteSink stringConverter(&format);

	ICUString.toUTF8(stringConverter);

	return true;
}


bool
BCountry::ThousandsSeparator(BString& separator) const
{
	UErrorCode err;
	NumberFormat* numberFormatter
		= NumberFormat::createInstance(*fICULocale, err);
	assert(err == U_ZERO_ERROR);
	DecimalFormat* decimalFormatter
		= dynamic_cast<DecimalFormat*>(numberFormatter);

	assert(decimalFormatter != NULL);

	const DecimalFormatSymbols* syms
		= decimalFormatter->getDecimalFormatSymbols();

	UnicodeString ICUString;
	ICUString = syms->getSymbol(DecimalFormatSymbols::kPatternSeparatorSymbol);

	BStringByteSink stringConverter(&separator);

	ICUString.toUTF8(stringConverter);

	return true;
}


bool
BCountry::Grouping(BString& grouping) const
{
	UErrorCode err;
	NumberFormat* numberFormatter
		= NumberFormat::createInstance(*fICULocale, err);
	assert(err == U_ZERO_ERROR);
	DecimalFormat* decimalFormatter
		= dynamic_cast<DecimalFormat*>(numberFormatter);

	assert(decimalFormatter != NULL);

	const DecimalFormatSymbols* syms
		= decimalFormatter->getDecimalFormatSymbols();

	UnicodeString ICUString;
	ICUString = syms->getSymbol(DecimalFormatSymbols::kGroupingSeparatorSymbol);

	BStringByteSink stringConverter(&grouping);

	ICUString.toUTF8(stringConverter);

	return true;
}


bool
BCountry::PositiveSign(BString& sign) const
{
	UErrorCode err;
	NumberFormat* numberFormatter
		= NumberFormat::createInstance(*fICULocale, err);
	assert(err == U_ZERO_ERROR);
	DecimalFormat* decimalFormatter
		= dynamic_cast<DecimalFormat*>(numberFormatter);

	assert(decimalFormatter != NULL);

	const DecimalFormatSymbols* syms
		= decimalFormatter->getDecimalFormatSymbols();

	UnicodeString ICUString;
	ICUString = syms->getSymbol(DecimalFormatSymbols::kPlusSignSymbol);

	BStringByteSink stringConverter(&sign);

	ICUString.toUTF8(stringConverter);

	return true;
}


bool
BCountry::NegativeSign(BString& sign) const
{
	UErrorCode err;
	NumberFormat* numberFormatter
		= NumberFormat::createInstance(*fICULocale, err);
	assert(err == U_ZERO_ERROR);
	DecimalFormat* decimalFormatter
		= dynamic_cast<DecimalFormat*>(numberFormatter);

	assert(decimalFormatter != NULL);

	const DecimalFormatSymbols* syms
		= decimalFormatter->getDecimalFormatSymbols();

	UnicodeString ICUString;
	ICUString = syms->getSymbol(DecimalFormatSymbols::kMinusSignSymbol);

	BStringByteSink stringConverter(&sign);

	ICUString.toUTF8(stringConverter);

	return true;
}


// TODO does ICU even support this ? Is it in the keywords ?
int8
BCountry::Measurement() const
{
	return B_US;
}


ssize_t
BCountry::FormatMonetary(char* string, size_t maxSize, double value)
{
	BString fullString;
	FormatMonetary(&fullString, value);
	strncpy(string, fullString.String(), maxSize);
}


ssize_t
BCountry::FormatMonetary(BString* string, double value)
{
	UErrorCode err;
	NumberFormat* numberFormatter
		= NumberFormat::createCurrencyInstance(*fICULocale, err);

	assert(err == U_ZERO_ERROR);

	UnicodeString ICUString;
	ICUString = numberFormatter->format(value, ICUString);

	string->Truncate(0);
	BStringByteSink stringConverter(string);

	ICUString.toUTF8(stringConverter);

	return B_OK;
}


bool
BCountry::CurrencySymbol(BString& symbol) const
{
	UErrorCode err;
	NumberFormat* numberFormatter
		= NumberFormat::createCurrencyInstance(*fICULocale, err);
	assert(err == U_ZERO_ERROR);
	DecimalFormat* decimalFormatter
		= dynamic_cast<DecimalFormat*>(numberFormatter);

	assert(decimalFormatter != NULL);

	const DecimalFormatSymbols* syms
		= decimalFormatter->getDecimalFormatSymbols();

	UnicodeString ICUString;
	ICUString = syms->getSymbol(DecimalFormatSymbols::kCurrencySymbol);

	BStringByteSink stringConverter(&symbol);

	ICUString.toUTF8(stringConverter);

	return true;
}


bool
BCountry::InternationalCurrencySymbol(BString& symbol) const
{
	UErrorCode err;
	NumberFormat* numberFormatter
		= NumberFormat::createCurrencyInstance(*fICULocale, err);
	assert(err == U_ZERO_ERROR);
	DecimalFormat* decimalFormatter
		= dynamic_cast<DecimalFormat*>(numberFormatter);

	assert(decimalFormatter != NULL);

	const DecimalFormatSymbols* syms
		= decimalFormatter->getDecimalFormatSymbols();

	UnicodeString ICUString;
	ICUString = syms->getSymbol(DecimalFormatSymbols::kIntlCurrencySymbol);

	BStringByteSink stringConverter(&symbol);

	ICUString.toUTF8(stringConverter);

	return true;
}


bool
BCountry::MonDecimalPoint(BString& decimal) const
{
	UErrorCode err;
	NumberFormat* numberFormatter
		= NumberFormat::createCurrencyInstance(*fICULocale, err);
	assert(err == U_ZERO_ERROR);
	DecimalFormat* decimalFormatter
		= dynamic_cast<DecimalFormat*>(numberFormatter);

	assert(decimalFormatter != NULL);

	const DecimalFormatSymbols* syms
		= decimalFormatter->getDecimalFormatSymbols();

	UnicodeString ICUString;
	ICUString = syms->getSymbol(DecimalFormatSymbols::kMonetarySeparatorSymbol);

	BStringByteSink stringConverter(&decimal);

	ICUString.toUTF8(stringConverter);

	return true;
}


bool
BCountry::MonThousandsSeparator(BString& separator) const
{
	UErrorCode err;
	NumberFormat* numberFormatter
		= NumberFormat::createCurrencyInstance(*fICULocale, err);
	assert(err == U_ZERO_ERROR);
	DecimalFormat* decimalFormatter
		= dynamic_cast<DecimalFormat*>(numberFormatter);

	assert(decimalFormatter != NULL);

	const DecimalFormatSymbols* syms
		= decimalFormatter->getDecimalFormatSymbols();

	UnicodeString ICUString;
	ICUString = syms->getSymbol(DecimalFormatSymbols::kPatternSeparatorSymbol);

	BStringByteSink stringConverter(&separator);

	ICUString.toUTF8(stringConverter);

	return true;
}


bool
BCountry::MonGrouping(BString& grouping) const
{
	UErrorCode err;
	NumberFormat* numberFormatter
		= NumberFormat::createCurrencyInstance(*fICULocale, err);
	assert(err == U_ZERO_ERROR);
	DecimalFormat* decimalFormatter
		= dynamic_cast<DecimalFormat*>(numberFormatter);

	assert(decimalFormatter != NULL);

	const DecimalFormatSymbols* syms
		= decimalFormatter->getDecimalFormatSymbols();

	UnicodeString ICUString;
	ICUString = syms->getSymbol(
		DecimalFormatSymbols::kMonetaryGroupingSeparatorSymbol);

	BStringByteSink stringConverter(&grouping);

	ICUString.toUTF8(stringConverter);

	return true;
}


// TODO: is this possible to get from ICU ?
int32
BCountry::MonFracDigits() const
{
	return 2;
}

