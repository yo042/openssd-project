//////////////////////////////////////////////////////////////////////////////////
// nvme_io_cmd.c for Cosmos+ OpenSSD
// Copyright (c) 2016 Hanyang University ENC Lab.
// Contributed by Yong Ho Song <yhsong@enc.hanyang.ac.kr>
//				  Youngjin Jo <yjjo@enc.hanyang.ac.kr>
//				  Sangjin Lee <sjlee@enc.hanyang.ac.kr>
//				  Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//
// This file is part of Cosmos+ OpenSSD.
//
// Cosmos+ OpenSSD is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// Cosmos+ OpenSSD is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Cosmos+ OpenSSD; see the file COPYING.
// If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Company: ENC Lab. <http://enc.hanyang.ac.kr>
// Engineer: Sangjin Lee <sjlee@enc.hanyang.ac.kr>
//			 Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//
// Project Name: Cosmos+ OpenSSD
// Design Name: Cosmos+ Firmware
// Module Name: NVMe IO Command Handler
// File Name: nvme_io_cmd.c
//
// Version: v1.0.1
//
// Description:
//   - handles NVMe IO command
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.0.1
//   - header file for buffer is changed from "ia_lru_buffer.h" to "lru_buffer.h"
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////


#include "xil_printf.h"
#include "debug.h"
#include "io_access.h"

#include "nvme.h"
#include "host_lld.h"
#include "nvme_io_cmd.h"

#include "../ftl_config.h"
#include "../request_transform.h"
#include "../kv_store.h"	// [prj3] KV index

void handle_nvme_io_read(unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd)
{
	IO_READ_COMMAND_DW12 readInfo12;
	//IO_READ_COMMAND_DW13 readInfo13;
	//IO_READ_COMMAND_DW15 readInfo15;
	unsigned int startLba[2];
	unsigned int nlb;
	unsigned int nsid = nvmeIOCmd->NSID;


	readInfo12.dword = nvmeIOCmd->dword[12];
	//readInfo13.dword = nvmeIOCmd->dword[13];
	//readInfo15.dword = nvmeIOCmd->dword[15];

	startLba[0] = nvmeIOCmd->dword[10];
	startLba[1] = nvmeIOCmd->dword[11];
	nlb = readInfo12.NLB;

	ASSERT(startLba[0] < storageCapacity_L / USER_CHANNELS && (startLba[1] < STORAGE_CAPACITY_H || startLba[1] == 0));
	//ASSERT(nlb < MAX_NUM_OF_NLB);
	ASSERT((nvmeIOCmd->PRP1[0] & 0x3) == 0 && (nvmeIOCmd->PRP2[0] & 0x3) == 0); //error
	ASSERT(nvmeIOCmd->PRP1[1] < 0x10000 && nvmeIOCmd->PRP2[1] < 0x10000);

	ReqTransNvmeToSlice(cmdSlotTag, startLba[0] + (storageCapacity_L / USER_CHANNELS) * (nsid - 1), nlb, IO_NVM_READ);
}


void handle_nvme_io_write(unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd)
{
	IO_READ_COMMAND_DW12 writeInfo12;
	//IO_READ_COMMAND_DW13 writeInfo13;
	//IO_READ_COMMAND_DW15 writeInfo15;
	unsigned int startLba[2];
	unsigned int nlb;
	unsigned int nsid = nvmeIOCmd->NSID;

	writeInfo12.dword = nvmeIOCmd->dword[12];
	//writeInfo13.dword = nvmeIOCmd->dword[13];
	//writeInfo15.dword = nvmeIOCmd->dword[15];

	//if(writeInfo12.FUA == 1)
	//	xil_printf("write FUA\r\n");

	startLba[0] = nvmeIOCmd->dword[10];
	startLba[1] = nvmeIOCmd->dword[11];
	nlb = writeInfo12.NLB;

	ASSERT(startLba[0] < storageCapacity_L / USER_CHANNELS && (startLba[1] < STORAGE_CAPACITY_H || startLba[1] == 0));
	//ASSERT(nlb < MAX_NUM_OF_NLB);
	ASSERT((nvmeIOCmd->PRP1[0] & 0xF) == 0 && (nvmeIOCmd->PRP2[0] & 0xF) == 0);
	ASSERT(nvmeIOCmd->PRP1[1] < 0x10000 && nvmeIOCmd->PRP2[1] < 0x10000);

	ReqTransNvmeToSlice(cmdSlotTag, startLba[0] + (storageCapacity_L / USER_CHANNELS) * (nsid - 1), nlb, IO_NVM_WRITE);
}

// [prj3] KV-PUT: key in dword[10], value block count (NLB, 0-based) in dword[12], length in dword[13].
//        The key difference from block I/O: dword[10] holds the key instead of the start LBA.
void handle_nvme_kv_put(unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd) {
	uint32_t key = nvmeIOCmd->dword[10];
	uint32_t nlb = nvmeIOCmd->dword[12];
	uint32_t value_len = nvmeIOCmd->dword[13];

	ASSERT(key < MAX_KV_ENTRIES);

	// [prj3] Write the value at the end of the append-only log (kv_current_lsa) and save that place in the index
	uint32_t lsa = kv_current_lsa;

	kv_index[key].lsa = lsa;
	kv_index[key].value_len = value_len;
	kv_index[key].valid = 1;

	// [prj3] Store it through the normal block write path.
	//        The FTL does not know it is a KV command; it sees a normal LSA write.
	ReqTransNvmeToSlice(cmdSlotTag, lsa, nlb, IO_NVM_WRITE);

	kv_current_lsa += (nlb + 1);
}

// [prj3] KV-GET: find the key's place in the index and read it through the normal block read path
void handle_nvme_kv_get(unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd) {
	uint32_t key = nvmeIOCmd->dword[10];
	NVME_COMPLETION nvmeCPL;

	// [prj3] Unknown key: complete right away with "No such key", with no data transfer.
	//        Bit 0 of statusFieldWord is the Phase Tag, so shift the status code left by 1.
	if (key >= MAX_KV_ENTRIES || !kv_index[key].valid) {
		nvmeCPL.dword[0] = 0;
		nvmeCPL.statusFieldWord = (SC_NO_SUCH_KEY << 1);
		nvmeCPL.specific = 0;
		set_auto_nvme_cpl(cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
		return;
	}

	// [prj3] Values are fixed at 4KB -> NLB 0 (one block)
	uint32_t lsa = kv_index[key].lsa;
	uint32_t nlb = (4096 - 1) / BYTES_PER_NVME_BLOCK;

	// [prj3] Do not send the completion here.
	//        request_allocation.c sends it after the DMA, with the value length.
	ReqTransNvmeToSlice(cmdSlotTag, lsa, nlb, IO_NVM_READ);
}

void handle_nvme_io_cmd(NVME_COMMAND *nvmeCmd)
{
	NVME_IO_COMMAND *nvmeIOCmd;
	NVME_COMPLETION nvmeCPL;
	unsigned int opc;
	nvmeIOCmd = (NVME_IO_COMMAND*)nvmeCmd->cmdDword;
	/*		xil_printf("OPC = 0x%X\r\n", nvmeIOCmd->OPC);
			xil_printf("PRP1[63:32] = 0x%X, PRP1[31:0] = 0x%X\r\n", nvmeIOCmd->PRP1[1], nvmeIOCmd->PRP1[0]);
			xil_printf("PRP2[63:32] = 0x%X, PRP2[31:0] = 0x%X\r\n", nvmeIOCmd->PRP2[1], nvmeIOCmd->PRP2[0]);
			xil_printf("dword10 = 0x%X\r\n", nvmeIOCmd->dword10);
			xil_printf("dword11 = 0x%X\r\n", nvmeIOCmd->dword11);
			xil_printf("dword12 = 0x%X\r\n", nvmeIOCmd->dword12);*/


	opc = (unsigned int)nvmeIOCmd->OPC;

	switch(opc)
	{
		case IO_NVM_FLUSH:
		{
		//	xil_printf("IO Flush Command\r\n");
			nvmeCPL.dword[0] = 0;
			nvmeCPL.specific = 0x0;
			set_auto_nvme_cpl(nvmeCmd->cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
			break;
		}
		case IO_NVM_WRITE:
		{
//			xil_printf("IO Write Command\r\n");
			handle_nvme_io_write(nvmeCmd->cmdSlotTag, nvmeIOCmd);
			break;
		}
		case IO_NVM_READ:
		{
//			xil_printf("IO Read Command\r\n");
			handle_nvme_io_read(nvmeCmd->cmdSlotTag, nvmeIOCmd);
			break;
		}
		// [prj1] New HELLO command (0x03): print a self-introduction over UART (added in prj1)
		case IO_NVM_HELLO:
		{
			xil_printf("Name: HyungsunYoo\r\n");
			xil_printf("Affiliation: Sogang University Computer Science and Engineering\r\n");
			xil_printf("Interests: Embedded Systems and Operating Systems\r\n");

			nvmeCPL.dword[0] = 0;
			nvmeCPL.specific = 0x0;
			set_auto_nvme_cpl(nvmeCmd->cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
			break;
		}
		// [prj3] KV command cases
		case IO_NVM_KV_PUT:
		{
			handle_nvme_kv_put(nvmeCmd->cmdSlotTag, nvmeIOCmd);
			break;
		}
		case IO_NVM_KV_GET:
		{
			handle_nvme_kv_get(nvmeCmd->cmdSlotTag, nvmeIOCmd);
			break;
		}
		default:
		{
			xil_printf("Not Support IO Command OPC: %X\r\n", opc);
			ASSERT(0);
			break;
		}
	}
}

