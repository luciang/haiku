/* ++++++++++
	ACPI namespace dump. 
	Nothing special here, just tree enumeration and type identification.
+++++ */

#include <Drivers.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>

#include "acpi_priv.h"

#include <util/kernel_cpp.h>
#include <util/ring_buffer.h>

class RingBuffer {
public:
	RingBuffer(size_t size = 1024);
	~RingBuffer();	
	ssize_t Read(void *buffer, size_t length);
	ssize_t Write(const void *buffer, size_t length);
	size_t WritableAmount() const;
	size_t ReadableAmount() const;

	bool Lock();
	void Unlock();
private:
	ring_buffer *fBuffer;
	sem_id fLock;
};


typedef struct acpi_ns_device_info {
	device_node *node;
	acpi_root_info	*acpi;
	void	*acpi_cookie;
	thread_id thread;
	sem_id read_sem;
	RingBuffer *buffer;
} acpi_ns_device_info;



// called with the buffer lock held
static bool
make_space(acpi_ns_device_info *device, size_t space)
{
	size_t available = device->buffer->WritableAmount();
	if (space <= available)
		return true;
	bool released = false;
	do {
		/*if (!released) {
			released = true;
			if (release_sem(device->read_sem) != B_OK) {
				panic("can't release sem");
				return false;
			}
		}*/
		device->buffer->Unlock();

		if (!released) {
			dprintf("try to release\n");
			if (release_sem_etc(device->read_sem, 1, B_RELEASE_IF_WAITING_ONLY) == B_OK) {
				dprintf("released\n");
				released = true;
			}
		}
		snooze(10000);

		if (!device->buffer->Lock()) {
			return false;
		}
		
	} while (device->buffer->WritableAmount() < space);
	
	return true;
} 


static int32 sNumCount = 0;

static void 
dump_acpi_namespace(acpi_ns_device_info *device, char *root, int indenting) 
{
	char result[255];
	char output[255];
	char tabs[255];
	char hid[9];
	int i;
	size_t written = 0;
	hid[8] = '\0';
	tabs[0] = '\0';
	for (i = 0; i < indenting; i++) {
		sprintf(tabs, "%s|    ", tabs);
	}
	sprintf(tabs, "%s|--- ", tabs);
	int depth = sizeof(char) * 5 * indenting + sizeof(char); // index into result where the device name will be.
	
	if (atomic_add(&sNumCount, 1) >= 200) {
		dprintf("above 200");
		// TODO: Without this, the function never finishes
		// to dump the acpi tree, so there seems to be something
		// weird with the acpi code 
		exit_thread(B_ERROR);
	}	

	void *counter = NULL;
	while (device->acpi->get_next_entry(ACPI_TYPE_ANY, root, result, 255, &counter) == B_OK) {
		uint32 type = device->acpi->get_object_type(result);
		sprintf(output, "%s%s", tabs, result + depth);
		switch(type) {
			case ACPI_TYPE_ANY:
			default: 
				break;
			case ACPI_TYPE_INTEGER:
				snprintf(output, sizeof(output), "%s     INTEGER", output);
				break;
			case ACPI_TYPE_STRING:
				snprintf(output, sizeof(output), "%s     STRING", output);
				break;
			case ACPI_TYPE_BUFFER:
				snprintf(output, sizeof(output), "%s     BUFFER", output);
				break;
			case ACPI_TYPE_PACKAGE:
				snprintf(output, sizeof(output), "%s     PACKAGE", output);
				break;
			case ACPI_TYPE_FIELD_UNIT:
				snprintf(output, sizeof(output), "%s     FIELD UNIT", output);
				break;
			case ACPI_TYPE_DEVICE:
				device->acpi->get_device_hid(result, hid);
				snprintf(output, sizeof(output), "%s     DEVICE (%s)", output, hid);
				break;
			case ACPI_TYPE_EVENT:
				snprintf(output, sizeof(output), "%s     EVENT", output);
				break;
			case ACPI_TYPE_METHOD:
				snprintf(output, sizeof(output), "%s     METHOD", output);
				break;
			case ACPI_TYPE_MUTEX:
				snprintf(output, sizeof(output), "%s     MUTEX", output);
				break;
			case ACPI_TYPE_REGION:
				snprintf(output, sizeof(output), "%s     REGION", output);
				break;
			case ACPI_TYPE_POWER:
				snprintf(output, sizeof(output), "%s     POWER", output);
				break;
			case ACPI_TYPE_PROCESSOR:
				snprintf(output, sizeof(output), "%s     PROCESSOR", output);
				break;
			case ACPI_TYPE_THERMAL:
				snprintf(output, sizeof(output), "%s     THERMAL", output);
				break;
			case ACPI_TYPE_BUFFER_FIELD:
				snprintf(output, sizeof(output), "%s     BUFFER_FIELD", output);
				break;
		}
		//strcat(output, "\n");
		written = 0;
		RingBuffer &ringBuffer = *device->buffer;
		size_t toWrite = strlen(output);
		if (toWrite > 0) {
			strlcat(output, "\n", sizeof(output));
			toWrite++;
			if (ringBuffer.Lock()) {
				if (ringBuffer.WritableAmount() < toWrite) {
					//dprintf("not enough space\n");
					if (!make_space(device, toWrite)) {
						panic("couldn't make space");
						exit_thread(0);
					}
				}

				if (ringBuffer.WritableAmount() < toWrite)
					panic("fuck!!!");
				written = ringBuffer.Write(output, toWrite);
				//dprintf("written %ld bytes\n", written);
				ringBuffer.Unlock();
			}

			dump_acpi_namespace(device, result, indenting + 1);
		}

	}
	//dprintf("Reached end of devices, root %s, counter %p\n", root, counter);
	//panic("reached end of device!!!");
}


static int32
acpi_namespace_dump(void *arg)
{
	acpi_ns_device_info *device = (acpi_ns_device_info*)(arg);
	dprintf("**** start dumping ****\n");
        dump_acpi_namespace(device, NULL, 0);
	dprintf("**** finished dumping. Writing last line ****\n");
	if (device->buffer->Lock()) {
		size_t writable = device->buffer->WritableAmount();
		if (writable < 1)
			make_space(device, 1);
		device->buffer->Unlock();
	}

	if (device->buffer->Lock()) {
		device->buffer->Write("\n", 1);
		device->buffer->Unlock();
	}

	dprintf("written. exiting\n");
	return 0;
}

extern "C" {
/* ----------
	acpi_namespace_open - handle open() calls
----- */

static status_t
acpi_namespace_open(void *_cookie, const char* path, int flags, void** cookie)
{
	acpi_ns_device_info *device = (acpi_ns_device_info *)_cookie;

	dprintf("\nacpi_ns_dump: device_open\n");

	*cookie = device;

	RingBuffer *ringBuffer = new RingBuffer(1024);
	if (ringBuffer == NULL)
		return B_NO_MEMORY;

	device->read_sem = create_sem(0, "read_sem");

	device->thread = spawn_kernel_thread(acpi_namespace_dump, "acpi dumper",
		 B_NORMAL_PRIORITY, device);
	if (device->thread < 0) {
		delete ringBuffer;
		return device->thread;
	}

	device->buffer = ringBuffer;
	sNumCount = 0;
	resume_thread(device->thread);
	return B_OK;
}


/* ----------
	acpi_namespace_read - handle read() calls
----- */
static status_t
acpi_namespace_read(void *_cookie, off_t position, void *buf, size_t* num_bytes)
{
	acpi_ns_device_info *device = (acpi_ns_device_info *)_cookie;
	size_t bytesRead = 0; 
	size_t readable = 0;
        //dprintf("acpi_namespace_read(cookie: %p, position: %lld, buffer: %p, size: %ld)\n",
        //                _cookie, position, buf, *num_bytes);

	RingBuffer &ringBuffer = *device->buffer;

	if (ringBuffer.Lock()) {
		readable = ringBuffer.ReadableAmount();

		//dprintf("%ld bytes readable\n", readable);
		if (readable <= 0) {
			//dprintf("acquiring read sem...\n");
			ringBuffer.Unlock();
			status_t status = acquire_sem_etc(device->read_sem, 1, B_CAN_INTERRUPT, 0);
			if (status < B_OK) {
				//dprintf("read: acquire_sem returned %s\n", strerror(status));
				*num_bytes = 0;
				return status;
			}
			//dprintf("read sem acquired\n");
			if (!ringBuffer.Lock()) {
				dprintf("read: couldn't acquire lock. bailing\n");
				*num_bytes = 0;
				return B_ERROR;
			}
		}

		//dprintf("readable %ld\n", ringBuffer.ReadableAmount());
		bytesRead = ringBuffer.Read(buf, *num_bytes);
		ringBuffer.Unlock();
	}

	//dprintf("read: read %ld bytes\n", bytesRead);
	if (bytesRead < 0) {
		*num_bytes = 0;
		return bytesRead;
	}

	*num_bytes = bytesRead;
	
	return B_OK;
}


/* ----------
	acpi_namespace_write - handle write() calls
----- */

static status_t
acpi_namespace_write(void* cookie, off_t position, const void* buffer, size_t* num_bytes)
{
	dprintf("acpi_ns_dump: device_write\n");
	*num_bytes = 0;				/* tell caller nothing was written */
	return B_IO_ERROR;
}


/* ----------
	acpi_namespace_control - handle ioctl calls
----- */

static status_t
acpi_namespace_control(void* cookie, uint32 op, void* arg, size_t len)
{
	dprintf("acpi_ns_dump: device_control\n");
	return B_BAD_VALUE;
}


/* ----------
	acpi_namespace_close - handle close() calls
----- */

static status_t
acpi_namespace_close(void* cookie)
{
	status_t status;
	acpi_ns_device_info *device = (acpi_ns_device_info *)cookie;
	dprintf("acpi_ns_dump: device_close\n");
	if (device->read_sem >= 0)
		delete_sem(device->read_sem);
	
	kill_thread(device->thread);

	delete device->buffer;

	return B_OK;
}


/* -----
	acpi_namespace_free - called after the last device is closed, and after
	all i/o is complete.
----- */
static status_t
acpi_namespace_free(void* cookie)
{
	dprintf("acpi_ns_dump: device_free\n");
	
	return B_OK;
}


//	#pragma mark - device module API


static status_t
acpi_namespace_init_device(void *_cookie, void **cookie)
{
	device_node *node = (device_node *)_cookie;
	status_t err;
	
	acpi_ns_device_info *device = (acpi_ns_device_info *)calloc(1, sizeof(*device));
	if (device == NULL)
		return B_NO_MEMORY;

	device->node = node;
	err = gDeviceManager->get_driver(node, (driver_module_info **)&device->acpi,
		(void **)&device->acpi_cookie);
	if (err != B_OK) {
		free(device);
		return err;
	}
	
	*cookie = device;
	return B_OK;
}


static void
acpi_namespace_uninit_device(void *_cookie)
{
	acpi_ns_device_info *device = (acpi_ns_device_info *)_cookie;
	free(device);
}

}

struct device_module_info acpi_ns_dump_module = {
	{
		ACPI_NS_DUMP_DEVICE_MODULE_NAME,
		0,
		NULL
	},

	acpi_namespace_init_device,
	acpi_namespace_uninit_device,
	NULL,
		
	acpi_namespace_open,
	acpi_namespace_close,
	acpi_namespace_free,
	acpi_namespace_read,
	acpi_namespace_write,
	NULL,
	acpi_namespace_control,

	NULL,
	NULL
};


RingBuffer::RingBuffer(size_t size)
{
	fBuffer = create_ring_buffer(size);
	fLock = create_sem(1, "ring buffer lock");
}


RingBuffer::~RingBuffer()
{
	delete_sem(fLock);
	delete_ring_buffer(fBuffer);
}


ssize_t
RingBuffer::Read(void *buffer, size_t size)
{
	return ring_buffer_read(fBuffer, (uint8*)buffer, size);
}


ssize_t
RingBuffer::Write(const void *buffer, size_t size)
{
	return ring_buffer_write(fBuffer, (uint8*)buffer, size);
}


size_t
RingBuffer::ReadableAmount() const
{
	return ring_buffer_readable(fBuffer);
}


size_t
RingBuffer::WritableAmount() const
{
	return ring_buffer_writable(fBuffer);
}


bool
RingBuffer::Lock()
{
	//status_t status = acquire_sem_etc(fLock, 1, B_CAN_INTERRUPT, 0);
	status_t status = acquire_sem(fLock);
	return status == B_OK;
}


void
RingBuffer::Unlock()
{
	release_sem(fLock);
}



