/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */


#include "TypeHandlerRoster.h"

#include <new>

#include <AutoDeleter.h>
#include <AutoLocker.h>

#include "AddressValueNode.h"
#include "ArrayValueNode.h"
#include "CompoundValueNode.h"
#include "EnumerationValueNode.h"
#include "PointerToMemberValueNode.h"
#include "PrimitiveValueNode.h"
#include "Type.h"
#include "TypeHandler.h"


// #pragma mark - BasicTypeHandler


namespace {


template<typename TypeClass, typename NodeClass>
class BasicTypeHandler : public TypeHandler {
public:
	virtual float SupportsType(Type* type)
	{
		return dynamic_cast<TypeClass*>(type) != NULL ? 0.5f : 0;
	}

	virtual status_t CreateValueNode(ValueNodeChild* nodeChild,
		Type* type, ValueNode*& _node)
	{
		TypeClass* supportedType = dynamic_cast<TypeClass*>(type);
		if (supportedType == NULL)
			return B_BAD_VALUE;

		ValueNode* node = new(std::nothrow) NodeClass(nodeChild, supportedType);
		if (node == NULL)
			return B_NO_MEMORY;

		_node = node;
		return B_OK;
	}
};


}	// unnamed namespace


// #pragma mark - TypeHandlerRoster


/*static*/ TypeHandlerRoster* TypeHandlerRoster::sDefaultInstance = NULL;


TypeHandlerRoster::TypeHandlerRoster()
	:
	fLock("type handler roster")
{
}


TypeHandlerRoster::~TypeHandlerRoster()
{
}

/*static*/ TypeHandlerRoster*
TypeHandlerRoster::Default()
{
	return sDefaultInstance;
}


/*static*/ status_t
TypeHandlerRoster::CreateDefault()
{
	if (sDefaultInstance != NULL)
		return B_OK;

	TypeHandlerRoster* roster = new(std::nothrow) TypeHandlerRoster;
	if (roster == NULL)
		return B_NO_MEMORY;
	ObjectDeleter<TypeHandlerRoster> rosterDeleter(roster);

	status_t error = roster->Init();
	if (error != B_OK)
		return error;

	error = roster->RegisterDefaultHandlers();
	if (error != B_OK)
		return error;

	sDefaultInstance = rosterDeleter.Detach();
	return B_OK;
}


/*static*/ void
TypeHandlerRoster::DeleteDefault()
{
	TypeHandlerRoster* roster = sDefaultInstance;
	sDefaultInstance = NULL;
	delete roster;
}


status_t
TypeHandlerRoster::Init()
{
	return fLock.InitCheck();
}


status_t
TypeHandlerRoster::RegisterDefaultHandlers()
{
	TypeHandler* handler;
	Reference<TypeHandler> handlerReference;

	#undef REGISTER_HANDLER
	#define REGISTER_HANDLER(name)								\
		handler = new(std::nothrow)								\
			BasicTypeHandler<name##Type, name##ValueNode>();	\
		handlerReference.SetTo(handler, true);					\
		if (handler == NULL || !RegisterHandler(handler))		\
			return B_NO_MEMORY;

	REGISTER_HANDLER(Address);
	REGISTER_HANDLER(Array);
	REGISTER_HANDLER(Compound);
	REGISTER_HANDLER(Enumeration);
	REGISTER_HANDLER(PointerToMember);
	REGISTER_HANDLER(Primitive);

	return B_OK;
}


status_t
TypeHandlerRoster::FindTypeHandler(ValueNodeChild* nodeChild, Type* type,
	TypeHandler*& _handler)
{
	// find the best-supporting handler
	AutoLocker<BLocker> locker(fLock);

	TypeHandler* bestHandler = NULL;
	float bestSupport = 0;

	for (int32 i = 0; TypeHandler* handler = fTypeHandlers.ItemAt(i); i++) {
		float support = handler->SupportsType(type);
		if (support > 0 && support > bestSupport) {
			bestHandler = handler;
			bestSupport = support;
		}
	}

	if (bestHandler == NULL)
		return B_ENTRY_NOT_FOUND;

	bestHandler->AcquireReference();
	_handler = bestHandler;
	return B_OK;
}


status_t
TypeHandlerRoster::CreateValueNode(ValueNodeChild* nodeChild, Type* type,
	ValueNode*& _node)
{
	// find the best-supporting handler
	while (true) {
		TypeHandler* handler;
		status_t error = FindTypeHandler(nodeChild, type, handler);
		if (error == B_OK) {
			// let the handler create the node
			Reference<TypeHandler> handlerReference(handler, true);
			return handler->CreateValueNode(nodeChild, type, _node);
		}

		// not found yet -- try to strip a modifier/typedef from the type
		Type* nextType = type->ResolveRawType(true);
		if (nextType == NULL || nextType == type)
			return B_UNSUPPORTED;

		type = nextType;
	}
}


bool
TypeHandlerRoster::RegisterHandler(TypeHandler* handler)
{
	if (!fTypeHandlers.AddItem(handler))
		return false;

	handler->AcquireReference();
	return true;
}


void
TypeHandlerRoster::UnregisterHandler(TypeHandler* handler)
{
	if (fTypeHandlers.RemoveItem(handler))
		handler->RemoveReference();
}
