#include <ACPI.h>
#include <KernelExport.h>
#include <stdio.h>
#include <malloc.h>

#include "acpi.h"
#include "acpixf.h"

status_t acpi_std_ops(int32 op,...);
status_t acpi_rescan_stub(void);

void enable_fixed_event (uint32 event);
void disable_fixed_event (uint32 event);

uint32 fixed_event_status (uint32 event);
void reset_fixed_event (uint32 event);

status_t install_fixed_event_handler	(uint32 event, interrupt_handler *handler, void *data); 
status_t remove_fixed_event_handler	(uint32 event, interrupt_handler *handler); 

status_t get_next_entry (uint32 object_type, const char *base, char *result, size_t len, void **counter);
status_t get_device (const char *hid, uint32 index, char *result);

status_t get_device_hid (const char *path, char *hid);
uint32 get_object_type (const char *path);

status_t evaluate_object (const char *object, acpi_object_type *return_value, size_t buf_len);
status_t evaluate_method (const char *object, const char *method, acpi_object_type *return_value, size_t buf_len, acpi_object_type *args, int num_args);

status_t enter_sleep_state (uint8 state);

struct acpi_module_info acpi_module = {
	{
		{
			B_ACPI_MODULE_NAME,
			B_KEEP_LOADED,
			acpi_std_ops
		},
		
		acpi_rescan_stub
	},
	
	enable_fixed_event,
	disable_fixed_event,
	fixed_event_status,
	reset_fixed_event,
	install_fixed_event_handler,
	remove_fixed_event_handler,
	get_next_entry,
	get_device,
	get_device_hid,
	get_object_type,
	evaluate_object,
	evaluate_method,
	enter_sleep_state
};

_EXPORT module_info *modules[] = {
	(module_info *) &acpi_module,
	NULL
};

status_t acpi_std_ops(int32 op,...) {
	ACPI_STATUS Status;
	
	switch(op) {
		case B_MODULE_INIT:
			
			#ifdef ACPI_DEBUG_OUTPUT
				AcpiDbgLevel = ACPI_DEBUG_ALL | ACPI_LV_VERBOSE;
				AcpiDbgLayer = ACPI_ALL_COMPONENTS;
			#endif
			
			/* Bring up ACPI */
			Status = AcpiInitializeSubsystem();
			if (Status != AE_OK) {
				dprintf("ACPI: AcpiInitializeSubsystem() failed (%s)",AcpiFormatException(Status));
				return B_ERROR;
			}
			Status = AcpiLoadTables();
			if (Status != AE_OK) {
				dprintf("ACPI: AcpiLoadTables failed (%s)",AcpiFormatException(Status));
				return B_ERROR;
			}
			Status = AcpiEnableSubsystem(ACPI_FULL_INITIALIZATION);
			if (Status != AE_OK) {
				dprintf("ACPI: AcpiEnableSubsystem failed (%s)",AcpiFormatException(Status));
				return B_ERROR;
			}
			
			/* Phew. Now in ACPI mode */
			dprintf("ACPI: ACPI initialized\n");
			break;
		
		case B_MODULE_UNINIT:
			Status = AcpiTerminate();
			if (Status != AE_OK)
				dprintf("Could not bring system out of ACPI mode. Oh well.\n");
			
				/* This isn't so terrible. We'll just fail silently */
			break;
		default:
			return B_ERROR;
	}
	return B_OK;
}

status_t acpi_rescan_stub(void) {
	return B_OK;
}

void enable_fixed_event (uint32 event) {
	AcpiEnableEvent(event,0);
}

void disable_fixed_event (uint32 event) {
	AcpiDisableEvent(event,0);
}

uint32 fixed_event_status (uint32 event) {
	ACPI_EVENT_STATUS status = 0;
	AcpiGetEventStatus(event,&status);
	return (status/* & ACPI_EVENT_FLAG_SET*/);
}

void reset_fixed_event (uint32 event) {
	AcpiClearEvent(event);
}

status_t install_fixed_event_handler (uint32 event, interrupt_handler *handler, void *data) {
	return ((AcpiInstallFixedEventHandler(event,handler,data) == AE_OK) ? B_OK : B_ERROR);
}

status_t remove_fixed_event_handler	(uint32 event, interrupt_handler *handler) {
	return ((AcpiRemoveFixedEventHandler(event,handler) == AE_OK) ? B_OK : B_ERROR);
}

status_t get_next_entry (uint32 object_type, const char *base, char *result, size_t len, void **counter) {
	ACPI_STATUS result_status;
	ACPI_HANDLE parent,child,new_child;
	ACPI_BUFFER buffer;
	
	if ((base == NULL) || (strcmp(base,"\\") == 0)) {
		parent = ACPI_ROOT_OBJECT;
	} else {
		result_status = AcpiGetHandle(NULL,base,&parent);
		if (result_status != AE_OK)
			return B_ENTRY_NOT_FOUND;
	}
	
	child = *counter;
	
	result_status = AcpiGetNextObject(object_type,parent,child,&new_child);
	if (result_status != AE_OK)
		return B_ENTRY_NOT_FOUND;
		
	*counter = new_child;
	buffer.Length = len;
	buffer.Pointer = result;
	
	result_status = AcpiGetName(new_child,ACPI_FULL_PATHNAME,&buffer);
	if (result_status != AE_OK)
		return B_NO_MEMORY; /* Corresponds to AE_BUFFER_OVERFLOW */
	
	return B_OK;
}

static ACPI_STATUS get_device_by_hid_callback(ACPI_HANDLE object, UINT32 depth, void *context, void **return_val) {
	ACPI_STATUS result;
	ACPI_BUFFER buffer;
	uint32 *counter = (uint32 *)(context);
		
	buffer.Length = 255;
	buffer.Pointer = malloc(255);
	
	*return_val = NULL;
	
	if (counter[0] == counter[1]) {
		result = AcpiGetName(object,ACPI_FULL_PATHNAME,&buffer);
		if (result != AE_OK)
			return result;
		
		((char*)(buffer.Pointer))[buffer.Length] = '\0';
		*return_val = buffer.Pointer;
		return AE_CTRL_TERMINATE;
	}
	
	counter[1]++;
	return AE_OK;
}

status_t get_device (const char *hid, uint32 index, char *result) {
	ACPI_STATUS status;
	uint32 counter[2] = {index,0};
	char *result2;
	
	status = AcpiGetDevices((char *)hid,&get_device_by_hid_callback,counter,&result2);
	if ((status != AE_OK) || (result2 == NULL))
		return B_ENTRY_NOT_FOUND;
		
	strcpy(result,result2);
	free(result2);
	return B_OK;
}

status_t get_device_hid (const char *path, char *hid) {
	ACPI_HANDLE handle;
	ACPI_DEVICE_INFO info;
	
	if (AcpiGetHandle(NULL,path,&handle) != AE_OK)
		return B_ENTRY_NOT_FOUND;
	
	if (AcpiGetObjectInfo(handle,&info) != AE_OK)
		return B_BAD_TYPE;
		
	strcpy(hid,info.HardwareId.Value);
	return B_OK;
}

uint32 get_object_type (const char *path) {
	ACPI_HANDLE handle;
	ACPI_OBJECT_TYPE type;
	
	if (AcpiGetHandle(NULL,path,&handle) != AE_OK)
		return B_ENTRY_NOT_FOUND;
		
	AcpiGetType(handle,&type);
	return type;
}

status_t evaluate_object (const char *object, acpi_object_type *return_value, size_t buf_len) {
	ACPI_BUFFER buffer;
	ACPI_STATUS status;
	
	buffer.Pointer = return_value;
	buffer.Length = buf_len;
	
	status = AcpiEvaluateObject(NULL,object,NULL,(return_value != NULL) ? &buffer : NULL);
	return (status == AE_OK) ? B_OK : B_ERROR;
}

status_t evaluate_method (const char *object, const char *method, acpi_object_type *return_value, size_t buf_len, acpi_object_type *args, int num_args) {
	ACPI_BUFFER buffer;
	ACPI_STATUS status;
	ACPI_OBJECT_LIST acpi_args;
	ACPI_HANDLE handle;
	
	if (AcpiGetHandle(NULL,object,&handle) != AE_OK)
		return B_ENTRY_NOT_FOUND;
	
	buffer.Pointer = return_value;
	buffer.Length = buf_len;
	
	acpi_args.Count = num_args;
	acpi_args.Pointer = args;
	
	status = AcpiEvaluateObject(handle,method,(args != NULL) ? &acpi_args : NULL,(return_value != NULL) ? &buffer : NULL);
	return (status == AE_OK) ? B_OK : B_ERROR;
}

void waking_vector(void) {
	//--- This should do something ---
}

status_t enter_sleep_state (uint8 state) {
	ACPI_STATUS status;
	cpu_status cpu;
	physical_entry wake_vector;
	
	lock_memory(&waking_vector,sizeof(waking_vector),0);
	get_memory_map(&waking_vector,sizeof(waking_vector),&wake_vector,1);
	
	status = AcpiSetFirmwareWakingVector(wake_vector.address);
	if (status != AE_OK)
		return B_ERROR;
	
	status = AcpiEnterSleepStatePrep(state);
	if (status != AE_OK)
		return B_ERROR;
	
	/* NOT SMP SAFE (!!!!!!!!!!!) */
	
	cpu = disable_interrupts();
	status = AcpiEnterSleepState(state);
	restore_interrupts(cpu);
	
	if (status != AE_OK)
		return B_ERROR;
	
	/*status = AcpiLeaveSleepState(state);
	if (status != AE_OK)
		return B_ERROR;*/
	
	return B_OK;
}
