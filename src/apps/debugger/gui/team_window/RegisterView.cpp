/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */

#include "RegisterView.h"

#include <stdio.h>

#include <new>

#include "table/TableColumns.h"

#include "Architecture.h"
#include "CpuState.h"
#include "Register.h"


// #pragma mark - RegisterValueColumn


class RegisterView::RegisterValueColumn : public StringTableColumn {
public:
	RegisterValueColumn(int32 modelIndex, const char* title, float width,
		float minWidth, float maxWidth, uint32 truncate = B_TRUNCATE_MIDDLE,
		alignment align = B_ALIGN_RIGHT)
		:
		StringTableColumn(modelIndex, title, width, minWidth, maxWidth,
			truncate, align)
	{
	}

protected:
	virtual BField* PrepareField(const BVariant& value) const
	{
		char buffer[64];
		return StringTableColumn::PrepareField(
			BVariant(_ToString(value, buffer, sizeof(buffer)),
				B_VARIANT_DONT_COPY_DATA));
	}

	virtual int CompareValues(const BVariant& a, const BVariant& b)
	{
		// If neither value is a number, compare the strings. If only one value
		// is a number, it is considered to be greater.
		if (!a.IsNumber()) {
			if (b.IsNumber())
				return -1;
			char bufferA[64];
			char bufferB[64];
			return StringTableColumn::CompareValues(
				BVariant(_ToString(a, bufferA, sizeof(bufferA)),
					B_VARIANT_DONT_COPY_DATA),
				BVariant(_ToString(b, bufferB, sizeof(bufferB)),
					B_VARIANT_DONT_COPY_DATA));
		}

		if (!b.IsNumber())
			return 1;

		// If either value is floating point, we compare floating point values.
		if (a.IsFloat() || b.IsFloat()) {
			double valueA = a.ToDouble();
			double valueB = b.ToDouble();
			return valueA < valueB ? -1 : (valueA == valueB ? 0 : 1);
		}

		uint64 valueA = a.ToUInt64();
		uint64 valueB = b.ToUInt64();
		return valueA < valueB ? -1 : (valueA == valueB ? 0 : 1);
	}

private:
	const char* _ToString(const BVariant& value, char* buffer,
		size_t bufferSize) const
	{
		if (!value.IsNumber())
			return value.ToString();

		switch (value.Type()) {
			case B_FLOAT_TYPE:
			case B_DOUBLE_TYPE:
				snprintf(buffer, bufferSize, "%g", value.ToDouble());
				break;
			case B_INT8_TYPE:
			case B_UINT8_TYPE:
				snprintf(buffer, bufferSize, "0x%02x", value.ToUInt8());
				break;
			case B_INT16_TYPE:
			case B_UINT16_TYPE:
				snprintf(buffer, bufferSize, "0x%04x", value.ToUInt16());
				break;
			case B_INT32_TYPE:
			case B_UINT32_TYPE:
				snprintf(buffer, bufferSize, "0x%08lx", value.ToUInt32());
				break;
			case B_INT64_TYPE:
			case B_UINT64_TYPE:
			default:
				snprintf(buffer, bufferSize, "0x%016llx", value.ToUInt64());
				break;
		}

		return buffer;
	}
};


// #pragma mark - RegisterTableModel


class RegisterView::RegisterTableModel : public TableModel {
public:
	RegisterTableModel(Architecture* architecture)
		:
		fArchitecture(architecture),
		fCpuState(NULL)
	{
	}

	~RegisterTableModel()
	{
	}

	void SetCpuState(CpuState* cpuState)
	{
		fCpuState = cpuState;

		NotifyRowsChanged(0, CountRows());
	}

	virtual int32 CountColumns() const
	{
		return 2;
	}

	virtual int32 CountRows() const
	{
		return fArchitecture->CountRegisters();
	}

	virtual bool GetValueAt(int32 rowIndex, int32 columnIndex, BVariant& value)
	{
		if (rowIndex < 0 || rowIndex >= fArchitecture->CountRegisters())
			return false;

		const Register* reg = fArchitecture->Registers() + rowIndex;

		switch (columnIndex) {
			case 0:
				value.SetTo(reg->Name(), B_VARIANT_DONT_COPY_DATA);
				return true;
			case 1:
				if (fCpuState == NULL)
					return false;
				if (!fCpuState->GetRegisterValue(reg, value))
					value.SetTo("?", B_VARIANT_DONT_COPY_DATA);
				return true;
			default:
				return false;
		}
	}

private:
	Architecture*	fArchitecture;
	CpuState*		fCpuState;
};


// #pragma mark - RegisterView


RegisterView::RegisterView(Architecture* architecture)
	:
	BGroupView(B_VERTICAL),
	fArchitecture(architecture),
	fRegisterTable(NULL),
	fRegisterTableModel(NULL)
{
	SetName("Registers");
}


RegisterView::~RegisterView()
{
	SetCpuState(NULL);
	fRegisterTable->SetTableModel(NULL);
	delete fRegisterTableModel;
}


/*static*/ RegisterView*
RegisterView::Create(Architecture* architecture)
{
	RegisterView* self = new RegisterView(architecture);

	try {
		self->_Init();
	} catch (...) {
		delete self;
		throw;
	}

	return self;
}


void
RegisterView::SetCpuState(CpuState* cpuState)
{
	if (cpuState == fCpuState)
		return;

	if (fCpuState != NULL)
		fCpuState->RemoveReference();

	fCpuState = cpuState;

	if (fCpuState != NULL)
		fCpuState->AddReference();

	fRegisterTableModel->SetCpuState(fCpuState);
}


void
RegisterView::TableRowInvoked(Table* table, int32 rowIndex)
{
}


void
RegisterView::_Init()
{
	fRegisterTable = new Table("register list", 0, B_FANCY_BORDER);
	AddChild(fRegisterTable->ToView());

	// columns
	fRegisterTable->AddColumn(new StringTableColumn(0, "Register", 80, 40, 1000,
		B_TRUNCATE_END, B_ALIGN_LEFT));
	fRegisterTable->AddColumn(new RegisterValueColumn(1, "Value", 80, 40, 1000,
		B_TRUNCATE_END, B_ALIGN_RIGHT));

	fRegisterTableModel = new RegisterTableModel(fArchitecture);
	fRegisterTable->SetTableModel(fRegisterTableModel);

	fRegisterTable->AddTableListener(this);
}
