/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef PACKAGE_DATA_READER_H
#define PACKAGE_DATA_READER_H


#include "DataReader.h"


class PackageData;


class PackageDataReader : public DataReader {
public:
								PackageDataReader(DataReader* dataReader);
	virtual						~PackageDataReader();

	virtual	status_t			Init(const PackageData& data) = 0;

	virtual	uint64				Size() const = 0;
	virtual	size_t				BlockSize() const = 0;

protected:
			DataReader*			fDataReader;
};


class PackageDataReaderFactory {
public:
	static	status_t			CreatePackageDataReader(DataReader* dataReader,
									const PackageData& data,
									PackageDataReader*& _reader);
};


#endif	// PACKAGE_DATA_READER_H
