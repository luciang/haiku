#include <PCI.h>
#include <Drivers.h>
#include <KernelExport.h>
#include <malloc.h>
#include <string.h>
#include <ether_driver.h>

#include "b44mm.h"
#include "b44lm.h"
#include "mempool.h"

struct pci_module_info *pci = NULL;

const char *dev_list[11];
struct be_b44_dev be_b44_dev_cards[10];
int cards_found = 0;

int b44_Packet_Desc_Size = sizeof(struct B_UM_PACKET);

#define ROUND_UP_TO_PAGE(size) ((size % 4096 != 0) ? 4096 - (size % 4096) + size : size)

/* -------- BeOS Driver Hooks ------------ */

status_t b44_open(const char *name, uint32 flags, void **cookie);
status_t b44_close(void *cookie);
status_t b44_free(void *cookie);
status_t b44_ioctl(void *cookie,uint32 op,void *data,size_t len);
status_t b44_read(void *cookie,off_t pos,void *data,size_t *numBytes);
status_t b44_write(void *cookie,off_t pos,const void *data,size_t *numBytes);
int32	 b44_interrupt(void *cookie);
int32 tx_cleanup_thread(void *us);

device_hooks b44_hooks = {b44_open,b44_close,b44_free,b44_ioctl,b44_read,b44_write,NULL,NULL,NULL,NULL};

status_t init_hardware(void) {	
	return B_OK;
}

const char **publish_devices() {
 	return dev_list;
}

device_hooks *find_device(const char *name) {
	return &b44_hooks;
}
	
status_t init_driver(void) {
	int i = 0;
	pci_info dev_info;
	
   	if (pci == NULL)
   		get_module(B_PCI_MODULE_NAME,(module_info **)&pci);
   	   		
   	while (pci->get_nth_pci_info(i++,&dev_info) == 0) {
   		if (!((dev_info.class_base == PCI_network) && (dev_info.class_sub == PCI_ethernet)
   		      && (dev_info.vendor_id == 0x14e4) && (dev_info.device_id == 0x4401)))
 					continue;
 	 	   		
 	 	if (cards_found >= 10)
 	 		break;
 	 	
 	 	dev_list[cards_found] = (char *)malloc(16 /* net/bcm440x/xx */);
 	 	sprintf(dev_list[cards_found],"net/bcm440x/%d",cards_found);
 	 	be_b44_dev_cards[cards_found].pci_data = dev_info;
 	 	be_b44_dev_cards[cards_found].packet_release_sem = create_sem(0,dev_list[cards_found]);
 	 	be_b44_dev_cards[cards_found].mem_list_num = 0;
 	 	be_b44_dev_cards[cards_found].lockmem_list_num = 0; 	 	
 	 	be_b44_dev_cards[cards_found].opened = 0;
 	 	be_b44_dev_cards[cards_found].block = 1;
 	 	be_b44_dev_cards[cards_found].lock = 0;
 	 	 	 	
 	 	if (b44_LM_GetAdapterInfo(&be_b44_dev_cards[cards_found].lm_dev) != LM_STATUS_SUCCESS)
 	 		return ENODEV;
 	 	 	 	
 	 	QQ_InitQueue(&be_b44_dev_cards[cards_found].RxPacketReadQ.Container,MAX_RX_PACKET_DESC_COUNT);
 	 	 		
 		cards_found++;
 	}
 	
 	mempool_init((MAX_RX_PACKET_DESC_COUNT+10) * cards_found);
 	 	
 	dev_list[cards_found] = NULL;

	return B_OK;
}

void uninit_driver(void) {
	int i,j;
	struct be_b44_dev *pUmDevice; 
	
	for (j = 0; j < cards_found; j++) {
		pUmDevice = &be_b44_dev_cards[j];
		for (i = 0; i < pUmDevice->mem_list_num; i++)
			free(pUmDevice->mem_list[i]);
		for (i = 0; i < pUmDevice->lockmem_list_num; i++)
			delete_area(pUmDevice->lockmem_list[i]);
		
		delete_area(pUmDevice->mem_base);
		
		free(dev_list[j]);
	}
	
	mempool_exit();
}

status_t b44_open(const char *name, uint32 flags, void **cookie) {
	struct be_b44_dev *pDevice = NULL;
	int i;
	
	*cookie = NULL;
	for (i = 0; i < cards_found; i++) {
		if (strcmp(dev_list[i],name) == 0) {
			*cookie = pDevice = &be_b44_dev_cards[i];
			break;
		}
	}
	
	if (*cookie == NULL)
		return B_FILE_NOT_FOUND;
	
	if (atomic_or(&pDevice->opened,1)) {
		*cookie = pDevice = NULL;
		return B_BUSY;
	}
	
	install_io_interrupt_handler(pDevice->pci_data.u.h0.interrupt_line,b44_interrupt,*cookie,0);
	if (b44_LM_InitializeAdapter(&pDevice->lm_dev) != LM_STATUS_SUCCESS) { 
		atomic_and(&pDevice->opened,0);
		remove_io_interrupt_handler(pDevice->pci_data.u.h0.interrupt_line,b44_interrupt,*cookie);
		*cookie = NULL;
		return B_ERROR;
	}
	
	/*QQ_InitQueue(&pDevice->rx_out_of_buf_q.Container,
        MAX_RX_PACKET_DESC_COUNT);*/
	
	b44_LM_EnableInterrupt(&pDevice->lm_dev);
	
	return B_OK;
}

status_t b44_close(void *cookie) {
	struct be_b44_dev *pUmDevice = (struct be_b44_dev *)(cookie);
	
	if (cookie == NULL)
		return B_OK;
		
	atomic_and(&pUmDevice->opened,0);
	b44_LM_Halt(&pUmDevice->lm_dev);
	
	return B_OK;
}

status_t b44_free(void *cookie) {
	struct be_b44_dev *pUmDevice = (struct be_b44_dev *)(cookie);
	
	if (cookie == NULL)
		return B_OK;
		
	remove_io_interrupt_handler(pUmDevice->pci_data.u.h0.interrupt_line,b44_interrupt,cookie);
	return B_OK;
}

status_t b44_ioctl(void *cookie,uint32 op,void *data,size_t len) {
	struct be_b44_dev *pUmDevice = (struct be_b44_dev *)(cookie);

	switch (op) {
		case ETHER_INIT:
			return B_OK;
		case ETHER_GETADDR:
			if (data == NULL)
				return B_ERROR;
				
			memcpy(data,pUmDevice->lm_dev.NodeAddress,6);
			return B_OK;
		case ETHER_NONBLOCK:
			pUmDevice->block = *((uint8 *)(data));
			return B_OK;
		case ETHER_ADDMULTI:
			return (b44_LM_MulticastAdd(&pUmDevice->lm_dev,(PLM_UINT8)(data)) == LM_STATUS_SUCCESS) ? B_OK : B_ERROR;
		case ETHER_REMMULTI:
			return (b44_LM_MulticastDel(&pUmDevice->lm_dev,(PLM_UINT8)(data)) == LM_STATUS_SUCCESS) ? B_OK : B_ERROR;
		case ETHER_SETPROMISC:
			if (*((uint8 *)(data)))
				b44_LM_SetReceiveMask(&pUmDevice->lm_dev,
					pUmDevice->lm_dev.ReceiveMask | LM_PROMISCUOUS_MODE);
			else
				 b44_LM_SetReceiveMask(&pUmDevice->lm_dev,
					pUmDevice->lm_dev.ReceiveMask & ~LM_PROMISCUOUS_MODE);
			return B_OK;
		case ETHER_GETLINKSTATE: {
			ether_link_state_t *state_buffer = (ether_link_state_t *)(data);
			state_buffer->link_speed = (pUmDevice->lm_dev.LineSpeed == LM_LINE_SPEED_10MBPS) ? 10 : 100;
			state_buffer->link_quality = (pUmDevice->lm_dev.LinkStatus == LM_STATUS_LINK_DOWN) ? 0.0 : 1.0;
			state_buffer->duplex_mode = (pUmDevice->lm_dev.DuplexMode == LM_DUPLEX_MODE_FULL);
			} return B_OK;
	}
	return B_ERROR;
}

int32 b44_interrupt(void *cookie) {
	struct be_b44_dev *pUmDevice = (struct be_b44_dev *)cookie;
	PLM_DEVICE_BLOCK pDevice = (PLM_DEVICE_BLOCK) pUmDevice;
	
	
	if (!pDevice->InitDone)
		return B_UNHANDLED_INTERRUPT;
	
	if (b44_LM_ServiceInterrupts(pDevice) == 12)
		return B_UNHANDLED_INTERRUPT;
		
	if (QQ_GetEntryCnt(&pDevice->RxPacketFreeQ.Container)) {
		b44_LM_QueueRxPackets(pDevice);
		return B_INVOKE_SCHEDULER;
	}
	
	return B_HANDLED_INTERRUPT;
}

status_t b44_read(void *cookie,off_t pos,void *data,size_t *numBytes) {
	struct be_b44_dev *pUmDevice = (struct be_b44_dev *)cookie;
	PLM_DEVICE_BLOCK pDevice = (PLM_DEVICE_BLOCK) pUmDevice;
	PLM_PACKET pPacket;
	struct B_UM_PACKET *pUmPacket;
	cpu_status cpu;
	
	if (pUmDevice->block)
		acquire_sem(pUmDevice->packet_release_sem);
	else
		acquire_sem_etc(pUmDevice->packet_release_sem,1,B_RELATIVE_TIMEOUT,0); // Decrement the receive sem anyway, but don't block
		
	cpu = disable_interrupts();
	acquire_spinlock(&pUmDevice->lock);
	
	pPacket = (PLM_PACKET)
		QQ_PopHead(&pUmDevice->RxPacketReadQ.Container);
		
	release_spinlock(&pUmDevice->lock);
	restore_interrupts(cpu);
	
	if (pPacket == 0)
		return B_ERROR;
	
	pUmPacket = (struct B_UM_PACKET *) pPacket;
	if ((pPacket->PacketStatus != LM_STATUS_SUCCESS) ||
		((pPacket->PacketSize) > 1518)) {
		
		cpu = disable_interrupts();
		acquire_spinlock(&pUmDevice->lock);
		
		QQ_PushTail(&pDevice->RxPacketFreeQ.Container, pPacket);

		release_spinlock(&pUmDevice->lock);
		restore_interrupts(cpu);
		*numBytes = -1;
		return B_ERROR;
	}
	
	if ((pPacket->PacketSize/*-pDevice->rxoffset*/) < *numBytes)
		*numBytes = pPacket->PacketSize/*-pDevice->rxoffset*/;
	
	memcpy(data,pUmPacket->data+pDevice->rxoffset,*numBytes);
	cpu = disable_interrupts();
	acquire_spinlock(&pUmDevice->lock);
	
	QQ_PushTail(&pDevice->RxPacketFreeQ.Container, pPacket);
	
	release_spinlock(&pUmDevice->lock);
	restore_interrupts(cpu);
	
	return B_OK;
}

status_t b44_write(void *cookie,off_t pos,const void *data,size_t *numBytes) {
	struct be_b44_dev *pUmDevice = (struct be_b44_dev *)cookie;
	PLM_DEVICE_BLOCK pDevice = (PLM_DEVICE_BLOCK) pUmDevice;
	PLM_PACKET pPacket;
	struct B_UM_PACKET *pUmPacket;
		
	/*if ((pDevice->LinkStatus == LM_STATUS_LINK_DOWN) || !pDevice->InitDone)
	{
		return ENETDOWN;
	}*/
	
	pPacket = (PLM_PACKET)
		QQ_PopHead(&pDevice->TxPacketFreeQ.Container);
	if (pPacket == 0)
		return B_ERROR;
		
	pUmPacket = (struct B_UM_PACKET *) pPacket;
	pUmPacket->data = chunk_pool_get();
		
	memcpy(pUmPacket->data/*+pDevice->dataoffset*/,data,*numBytes); /* no guarantee data is contiguous, so we have to copy */
	pPacket->PacketSize = pUmPacket->size = *numBytes/*+pDevice->rxoffset*/;
	
	pPacket->u.Tx.FragCount = 1;
		
	tx_cleanup_thread(pUmDevice);
	
	b44_LM_SendPacket(pDevice, pPacket);
		
	return B_OK;
}

/* -------- Broadcom MM hooks ----------- */

LM_STATUS b44_MM_ReadConfig16(PLM_DEVICE_BLOCK pDevice, LM_UINT32 Offset,
    LM_UINT16 *pValue16) {
    	if (pci == NULL)
    		get_module(B_PCI_MODULE_NAME,(module_info **)&pci);
    	
    	*pValue16 = (LM_UINT16)pci->read_pci_config(((struct be_b44_dev *)(pDevice))->pci_data.bus,((struct be_b44_dev *)(pDevice))->pci_data.device,((struct be_b44_dev *)(pDevice))->pci_data.function,(uchar)Offset,sizeof(LM_UINT16));
		return LM_STATUS_SUCCESS;
}
    	
LM_STATUS b44_MM_WriteConfig16(PLM_DEVICE_BLOCK pDevice, LM_UINT32 Offset,
    LM_UINT16 Value16) {
    	if (pci == NULL)
    		get_module(B_PCI_MODULE_NAME,(module_info **)&pci);
    	
    	pci->write_pci_config(((struct be_b44_dev *)(pDevice))->pci_data.bus,((struct be_b44_dev *)(pDevice))->pci_data.device,((struct be_b44_dev *)(pDevice))->pci_data.function,(uchar)Offset,sizeof(LM_UINT16),(uint32)Value16);
		return LM_STATUS_SUCCESS;
}

LM_STATUS b44_MM_ReadConfig32(PLM_DEVICE_BLOCK pDevice, LM_UINT32 Offset,
    LM_UINT32 *pValue32)  {
    	if (pci == NULL)
    		get_module(B_PCI_MODULE_NAME,(module_info **)&pci);
    	
    	*pValue32 = (LM_UINT32)pci->read_pci_config(((struct be_b44_dev *)(pDevice))->pci_data.bus,((struct be_b44_dev *)(pDevice))->pci_data.device,((struct be_b44_dev *)(pDevice))->pci_data.function,(uchar)Offset,sizeof(LM_UINT32));
		return LM_STATUS_SUCCESS;
}

LM_STATUS b44_MM_WriteConfig32(PLM_DEVICE_BLOCK pDevice, LM_UINT32 Offset,
    LM_UINT32 Value32){
    	if (pci == NULL)
    		get_module(B_PCI_MODULE_NAME,(module_info **)&pci);
    	
    	pci->write_pci_config(((struct be_b44_dev *)(pDevice))->pci_data.bus,((struct be_b44_dev *)(pDevice))->pci_data.device,((struct be_b44_dev *)(pDevice))->pci_data.function,(uchar)Offset,sizeof(LM_UINT32),(uint32)Value32);
		return LM_STATUS_SUCCESS;
}

LM_STATUS b44_MM_MapMemBase(PLM_DEVICE_BLOCK pDevice) {
    struct be_b44_dev *pUmDevice = (struct be_b44_dev *)(pDevice);
    size_t size = pUmDevice->pci_data.u.h0.base_register_sizes[0];
    
    if (pci == NULL)
    	get_module(B_PCI_MODULE_NAME,(module_info **)&pci);
    
    size = ROUNDUP(size,B_PAGE_SIZE);
    pUmDevice->mem_base = map_physical_memory("bcm440x_regs",(void *)(pUmDevice->pci_data.u.h0.base_registers[0]),size,B_ANY_KERNEL_BLOCK_ADDRESS,B_READ_AREA | B_WRITE_AREA,(void **)(&pDevice->pMappedMemBase));

	return LM_STATUS_SUCCESS;
}

/*LM_STATUS b44_MM_MapIoBase(PLM_DEVICE_BLOCK pDevice) {
    	if (pci == NULL)
    		get_module(B_PCI_MODULE_NAME,(module_info **)&pci);
    		
	pDevice->pMappedMemBase = pci->ram_address(((struct be_b44_dev *)(pDevice))->pci_data.memory_base);
	return LM_STATUS_SUCCESS;
}*/

LM_STATUS b44_MM_IndicateRxPackets(PLM_DEVICE_BLOCK pDevice) {
	struct be_b44_dev *dev = (struct be_b44_dev *)pDevice;
	PLM_PACKET pPacket;
	
	while (1) {
		pPacket = (PLM_PACKET)
			QQ_PopHead(&pDevice->RxPacketReceivedQ.Container);
		if (pPacket == 0)
			break;
		
		acquire_spinlock(&dev->lock);
		release_sem_etc(dev->packet_release_sem,1,B_DO_NOT_RESCHEDULE);
		release_spinlock(&dev->lock);
		QQ_PushTail(&dev->RxPacketReadQ.Container, pPacket);
	}
	
	return LM_STATUS_SUCCESS;
}

LM_STATUS b44_MM_IndicateTxPackets(PLM_DEVICE_BLOCK pDevice) {
	return LM_STATUS_SUCCESS;
}

int32 tx_cleanup_thread(void *us) {
	PLM_PACKET pPacket;
	PLM_DEVICE_BLOCK pDevice = (PLM_DEVICE_BLOCK)(us);
	struct be_b44_dev *pUmDevice = (struct be_b44_dev *)(us);
	struct B_UM_PACKET *pUmPacket;
	cpu_status cpu;
	
	while (1) { 
		cpu = disable_interrupts();
		acquire_spinlock(&pUmDevice->lock);
		
		pPacket = (PLM_PACKET)
			QQ_PopHead(&pDevice->TxPacketXmittedQ.Container);
			
		release_spinlock(&pUmDevice->lock);
		restore_interrupts(cpu);
		if (pPacket == 0)
			break;
		pUmPacket = (struct B_UM_PACKET *)(pPacket);
		chunk_pool_put(pUmPacket->data);
		pUmPacket->data = NULL;
		
		cpu = disable_interrupts();
		acquire_spinlock(&pUmDevice->lock);
		QQ_PushTail(&pDevice->TxPacketFreeQ.Container, pPacket);
		release_spinlock(&pUmDevice->lock);
		restore_interrupts(cpu);
	}
	return LM_STATUS_SUCCESS;
}
	
/*LM_STATUS b44_MM_StartTxDma(PLM_DEVICE_BLOCK pDevice, PLM_PACKET pPacket);
LM_STATUS b44_MM_CompleteTxDma(PLM_DEVICE_BLOCK pDevice, PLM_PACKET pPacket);*/

LM_STATUS b44_MM_AllocateMemory(PLM_DEVICE_BLOCK pDevice, LM_UINT32 BlockSize, 
    PLM_VOID *pMemoryBlockVirt) {
	    struct be_b44_dev *dev = (struct be_b44_dev *)(pDevice);
    	
    	if (dev->mem_list_num == 16)
    		return LM_STATUS_FAILURE;
    		
    	*pMemoryBlockVirt = dev->mem_list[(dev->mem_list_num)++] = (void *)malloc(BlockSize);
    	return LM_STATUS_SUCCESS;
    }
    
LM_STATUS b44_MM_AllocateSharedMemory(PLM_DEVICE_BLOCK pDevice, LM_UINT32 BlockSize,
    PLM_VOID *pMemoryBlockVirt, PLM_PHYSICAL_ADDRESS pMemoryBlockPhy) {
	    struct be_b44_dev *dev;
	    void *pvirt = NULL;
	    area_id area_desc;
		physical_entry entry;
	 	
	 	dev = (struct be_b44_dev *)(pDevice);
	 	area_desc = dev->lockmem_list[dev->lockmem_list_num++] = create_area("broadcom_shared_mem",&pvirt,B_ANY_KERNEL_ADDRESS,ROUND_UP_TO_PAGE(BlockSize),/*B_CONTIGUOUS | B_FULL_LOCK*/ B_LOMEM,B_READ_AREA | B_WRITE_AREA);
	 	
	 	if (pvirt == NULL)
	 		return LM_STATUS_FAILURE;
	 	
		memset(pvirt, 0, BlockSize);
		*pMemoryBlockVirt = (PLM_VOID) pvirt;
		
		get_memory_map(pvirt,BlockSize,&entry,1);
		*pMemoryBlockPhy = (LM_PHYSICAL_ADDRESS) entry.address;
				
		return LM_STATUS_SUCCESS;
}

LM_STATUS b44_MM_GetConfig(PLM_DEVICE_BLOCK pDevice){
	pDevice->DisableAutoNeg = FALSE;
	pDevice->RequestedLineSpeed = LM_LINE_SPEED_AUTO;
	pDevice->RequestedDuplexMode = LM_DUPLEX_MODE_FULL;
	pDevice->FlowControlCap |= LM_FLOW_CONTROL_AUTO_PAUSE;
	//pDevice->TxPacketDescCnt = tx_pkt_desc_cnt[DEFAULT_TX_PACKET_DESC_COUNT];
	pDevice->RxPacketDescCnt = DEFAULT_RX_PACKET_DESC_COUNT;

	return LM_STATUS_SUCCESS;
}

LM_STATUS b44_MM_IndicateStatus(PLM_DEVICE_BLOCK pDevice, LM_STATUS Status) {
	return LM_STATUS_SUCCESS;
}

LM_STATUS
b44_MM_InitializeUmPackets(PLM_DEVICE_BLOCK pDevice)
{
	int i;
	struct B_UM_PACKET *pUmPacket;
	PLM_PACKET pPacket;
	
	for (i = 0; i < pDevice->RxPacketDescCnt; i++) {
		pPacket = QQ_PopHead(&pDevice->RxPacketFreeQ.Container);
		pUmPacket = (struct B_UM_PACKET *) pPacket;
		pUmPacket->data = chunk_pool_get();
		/*if (pUmPacket->data == 0) {
			QQ_PushTail(&pUmDevice->rx_out_of_buf_q.Container, pPacket);
			continue;
		}*/
		pPacket->u.Rx.pRxBufferVirt = pUmPacket->data;
		QQ_PushTail(&pDevice->RxPacketFreeQ.Container, pPacket);
	}

	return LM_STATUS_SUCCESS;
}

LM_STATUS b44_MM_FreeRxBuffer(PLM_DEVICE_BLOCK pDevice, PLM_PACKET pPacket) {
	struct B_UM_PACKET *pUmPacket;
	pUmPacket = (struct B_UM_PACKET *) pPacket;
	chunk_pool_put(pUmPacket->data);
	pUmPacket->data = NULL;
	return LM_STATUS_SUCCESS;
}
