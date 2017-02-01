/*
 * Copyright (C) 2016  Nexell Co., Ltd.
 * Author: Sangjong, Han <hans@nexell.co.kr>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "sysheader.h"

#include <nx_sdmmc.h>
#include "iSDHCBOOT.h"
#include "nx_bootheader.h"

#ifdef NX_DEBUG
#define dprintf         printf
#else
#define dprintf(x, ...) {}
#endif

void ResetCon(U32 devicenum, CBOOL en);
void GPIOSetAltFunction(U32 AltFunc);
U32 NX_CLKPWR_GetPLLFrequency(U32 PllNumber);

//------------------------------------------------------------------------------
static struct NX_CLKGEN_RegisterSet *const pgSDClkGenReg[3] = {
	(struct NX_CLKGEN_RegisterSet *)PHY_BASEADDR_CLKGEN18_MODULE,
	(struct NX_CLKGEN_RegisterSet *)PHY_BASEADDR_CLKGEN19_MODULE,
	(struct NX_CLKGEN_RegisterSet *)PHY_BASEADDR_CLKGEN20_MODULE
};
static U32 const SDResetNum[3] = {
	RESETINDEX_OF_SDMMC0_MODULE_i_nRST,
	RESETINDEX_OF_SDMMC1_MODULE_i_nRST,
	RESETINDEX_OF_SDMMC2_MODULE_i_nRST
};
struct NX_SDMMC_RegisterSet *const pgSDXCReg[3] = {
	(struct NX_SDMMC_RegisterSet *)PHY_BASEADDR_SDMMC0_MODULE,
	(struct NX_SDMMC_RegisterSet *)PHY_BASEADDR_SDMMC1_MODULE,
	(struct NX_SDMMC_RegisterSet *)PHY_BASEADDR_SDMMC2_MODULE
};

//------------------------------------------------------------------------------
#if 1
typedef struct {
	U32 nPllNum;
	U32 nFreqHz;
	U32 nClkDiv;
	U32 nClkGenDiv;
} NX_CLKINFO_SDMMC;

CBOOL   NX_SDMMC_GetClkParam( NX_CLKINFO_SDMMC *pClkInfo )
{
	U32 srcFreq;
	U32 nRetry = 1, nTemp = 0;
	CBOOL   fRet = CFALSE;

	srcFreq = NX_CLKPWR_GetPLLFrequency(pClkInfo->nPllNum);

retry_getparam:
	for (pClkInfo->nClkDiv = 2; ; pClkInfo->nClkDiv += 2) {
		nTemp   = (pClkInfo->nFreqHz * pClkInfo->nClkDiv);
		pClkInfo->nClkGenDiv  = (srcFreq / nTemp);

		if (srcFreq > (pClkInfo->nFreqHz * pClkInfo->nClkDiv))
			pClkInfo->nClkGenDiv += 2;

		if (pClkInfo->nClkGenDiv < 255)
			break;
	}

	nTemp = (srcFreq / (pClkInfo->nClkGenDiv * pClkInfo->nClkDiv));
	if (nTemp <= pClkInfo->nFreqHz) {
		fRet = CTRUE;
		goto exit_getparam;
	}

	if (nRetry) {
		nRetry--;
		goto retry_getparam;
	}

exit_getparam:

	return fRet;
}
#endif

//------------------------------------------------------------------------------
//static CBOOL	NX_SDMMC_SetClock(SDXCBOOTSTATUS * pSDXCBootStatus,
//				CBOOL enb,
//				U32 divider)
static CBOOL	NX_SDMMC_SetClock(SDXCBOOTSTATUS * pSDXCBootStatus,
				CBOOL enb, U32 nFreq)
{
	volatile U32 timeout;
	register struct NX_SDMMC_RegisterSet * const pSDXCReg =
		pgSDXCReg[pSDXCBootStatus->SDPort];
	register struct NX_CLKGEN_RegisterSet * const pSDClkGenReg =
		pgSDClkGenReg[pSDXCBootStatus->SDPort];
	NX_CLKINFO_SDMMC clkInfo;
	CBOOL ret;

#if defined(VERBOSE)
	dprintf("NX_SDMMC_SetClock : divider = %d\r\n", divider);
#endif

	//	NX_ASSERT( (1==divider) || (0==(divider&1)) );		// 1 or even number
	//	NX_ASSERT( (0<divider) && (510>=divider) );			// between 1 and 510

	//--------------------------------------------------------------------------
	// 1. Confirm that no card is engaged in any transaction.
	//	If there's a transaction, wait until it has been finished.
	//	while( NX_SDXC_IsDataTransferBusy() );
	//	while( NX_SDXC_IsCardDataBusy() );

#if defined(NX_DEBUG)
	if (pSDXCReg->STATUS & (NX_SDXC_STATUS_DATABUSY | NX_SDXC_STATUS_FSMBUSY)) {
#if defined(NX_DEBUG)
		if (pSDXCReg->STATUS & NX_SDXC_STATUS_DATABUSY)
			dprintf("%s : ERROR - Data is busy\r\n", __func__);

		if (pSDXCReg->STATUS & NX_SDXC_STATUS_FSMBUSY)
			dprintf("%s : ERROR - Data Transfer is busy\r\n", __func__);
#endif
		//return CFALSE;
		timeout = NX_SDMMC_TIMEOUT;
		while (timeout--) {
			if (!(pSDXCReg->STATUS &
				(NX_SDXC_STATUS_DATABUSY | NX_SDXC_STATUS_FSMBUSY)))
				break;
		}
		if(timeout == 0) {
			INFINTE_LOOP();
		}
	}
#endif

	//--------------------------------------------------------------------------
	// 2. Disable the output clock.
	pSDXCReg->CLKENA &= ~NX_SDXC_CLKENA_CLKENB;
	pSDXCReg->CLKENA |= NX_SDXC_CLKENA_LOWPWR;	// low power mode & clock disable

	pSDClkGenReg->CLKENB = NX_PCLKMODE_ALWAYS << 3 | NX_BCLKMODE_DYNAMIC << 0;
#if 0
	pSDClkGenReg->CLKGEN[0] = (pSDClkGenReg->CLKGEN[0] & ~(0x7 << 2 | 0xFF << 5))
				| (SDXC_CLKGENSRC << 2)	// set clock source
				| ((divider - 1) << 5)	// set clock divisor
				| (0UL << 1);		// set clock invert
#else

	clkInfo.nPllNum = NX_CLKSRC_SDMMC;
	clkInfo.nFreqHz = nFreq;
	ret = NX_SDMMC_GetClkParam(&clkInfo);
	if (ret == CTRUE) {
		pSDClkGenReg->CLKGEN[0] = (pSDClkGenReg->CLKGEN[0] &
						~(0x7 << 2 | 0xFF << 5))
			| (clkInfo.nPllNum << 2)		// set clock source
			| ((clkInfo.nClkGenDiv - 1) << 5)	// set clock divisor
			| (0UL << 1);			// set clock invert

		pSDXCReg->CLKDIV = (clkInfo.nClkDiv >> 1);	//  2*n divider (0 : bypass)
	}
#endif
	pSDClkGenReg->CLKENB |= 0x1UL << 2;		// clock generation enable
	pSDXCReg->CLKENA &= ~NX_SDXC_CLKENA_LOWPWR;	// normal power mode
	//--------------------------------------------------------------------------
	// 3. Program the clock divider as required.
	//	pSDXCReg->CLKSRC = 0;	// prescaler 0
	//	pSDXCReg->CLKDIV = SDXC_CLKDIV >> 1;	//2*n divider (0 : bypass)
	//	pSDXCReg->CLKDIV = (divider >> 1);	//2*n divider (0 : bypass)

	//--------------------------------------------------------------------------
	// 4. Start a command with NX_SDXC_CMDFLAG_UPDATECLKONLY flag.
repeat_4 :
	pSDXCReg->CMD = 0 | NX_SDXC_CMDFLAG_STARTCMD |
			NX_SDXC_CMDFLAG_UPDATECLKONLY |
			NX_SDXC_CMDFLAG_STOPABORT;

	//--------------------------------------------------------------------------
	// 5. Wait until a update clock command is taken by the SDXC module.
	//	If a HLE is occurred, repeat 4.
	timeout = 0;
	while (pSDXCReg->CMD & NX_SDXC_CMDFLAG_STARTCMD) {
		if (++timeout > NX_SDMMC_TIMEOUT) {
			dprintf("%s : ERROR - Time-out to update clock.\r\n", __func__);
			INFINTE_LOOP();
			return CFALSE;
		}
	}

	if (pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_HLE) {
		INFINTE_LOOP();
		pSDXCReg->RINTSTS = NX_SDXC_RINTSTS_HLE;
		goto repeat_4;
	}

	if (CFALSE == enb)
		return CTRUE;

	//--------------------------------------------------------------------------
	// 6. Enable the output clock.
	pSDXCReg->CLKENA |= NX_SDXC_CLKENA_CLKENB;

	//--------------------------------------------------------------------------
	// 7. Start a command with NX_SDXC_CMDFLAG_UPDATECLKONLY flag.
repeat_7 :
	//	pSDXCReg->CMD = 0 | NX_SDXC_CMDFLAG_STARTCMD |
	//			NX_SDXC_CMDFLAG_UPDATECLKONLY |
	//			NX_SDXC_CMDFLAG_WAITPRVDAT;
	pSDXCReg->CMD = 0 | NX_SDXC_CMDFLAG_STARTCMD |
			NX_SDXC_CMDFLAG_UPDATECLKONLY |
			NX_SDXC_CMDFLAG_STOPABORT;

	//--------------------------------------------------------------------------
	// 8. Wait until a update clock command is taken by the SDXC module.
	//	If a HLE is occurred, repeat 7.
	timeout = 0;
	while (pSDXCReg->CMD & NX_SDXC_CMDFLAG_STARTCMD) {
		if (++timeout > NX_SDMMC_TIMEOUT){
			dprintf("%s : ERROR - TIme-out to update clock2.\r\n", __func__);
			INFINTE_LOOP();
			return CFALSE;
		}
	}

	if (pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_HLE) {
		INFINTE_LOOP();
		pSDXCReg->RINTSTS = NX_SDXC_RINTSTS_HLE;
		goto repeat_7;
	}

	return CTRUE;
}

//------------------------------------------------------------------------------
static U32 NX_SDMMC_SendCommandInternal(
		SDXCBOOTSTATUS *pSDXCBootStatus,
		NX_SDMMC_COMMAND *pCommand)
{
	U32 cmd, flag;
	U32 status = 0;
	volatile U32 timeout;
	register struct NX_SDMMC_RegisterSet *const pSDXCReg =
		pgSDXCReg[pSDXCBootStatus->SDPort];

	NX_ASSERT(CNULL != pCommand);

#ifdef VERBOSE
	dprintf("%s : Command(0x%08X), "
			"Argument(0x%08X)\r\n",
			__func__, pCommand->cmdidx, pCommand->arg);
#endif

	cmd = pCommand->cmdidx & 0xFF;
	flag = pCommand->flag;

	pSDXCReg->RINTSTS = 0xFFFFFFFF;

	//--------------------------------------------------------------------------
	// Send Command
	timeout = 0;
	do {
		pSDXCReg->RINTSTS	= NX_SDXC_RINTSTS_HLE;
		pSDXCReg->CMDARG	= pCommand->arg;
		pSDXCReg->CMD		= cmd | flag | NX_SDXC_CMDFLAG_USE_HOLD_REG;
		while (pSDXCReg->CMD & NX_SDXC_CMDFLAG_STARTCMD) {
			if (++timeout > NX_SDMMC_TIMEOUT) {
				dprintf("%s : ERROR - Time-Out to send command.\r\n", __func__);
				status |= NX_SDMMC_STATUS_CMDBUSY;
				INFINTE_LOOP();
				goto End;
			}
		}
	} while (pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_HLE);

	//--------------------------------------------------------------------------
	// Wait until Command sent to card and got response from card.
	timeout = 0;
	while (1) {
		if (pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_CD)
			break;

		if (++timeout > NX_SDMMC_TIMEOUT) {
			dprintf("%s : ERROR - Time-Out to wait command done.\r\n", __func__);
			status |= NX_SDMMC_STATUS_CMDTOUT;
			INFINTE_LOOP();
			goto End;
		}

		if ((flag & NX_SDXC_CMDFLAG_STOPABORT) &&
				(pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_HTO)) {
			// You have to clear FIFO when HTO is occurred.
			// After that, SDXC module leaves in stopped state and takes next command.
			while (0 == (pSDXCReg->STATUS & NX_SDXC_STATUS_FIFOEMPTY)) {
				pSDXCReg->DATA;
			}
		}
	}

	// Check Response Error
	if (pSDXCReg->RINTSTS & (NX_SDXC_RINTSTS_RCRC |
				NX_SDXC_RINTSTS_RE |
				NX_SDXC_RINTSTS_RTO)) {
		if (pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_RCRC)
			status |= NX_SDMMC_STATUS_RESCRCFAIL;
		if (pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_RE)
			status |= NX_SDMMC_STATUS_RESERROR;
		if (pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_RTO)
			status |= NX_SDMMC_STATUS_RESTOUT;
	}

	if ((status == NX_SDMMC_STATUS_NOERROR) &&
			(flag & NX_SDXC_CMDFLAG_SHORTRSP)) {
		pCommand->response[0] = pSDXCReg->RESP0;
		if ((flag & NX_SDXC_CMDFLAG_LONGRSP) ==
				NX_SDXC_CMDFLAG_LONGRSP) {
			pCommand->response[1] = pSDXCReg->RESP1;
			pCommand->response[2] = pSDXCReg->RESP2;
			pCommand->response[3] = pSDXCReg->RESP3;
		}

		if (NX_SDMMC_RSPIDX_R1B == ((pCommand->cmdidx >> 8) & 0xFF)) {
			timeout = 0;
			do {
				if ( ++timeout > NX_SDMMC_TIMEOUT) {
					dprintf("%s : ERROR - Time-Out to wait card data is ready.\r\n", __func__);
					status |= NX_SDMMC_STATUS_DATABUSY;
					INFINTE_LOOP();
					goto End;
				}
			} while (pSDXCReg->STATUS & NX_SDXC_STATUS_DATABUSY);
		}
	}

End:

#if defined(NX_DEBUG)
	if (NX_SDMMC_STATUS_NOERROR != status) {
		dprintf("%s Failed : command(0x%08X), argument(0x%08X) => status(0x%08X)\r\n",
				__func__, pCommand->cmdidx, pCommand->arg, status);
	}
#endif

	pCommand->status = status;

	return status;
}

//------------------------------------------------------------------------------
static U32 NX_SDMMC_SendStatus(SDXCBOOTSTATUS *pSDXCBootStatus)
{
	U32 status;
	NX_SDMMC_COMMAND cmd;

	cmd.cmdidx	= SEND_STATUS;
	cmd.arg		= pSDXCBootStatus->rca;
	cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD |
			NX_SDXC_CMDFLAG_CHKRSPCRC |
			NX_SDXC_CMDFLAG_SHORTRSP;

	status = NX_SDMMC_SendCommandInternal(pSDXCBootStatus, &cmd);

#if defined(VERBOSE) && defined(NX_DEBUG) && (1)
	if (NX_SDMMC_STATUS_NOERROR == status) {
		dprintf("\t %s : idx:0x%08X, arg:0x%08X, resp:0x%08X\r\n",
				__func__, cmd.cmdidx, cmd.arg, cmd.response[0]);

		if (cmd.response[0] & (1UL << 31))
			dprintf("\t\t ERROR : OUT_OF_RANGE\r\n");
		if (cmd.response[0] & (1UL << 30))
			dprintf("\t\t ERROR : ADDRESS_ERROR\r\n");
		if (cmd.response[0] & (1UL << 29))
			dprintf("\t\t ERROR : BLOCK_LEN_ERROR\r\n");
		if (cmd.response[0] & (1UL << 28))
			dprintf("\t\t ERROR : ERASE_SEQ_ERROR\r\n");
		if (cmd.response[0] & (1UL << 27))
			dprintf("\t\t ERROR : ERASE_PARAM\r\n");
		if (cmd.response[0] & (1UL << 26))
			dprintf("\t\t ERROR : WP_VIOLATION\r\n");
		if (cmd.response[0] & (1UL << 24))
			dprintf("\t\t ERROR : LOCK_UNLOCK_FAILED\r\n");
		if (cmd.response[0] & (1UL << 23))
			dprintf("\t\t ERROR : COM_CRC_ERROR\r\n");
		if (cmd.response[0] & (1UL << 22))
			dprintf("\t\t ERROR : ILLEGAL_COMMAND\r\n");
		if (cmd.response[0] & (1UL << 21))
			dprintf("\t\t ERROR : CARD_ECC_FAILED\r\n");
		if (cmd.response[0] & (1UL << 20))
			dprintf("\t\t ERROR : Internal Card Controller ERROR\r\n");
		if (cmd.response[0] & (1UL << 19))
			dprintf("\t\t ERROR : General Error\r\n");
		if (cmd.response[0] & (1UL << 17))
			dprintf("\t\t ERROR : Deferred Response\r\n");
		if (cmd.response[0] & (1UL << 16))
			dprintf("\t\t ERROR : CID/CSD_OVERWRITE_ERROR\r\n");
		if (cmd.response[0] & (1UL << 15))
			dprintf("\t\t ERROR : WP_ERASE_SKIP\r\n");
		if (cmd.response[0] & (1UL <<  3))
			dprintf("\t\t ERROR : AKE_SEQ_ERROR\r\n");

		dprintf("\t\t CURRENT_STATE : ");
		switch ((cmd.response[0] >> 9) & 0xF) {
		case 0 : dprintf("Idle\r\n");		break;
		case 1 : dprintf("Ready\r\n");		break;
		case 2 : dprintf("Identification\r\n");	break;
		case 3 : dprintf("Standby\r\n");	break;
		case 4 : dprintf("Transfer\r\n");	break;
		case 5 : dprintf("Data\r\n");		break;
		case 6 : dprintf("Receive\r\n");	break;
		case 7 : dprintf("Programming\r\n");	break;
		case 8 : dprintf("Disconnect\r\n");	break;
		case 9 : dprintf("Sleep\r\n");		break;
		default: dprintf("Reserved\r\n");	break;
		}
	}
#endif

	return status;
}

//------------------------------------------------------------------------------
static U32 NX_SDMMC_SendCommand(SDXCBOOTSTATUS *pSDXCBootStatus,
		NX_SDMMC_COMMAND *pCommand)
{
	U32 status;

	status = NX_SDMMC_SendCommandInternal(pSDXCBootStatus, pCommand);
	if (NX_SDMMC_STATUS_NOERROR != status)
		NX_SDMMC_SendStatus(pSDXCBootStatus);

	return status;
}

//------------------------------------------------------------------------------
static U32 NX_SDMMC_SendAppCommand(SDXCBOOTSTATUS *pSDXCBootStatus,
		NX_SDMMC_COMMAND *pCommand)
{
	U32 status;
	NX_SDMMC_COMMAND cmd;

	cmd.cmdidx = APP_CMD;
	cmd.arg = pSDXCBootStatus->rca;
	cmd.flag = NX_SDXC_CMDFLAG_STARTCMD |
		NX_SDXC_CMDFLAG_WAITPRVDAT |
		NX_SDXC_CMDFLAG_CHKRSPCRC |
		NX_SDXC_CMDFLAG_SHORTRSP;

	status = NX_SDMMC_SendCommandInternal(pSDXCBootStatus, &cmd);
	if (NX_SDMMC_STATUS_NOERROR == status)
		NX_SDMMC_SendCommand(pSDXCBootStatus, pCommand);

	return status;
}

//------------------------------------------------------------------------------
static CBOOL NX_SDMMC_IdentifyCard(SDXCBOOTSTATUS *pSDXCBootStatus)
{
	S32 timeout;
	U32 HCS, RCA;
	NX_SDMMC_CARDTYPE CardType = NX_SDMMC_CARDTYPE_UNKNOWN;
	NX_SDMMC_COMMAND cmd;
	struct NX_SDMMC_RegisterSet *const pSDXCReg =
		pgSDXCReg[pSDXCBootStatus->SDPort];

#if 0
	if (CFALSE == NX_SDMMC_SetClock(pSDXCBootStatus,
					CTRUE,
					SDXC_CLKGENDIV_400KHZ))
		return CFALSE;
#else
	if (CFALSE == NX_SDMMC_SetClock(pSDXCBootStatus, CTRUE, 400000))
		return CFALSE;
#endif

	// Data Bus Width : 0(1-bit), 1(4-bit)
	pSDXCReg->CTYPE = 0;

	pSDXCBootStatus->rca = 0;

	//----------------------------------------------------------------------
	//	Identify SD/MMC card
	//----------------------------------------------------------------------
	// Go idle state
	cmd.cmdidx	= GO_IDLE_STATE;
	cmd.arg		= 0;
	cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD |
			NX_SDXC_CMDFLAG_SENDINIT |
			NX_SDXC_CMDFLAG_STOPABORT;

	NX_SDMMC_SendCommand(pSDXCBootStatus, &cmd);

	// Check SD Card Version
	cmd.cmdidx	= SEND_IF_COND;
	// argument = VHS : 2.7~3.6V and Check Pattern(0xAA)
	cmd.arg		= (1 << 8) | 0xAA;
	cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD |
			NX_SDXC_CMDFLAG_WAITPRVDAT |
			NX_SDXC_CMDFLAG_CHKRSPCRC |
			NX_SDXC_CMDFLAG_SHORTRSP;

	if (NX_SDMMC_STATUS_NOERROR ==
			NX_SDMMC_SendCommandInternal(pSDXCBootStatus, &cmd)) {
		// Ver 2.0 or later SD Memory Card
		if (cmd.response[0] != ((1 << 8) | 0xAA))
			return CFALSE;

		HCS = 1 << 30;
		dprintf("Ver 2.0 or later SD Memory Card\r\n");
	} else {
		// voltage mismatch or Ver 1.X SD Memory Card or not SD Memory
		// Card
		HCS = 0;
		dprintf("voltage mismatch or"
			" Ver 1.X SD Memory Card or"
			" not SD Memory Card\r\n");
	}

	//--------------------------------------------------------------------------
	// voltage validation
	timeout = NX_SDMMC_TIMEOUT_IDENTIFY;

	cmd.cmdidx = APP_CMD;
	cmd.arg = pSDXCBootStatus->rca;
	cmd.flag = NX_SDXC_CMDFLAG_STARTCMD |
		NX_SDXC_CMDFLAG_WAITPRVDAT |
		NX_SDXC_CMDFLAG_CHKRSPCRC |
		NX_SDXC_CMDFLAG_SHORTRSP;

	if (NX_SDMMC_STATUS_NOERROR == NX_SDMMC_SendCommandInternal(
						pSDXCBootStatus, &cmd ) ) {
		//----------------------------------------------------------------------
		// SD memory card
#define FAST_BOOT	(1 << 29)

		cmd.cmdidx	= SD_SEND_OP_COND;
		cmd.arg		= (HCS | FAST_BOOT | 0x00FC0000);// 3.0 ~ 3.6V
		cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD |
				NX_SDXC_CMDFLAG_WAITPRVDAT |
				NX_SDXC_CMDFLAG_SHORTRSP;

		if (NX_SDMMC_STATUS_NOERROR != NX_SDMMC_SendCommandInternal(
						pSDXCBootStatus, &cmd ) ) {
			return CFALSE;
		}
		// Wait until card has finished the power up routine
		while (0 == (cmd.response[0] & (1UL << 31))) {
			if (NX_SDMMC_STATUS_NOERROR !=
					NX_SDMMC_SendAppCommand(
						pSDXCBootStatus, &cmd)) {
				return CFALSE;
			}

			if (timeout-- <= 0) {
				dprintf("%s : ERROR - Time-Out to wait power up for SD.\r\n", __func__);
				return CFALSE;
			}
		}

#if defined(VERBOSE)
		dprintf("--> Found SD Memory Card.\r\n");
		dprintf("--> SD_SEND_OP_COND Response = 0x%08X\r\n",
				cmd.response[0]);
#endif

		CardType	= NX_SDMMC_CARDTYPE_SDMEM;
		RCA		= 0;
	} else {
		//----------------------------------------------------------------------
		// MMC memory card
		cmd.cmdidx	= GO_IDLE_STATE;
		cmd.arg		= 0;
		cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD |
				NX_SDXC_CMDFLAG_SENDINIT |
				NX_SDXC_CMDFLAG_STOPABORT;

		NX_SDMMC_SendCommand(pSDXCBootStatus, &cmd);

		do {
			cmd.cmdidx	= SEND_OP_COND;
			// MMC High Capacity -_-???
			cmd.arg		= 0x40FC0000;
			cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD |
					NX_SDXC_CMDFLAG_WAITPRVDAT |
					NX_SDXC_CMDFLAG_SHORTRSP;
			if (NX_SDMMC_STATUS_NOERROR !=
					NX_SDMMC_SendCommand(
						pSDXCBootStatus, &cmd)) {
				return CFALSE;
			}

			if (timeout-- <= 0) {
				dprintf("%s : ERROR - Time-Out to wait power-up for MMC.\r\n", __func__);
				return CFALSE;
			}
		// Wait until card has finished the power up routine
		} while (0 == (cmd.response[0] & (1UL << 31)));

#if defined(VERBOSE)
		dprintf("--> Found MMC Memory Card.\r\n");
		dprintf("--> SEND_OP_COND Response = 0x%08X\r\n", cmd.response[0]);
#endif

		CardType	= NX_SDMMC_CARDTYPE_MMC;
		RCA		= 2 << 16;
	}

	//	NX_ASSERT((cmd.response[0] & 0x00FC0000) == 0x00FC0000);
	pSDXCBootStatus->bHighCapacity = (cmd.response[0] & (1 << 30)) ?
								CTRUE : CFALSE;

#if defined(NX_DEBUG)
	if (pSDXCBootStatus->bHighCapacity)
		dprintf("--> High Capacity Memory Card.\r\n");
#endif

	//----------------------------------------------------------------------
	// Get CID
	cmd.cmdidx	= ALL_SEND_CID;
	cmd.arg		= 0;
	cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD |
			NX_SDXC_CMDFLAG_WAITPRVDAT |
			NX_SDXC_CMDFLAG_CHKRSPCRC |
			NX_SDXC_CMDFLAG_LONGRSP;
	if (NX_SDMMC_STATUS_NOERROR !=
			NX_SDMMC_SendCommand(pSDXCBootStatus, &cmd))
		return CFALSE;

	//----------------------------------------------------------------------
	// Get RCA and change to Stand-by State in data transfer mode
	cmd.cmdidx	= (CardType == NX_SDMMC_CARDTYPE_SDMEM) ?
					SEND_RELATIVE_ADDR : SET_RELATIVE_ADDR;
	cmd.arg		= RCA;
	cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD |
			NX_SDXC_CMDFLAG_WAITPRVDAT |
			NX_SDXC_CMDFLAG_CHKRSPCRC |
			NX_SDXC_CMDFLAG_SHORTRSP;
	if (NX_SDMMC_STATUS_NOERROR !=
			NX_SDMMC_SendCommand(pSDXCBootStatus, &cmd))
		return CFALSE;

	if (CardType == NX_SDMMC_CARDTYPE_SDMEM)
		pSDXCBootStatus->rca = cmd.response[0] & 0xFFFF0000;
	else
		pSDXCBootStatus->rca = RCA;

#if defined(VERBOSE)
	dprintf("RCA = 0x%08X\r\n", pSDXCBootStatus->rca);
#endif

	pSDXCBootStatus->CardType = CardType;

	return CTRUE;
}

//------------------------------------------------------------------------------
static CBOOL NX_SDMMC_SelectCard(SDXCBOOTSTATUS *pSDXCBootStatus)
{
	U32 status;
	NX_SDMMC_COMMAND cmd;

	cmd.cmdidx = SELECT_CARD;
	cmd.arg = pSDXCBootStatus->rca;
	cmd.flag =	NX_SDXC_CMDFLAG_STARTCMD |
			NX_SDXC_CMDFLAG_WAITPRVDAT |
			NX_SDXC_CMDFLAG_CHKRSPCRC |
			NX_SDXC_CMDFLAG_SHORTRSP;

	status = NX_SDMMC_SendCommand(pSDXCBootStatus, &cmd);

	return (NX_SDMMC_STATUS_NOERROR == status) ? CTRUE : CFALSE;
}

//------------------------------------------------------------------------------
static CBOOL NX_SDMMC_SetCardDetectPullUp(
		SDXCBOOTSTATUS *pSDXCBootStatus,
		CBOOL bEnb)
{
	U32 status;
	NX_SDMMC_COMMAND cmd;

	cmd.cmdidx = SET_CLR_CARD_DETECT;
	cmd.arg = (bEnb) ? 1 : 0;
	cmd.flag = NX_SDXC_CMDFLAG_STARTCMD |
		NX_SDXC_CMDFLAG_WAITPRVDAT |
		NX_SDXC_CMDFLAG_CHKRSPCRC |
		NX_SDXC_CMDFLAG_SHORTRSP;

	status = NX_SDMMC_SendAppCommand(pSDXCBootStatus, &cmd);

	return (NX_SDMMC_STATUS_NOERROR == status) ? CTRUE : CFALSE;
}

//------------------------------------------------------------------------------
static CBOOL NX_SDMMC_SetBusWidth(SDXCBOOTSTATUS *pSDXCBootStatus, U32 buswidth)
{
	U32 status;
	NX_SDMMC_COMMAND cmd;
	register struct NX_SDMMC_RegisterSet * const pSDXCReg =
		pgSDXCReg[pSDXCBootStatus->SDPort];

	NX_ASSERT(buswidth == 1 || buswidth == 4);

	if (pSDXCBootStatus->CardType == NX_SDMMC_CARDTYPE_SDMEM) {
		cmd.cmdidx	= SET_BUS_WIDTH;
		cmd.arg		= (buswidth >> 1);
		cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD |
				NX_SDXC_CMDFLAG_WAITPRVDAT |
				NX_SDXC_CMDFLAG_CHKRSPCRC |
				NX_SDXC_CMDFLAG_SHORTRSP;
		status = NX_SDMMC_SendAppCommand(pSDXCBootStatus, &cmd);
	} else {
		// ExtCSD[183] : BUS_WIDTH <= 0 : 1-bit, 1 : 4-bit, 2 : 8-bit
		cmd.cmdidx	= SWITCH_FUNC;
		cmd.arg		=	(3	<< 24) |
					(183	<< 16) |
				((buswidth >> 2) <<  8) |
					(0	<<  0);
		cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD |
				NX_SDXC_CMDFLAG_WAITPRVDAT |
				NX_SDXC_CMDFLAG_CHKRSPCRC |
				NX_SDXC_CMDFLAG_SHORTRSP;
		status = NX_SDMMC_SendCommand(pSDXCBootStatus, &cmd);
	}

	if (NX_SDMMC_STATUS_NOERROR != status)
		return CFALSE;

	// 0 : 1-bit mode, 1 : 4-bit mode
	pSDXCReg->CTYPE = buswidth >> 2;

	return CTRUE;
}

//------------------------------------------------------------------------------
static CBOOL	NX_SDMMC_SetBlockLength(SDXCBOOTSTATUS * pSDXCBootStatus,
					U32 blocklength )
{
	U32 status;
	NX_SDMMC_COMMAND cmd;
	register struct NX_SDMMC_RegisterSet * const pSDXCReg =
		pgSDXCReg[pSDXCBootStatus->SDPort];

	cmd.cmdidx = SET_BLOCKLEN;
	cmd.arg = blocklength;
	cmd.flag = NX_SDXC_CMDFLAG_STARTCMD |
		NX_SDXC_CMDFLAG_WAITPRVDAT |
		NX_SDXC_CMDFLAG_CHKRSPCRC |
		NX_SDXC_CMDFLAG_SHORTRSP;
	status = NX_SDMMC_SendCommand(pSDXCBootStatus, &cmd);

	if (NX_SDMMC_STATUS_NOERROR != status)
		return CFALSE;

	pSDXCReg->BLKSIZ = blocklength;

	return CTRUE;
}

//------------------------------------------------------------------------------
CBOOL NX_SDMMC_Init(SDXCBOOTSTATUS *pSDXCBootStatus)
{
	register struct NX_SDMMC_RegisterSet *const pSDXCReg =
		pgSDXCReg[pSDXCBootStatus->SDPort];
	register struct NX_CLKGEN_RegisterSet *const pSDClkGenReg =
		pgSDClkGenReg[pSDXCBootStatus->SDPort];
#if 1
	NX_CLKINFO_SDMMC clkInfo;
	CBOOL ret;

	clkInfo.nPllNum = NX_CLKSRC_SDMMC;
	clkInfo.nFreqHz = 25000000;

	ret = NX_SDMMC_GetClkParam(&clkInfo);
	if (ret == CFALSE)
		printf("get clock param fail.\r\n");
#endif

	// CLKGEN
	pSDClkGenReg->CLKENB = NX_PCLKMODE_ALWAYS << 3 |
				NX_BCLKMODE_DYNAMIC << 0;
#if 0
	pSDClkGenReg->CLKGEN[0] = (pSDClkGenReg->CLKGEN[0] &
					~(0x7 << 2 | 0xFF << 5))
			| (SDXC_CLKGENSRC << 2)		// set clock source
			| ((SDXC_CLKGENDIV - 1) << 5)	// set clock divisor
			| (0UL << 1);			// set clock invert
#else
	pSDClkGenReg->CLKGEN[0] = (pSDClkGenReg->CLKGEN[0] &
					~(0x7 << 2 | 0xFF << 5))
			| (clkInfo.nPllNum << 2)	// set clock source
			| ((clkInfo.nClkGenDiv - 1) << 5)	// set clock divisor
			| (0UL << 1);			// set clock invert
#endif
	pSDClkGenReg->CLKENB |= 0x1UL << 2; // clock generation enable

	ResetCon(SDResetNum[pSDXCBootStatus->SDPort], CTRUE);  // reset on
	ResetCon(SDResetNum[pSDXCBootStatus->SDPort], CFALSE); // reset negate

	pSDXCReg->PWREN = 0 << 0; // Set Power Disable

	//	pSDXCReg->UHS_REG |= 1 << 0;		// for DDR mode

	// low power mode & clock disable
	pSDXCReg->CLKENA = NX_SDXC_CLKENA_LOWPWR;
	pSDXCReg->CLKCTRL = 0 << 24 |		// sample clock phase shift 0:0 1:90 2:180 3:270
			2 << 16 |		// drive clock phase shift 0:0 1:90 2:180 3:270
			0 << 8 |		// sample clock delay
			0 << 0;			// drive clock delay

	pSDXCReg->CLKSRC = 0; // prescaler 0
#if 0
	pSDXCReg->CLKDIV = (SDXC_CLKDIV >> 1);    //	2*n divider (0 : bypass)
#else
	pSDXCReg->CLKDIV = (clkInfo.nClkGenDiv >> 1);
#endif

	// fifo mode, not read wait(only use sdio mode)
	pSDXCReg->CTRL &= ~(NX_SDXC_CTRL_DMAMODE_EN | NX_SDXC_CTRL_READWAIT);
	// Reset the controller & DMA interface & FIFO
	pSDXCReg->CTRL = NX_SDXC_CTRL_DMARST |
			NX_SDXC_CTRL_FIFORST |
			NX_SDXC_CTRL_CTRLRST;
	while (pSDXCReg->CTRL & (NX_SDXC_CTRL_DMARST |
				NX_SDXC_CTRL_FIFORST |
				NX_SDXC_CTRL_CTRLRST));

	pSDXCReg->PWREN = 0x1 << 0;	// Set Power Enable


	// Data Timeout = 0xFFFFFF, Response Timeout = 0x64
	pSDXCReg->TMOUT = (0xFFFFFFU << 8) | (0x64 << 0);

	// Data Bus Width : 0(1-bit), 1(4-bit)
	pSDXCReg->CTYPE = 0;

	// Block size
	pSDXCReg->BLKSIZ = BLOCK_LENGTH;

	// Issue when RX FIFO Count >= 8 x 4 bytes & TX FIFO Count <= 8 x 4 bytes
	pSDXCReg->FIFOTH = ((8 - 1) << 16) |	// Rx threshold
				(8 << 0);	// Tx threshold

	// Mask & Clear All interrupts
	pSDXCReg->INTMASK = 0;
	pSDXCReg->RINTSTS = 0xFFFFFFFF;

	return CTRUE;
}

//------------------------------------------------------------------------------
CBOOL NX_SDMMC_Terminate(SDXCBOOTSTATUS *pSDXCBootStatus)
{
	register struct NX_SDMMC_RegisterSet *const pSDXCReg =
		pgSDXCReg[pSDXCBootStatus->SDPort];
	// Clear All interrupts
	pSDXCReg->RINTSTS = 0xFFFFFFFF;

	// Reset the controller & DMA interface & FIFO
	pSDXCReg->CTRL = NX_SDXC_CTRL_DMARST |
			NX_SDXC_CTRL_FIFORST |
			NX_SDXC_CTRL_CTRLRST;
	while (pSDXCReg->CTRL & (NX_SDXC_CTRL_DMARST |
				NX_SDXC_CTRL_FIFORST |
				NX_SDXC_CTRL_CTRLRST));

	// Disable CLKGEN
	pgSDClkGenReg[pSDXCBootStatus->SDPort]->CLKENB = 0;

	ResetCon(SDResetNum[pSDXCBootStatus->SDPort], CTRUE); // reset on

	return CTRUE;
}

//------------------------------------------------------------------------------
CBOOL NX_SDMMC_Open(SDXCBOOTSTATUS *pSDXCBootStatus) // U32 option )
{
	//----------------------------------------------------------------------
	// card identification mode : Identify & Initialize
	if (CFALSE == NX_SDMMC_IdentifyCard(pSDXCBootStatus)) {
		printf("Card Identify Failure\r\n");
		return CFALSE;
	}

	//----------------------------------------------------------------------
	// data transfer mode : Stand-by state
#if 0
	if (CFALSE == NX_SDMMC_SetClock(pSDXCBootStatus, CTRUE, SDXC_CLKGENDIV))
		return CFALSE;
#else
	if (CFALSE == NX_SDMMC_SetClock(pSDXCBootStatus, CTRUE, 25000000))
		return CFALSE;
#endif
	if (CFALSE == NX_SDMMC_SelectCard(pSDXCBootStatus))
		return CFALSE;

	//--------------------------------------------------------------------------
	// data transfer mode : Transfer state
	if (pSDXCBootStatus->CardType == NX_SDMMC_CARDTYPE_SDMEM) {
		NX_SDMMC_SetCardDetectPullUp(pSDXCBootStatus, CFALSE);
	}

	if (CFALSE == NX_SDMMC_SetBlockLength(pSDXCBootStatus, BLOCK_LENGTH))
		return CFALSE;

	NX_SDMMC_SetBusWidth(pSDXCBootStatus, 4);

	return CTRUE;
}

//------------------------------------------------------------------------------
CBOOL NX_SDMMC_Close(SDXCBOOTSTATUS *pSDXCBootStatus)
{
	//NX_SDMMC_SetClock(pSDXCBootStatus, CFALSE, SDXC_CLKGENDIV_400KHZ);
	NX_SDMMC_SetClock(pSDXCBootStatus, CFALSE, 400000);
	return CTRUE;
}

//------------------------------------------------------------------------------
static CBOOL NX_SDMMC_ReadSectorData(SDXCBOOTSTATUS *pSDXCBootStatus,
		U32 numberOfSector, U32 *pdwBuffer)
{
	U32		count;
	register struct NX_SDMMC_RegisterSet * const pSDXCReg =
		pgSDXCReg[pSDXCBootStatus->SDPort];

	NX_ASSERT(0 == ((U32)pdwBuffer & 3));

	count = numberOfSector * BLOCK_LENGTH;
	NX_ASSERT(0 == (count % 32));

	while (0 < count) {
		if ((pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_RXDR) ||
				(pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_DTO)) {
			U32 FSize, Timeout = NX_SDMMC_TIMEOUT;
			while ((pSDXCReg->STATUS & NX_SDXC_STATUS_FIFOEMPTY) &&
					Timeout--);
			if (0 == Timeout)
				break;
			FSize = (pSDXCReg->STATUS & NX_SDXC_STATUS_FIFOCOUNT) >> 17;
			count -= (FSize * 4);
			while (FSize) {
				*pdwBuffer++ = pSDXCReg->DATA;
				FSize--;
			}

			pSDXCReg->RINTSTS = NX_SDXC_RINTSTS_RXDR;
		}

		// Check Errors
		if (pSDXCReg->RINTSTS & (NX_SDXC_RINTSTS_DRTO |
					NX_SDXC_RINTSTS_EBE |
					NX_SDXC_RINTSTS_SBE |
					NX_SDXC_RINTSTS_DCRC)) {
#if defined(NX_DEBUG)
			dprintf("Read left = %d\r\n", count);

			if (pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_DRTO)
			        dprintf("ERROR : %s - NX_SDXC_RINTSTS_DRTO\r\n", __func__);
			if (pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_EBE)
			        dprintf("ERROR : %s - NX_SDXC_RINTSTS_EBE\r\n", __func__);
			if (pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_SBE)
			        dprintf("ERROR : %s - NX_SDXC_RINTSTS_SBE\r\n", __func__);
			if (pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_DCRC)
				dprintf("ERROR : %s - NX_SDXC_RINTSTS_DCRC\r\n", __func__);
#endif

			return CFALSE;
		}

		if (pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_DTO) {
			if (count == 0) {
				pSDXCReg->RINTSTS = NX_SDXC_RINTSTS_DTO;
				break;
			}
		}

#if defined(NX_DEBUG)
		if (pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_HTO) {
			dprintf("ERROR : NX_SDMMC_ReadSectors - "
					"NX_SDXC_RINTSTS_HTO\r\n");
			pSDXCReg->RINTSTS = NX_SDXC_RINTSTS_HTO;
		}
#endif

		NX_ASSERT(0 == (pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_FRUN));
	}

	pSDXCReg->RINTSTS = NX_SDXC_RINTSTS_DTO;

	return CTRUE;
}

//------------------------------------------------------------------------------
CBOOL NX_SDMMC_ReadSectors(SDXCBOOTSTATUS *pSDXCBootStatus, U32 SectorNum,
		U32 numberOfSector, U32 *pdwBuffer)
{
	CBOOL	result = CFALSE;
	U32	status;
#if defined(NX_DEBUG)
	U32	response;
#endif
	NX_SDMMC_COMMAND cmd;
	register struct NX_SDMMC_RegisterSet *const pSDXCReg =
		pgSDXCReg[pSDXCBootStatus->SDPort];

	NX_ASSERT(0 == ((U32)pdwBuffer & 3));

	 // wait while data busy or data transfer busy
	while (pSDXCReg->STATUS & (1 << 9 | 1 << 10));

	//----------------------------------------------------------------------
	// wait until 'Ready for data' is set and card is in transfer state.
	do {
		cmd.cmdidx	= SEND_STATUS;
		cmd.arg		= pSDXCBootStatus->rca;
		cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD |
				NX_SDXC_CMDFLAG_CHKRSPCRC |
				NX_SDXC_CMDFLAG_SHORTRSP;
		status = NX_SDMMC_SendCommand(pSDXCBootStatus, &cmd);
		if (NX_SDMMC_STATUS_NOERROR != status)
			goto End;
	} while (!((cmd.response[0] & (1 << 8)) &&
				(((cmd.response[0] >> 9) & 0xF) == 4)));

	NX_ASSERT(NX_SDXC_STATUS_FIFOEMPTY ==
			(pSDXCReg->STATUS & NX_SDXC_STATUS_FIFOEMPTY));
	NX_ASSERT(0 == (pSDXCReg->STATUS & NX_SDXC_STATUS_FSMBUSY));

	// Set byte count
	pSDXCReg->BYTCNT = numberOfSector * BLOCK_LENGTH;

	//----------------------------------------------------------------------
	// Send Command
	if (numberOfSector > 1) {
		cmd.cmdidx	= READ_MULTIPLE_BLOCK;
		cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD |
				NX_SDXC_CMDFLAG_WAITPRVDAT |
				NX_SDXC_CMDFLAG_CHKRSPCRC |
				NX_SDXC_CMDFLAG_SHORTRSP |
				NX_SDXC_CMDFLAG_BLOCK |
				NX_SDXC_CMDFLAG_RXDATA |
				NX_SDXC_CMDFLAG_SENDAUTOSTOP;
	} else {
		cmd.cmdidx	= READ_SINGLE_BLOCK;
		cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD |
				NX_SDXC_CMDFLAG_WAITPRVDAT |
				NX_SDXC_CMDFLAG_CHKRSPCRC |
				NX_SDXC_CMDFLAG_SHORTRSP |
				NX_SDXC_CMDFLAG_BLOCK |
				NX_SDXC_CMDFLAG_RXDATA;
	}
	cmd.arg		= (pSDXCBootStatus->bHighCapacity) ?
					SectorNum : SectorNum*BLOCK_LENGTH;

	status = NX_SDMMC_SendCommand(pSDXCBootStatus, &cmd);
	if (NX_SDMMC_STATUS_NOERROR != status)
		goto End;

	//----------------------------------------------------------------------
	// Read data
	if (CTRUE == NX_SDMMC_ReadSectorData(pSDXCBootStatus,
					numberOfSector, pdwBuffer)) {
		NX_ASSERT(NX_SDXC_STATUS_FIFOEMPTY ==
				(pSDXCReg->STATUS & NX_SDXC_STATUS_FIFOEMPTY));
		NX_ASSERT(0 == (pSDXCReg->STATUS & NX_SDXC_STATUS_FIFOFULL));
		NX_ASSERT(0 == (pSDXCReg->STATUS & NX_SDXC_STATUS_FIFOCOUNT));

		if (numberOfSector > 1) {
			// Wait until the Auto-stop command has been finished.
			while (0 == (pSDXCReg->RINTSTS & NX_SDXC_RINTSTS_ACD));

			NX_ASSERT(0 == (pSDXCReg->STATUS & NX_SDXC_STATUS_FSMBUSY));

#if defined(NX_DEBUG)
			// Get Auto-stop response and then check it.
			response = pSDXCReg->RESP1;
			if (response & 0xFDF98008) {
				dprintf("ERROR : %s - Auto Stop Response Failed = 0x%08X\r\n",
						__func__, response);
				//goto End;
			}
#endif
		}

		result = CTRUE;
	}

End:
	if (CFALSE == result) {
		cmd.cmdidx	= STOP_TRANSMISSION;
		cmd.arg		= 0;
		cmd.flag	= NX_SDXC_CMDFLAG_STARTCMD |
				NX_SDXC_CMDFLAG_CHKRSPCRC |
				NX_SDXC_CMDFLAG_SHORTRSP |
				NX_SDXC_CMDFLAG_STOPABORT;
		NX_SDMMC_SendCommandInternal(pSDXCBootStatus, &cmd);

		if (0 == (pSDXCReg->STATUS & NX_SDXC_STATUS_FIFOEMPTY)) {
			pSDXCReg->CTRL = NX_SDXC_CTRL_FIFORST;	// Reest the FIFO.
			// Wait until the FIFO reset is completed.
			while (pSDXCReg->CTRL & NX_SDXC_CTRL_FIFORST);
		}
	}

	return result;
}

//------------------------------------------------------------------------------
extern void Decrypt(U32 *SrcAddr, U32 *DestAddr, U32 Size, U8* key);
#if 0
static CBOOL SDMMCBOOT(SDXCBOOTSTATUS *pSDXCBootStatus,
		struct NX_SecondBootInfo *pTBI) // U32 option )
{
	CBOOL	result = CFALSE;
	register struct NX_SDMMC_RegisterSet * const pSDXCReg =
		pgSDXCReg[pSDXCBootStatus->SDPort];

	if (CTRUE == NX_SDMMC_Open(pSDXCBootStatus)) {
		dprintf("Cannot Detect SDMMC\r\n");
	}
	if (0 == (pSDXCReg->STATUS & NX_SDXC_STATUS_FIFOEMPTY)) {
		dprintf("FIFO Reset!!!\r\n");
		pSDXCReg->CTRL = NX_SDXC_CTRL_FIFORST;	// Reset the FIFO.
		// Wait until the FIFO reset is completed.
		while (pSDXCReg->CTRL & NX_SDXC_CTRL_FIFORST);
	}
	dprintf("Load from :0x%08X Sector\r\n",
			pSBI->DEVICEADDR / BLOCK_LENGTH);

#if 1
	result = NX_SDMMC_ReadSectors(pSDXCBootStatus,
			pSBI->DEVICEADDR / BLOCK_LENGTH,
			1, (U32 *)pTBI);
#else
	{
		U32 i;
		U8 *buff = 0x40100000;
		result = NX_SDMMC_ReadSectors(pSDXCBootStatus,
				1, 32, (U32 *)buff);

		for (i = 0; i < 16384;) {
			U32 j;
			dprintf("0x%08X ", &buff[i]);
			for (j = 0; j < 16; j++) {
				dprintf("%02X ", buff[i + j]);
			}
			DebugPutch(' ');
			for (j = 0; j < 16; j++) {
				if (buff[i + j] < 0x20 ||
					buff[i + j] > 0x80)
				{
					DebugPutch('.');
				} else {
					DebugPutch(buff[i + j]);
				}
			}
			DebugPutch('\r');
			DebugPutch('\n');
			i+= 16;
		}
	}
#endif
	if (result == CFALSE) {
		dprintf("cannot read boot header! SDMMC boot failure\r\n");
		return result;
	}

#ifdef SECURE_ON
	if (pReg_ClkPwr->SYSRSTCONFIG & 1 << 14)
		Decrypt((U32 *)pTBI, (U32 *)pTBI,
				sizeof(struct NX_SecondBootInfo));
#endif
	if (pTBI->SIGNATURE != HEADER_ID) {
		dprintf("0x%08X\r\n3rd boot Sinature is wrong!"
				" SDMMC boot failure\r\n",
				pTBI->SIGNATURE);
		return CFALSE;
	}

	//		pTBI->LOADADDR = 0x40c00000;
	//		pTBI->LOADSIZE = 0x00050000;
	//		pTBI->LAUNCHADDR = 0x40c00000;
	dprintf("Load Addr :0x%08X,  "
		"Load Size :0x%08X,  "
		"Launch Addr :0x%08X\r\n",
			pTBI->LOADADDR,
			pTBI->LOADSIZE,
			pTBI->LAUNCHADDR);

	result = NX_SDMMC_ReadSectors(pSDXCBootStatus,
			pSBI->DEVICEADDR / BLOCK_LENGTH + 1,
			(pTBI->LOADSIZE + BLOCK_LENGTH - 1) / BLOCK_LENGTH,
			(U32 *)pTBI->LOADADDR );

#ifdef SECURE_ON
	if (pReg_ClkPwr->SYSRSTCONFIG & 1 << 14)
		Decrypt((U32 *)pTBI->LOADADDR,
			(U32 *)pTBI->LOADADDR,
				pTBI->LOADSIZE);
#endif

	if (result == CFALSE) {
		dprintf("Image Read Failure\r\n");
	}

	return result;
}
#endif

/*
   sdmmc 0                   sdmmc 1                   sdmmc 2
   clk  a 29 1 gpio:0        clk  d 22 1 gpio:0        clk  c 18 2 gpio:1
   cmd  a 31 1 gpio:0        cmd  d 23 1 gpio:0        cmd  c 19 2 gpio:1
   dat0 b  1 1 gpio:0        dat0 d 24 1 gpio:0        dat0 c 20 2 gpio:1
   dat1 b  3 1 gpio:0        dat1 d 25 1 gpio:0        dat1 c 21 2 gpio:1
   dat2 b  5 1 gpio:0        dat2 d 26 1 gpio:0        dat2 c 22 2 gpio:1
   dat3 b  7 1 gpio:0        dat3 d 27 1 gpio:0        dat3 c 23 2 gpio:1
 */
void NX_SDPADSetALT(U32 PortNum)
{
	if (PortNum == 0) {
		struct NX_GPIO_RegisterSet *pGPA = pReg_GPIO[0];
		struct NX_GPIO_RegisterSet *pGPB = pReg_GPIO[1];
		register U32 *pGPIOARegA1 =
			(U32 *)&pGPA->GPIOxALTFN[1];	// a 29, a 31
		register U32 *pGPIOBRegA0 =
			(U32 *)&pGPB->GPIOxALTFN[0];	// b 1, 3, 5, 7
		*pGPIOARegA1 = (*pGPIOARegA1 & ~0xCC000000) | 0x44000000;	// all alt is 1
		*pGPIOBRegA0 = (*pGPIOBRegA0 & ~0x0000CCCC) | 0x00004444;	// all alt is 1
		pGPA->GPIOx_SLEW			&= ~(5 << 29);
		pGPA->GPIOx_SLEW_DISABLE_DEFAULT	|=   5 << 29;
		pGPA->GPIOx_DRV0			|=   5 << 29;
		pGPA->GPIOx_DRV0_DISABLE_DEFAULT	|=   5 << 29;
		pGPA->GPIOx_DRV1			|=   5 << 29;
		pGPA->GPIOx_DRV1_DISABLE_DEFAULT	|=   5 << 29;
		pGPA->GPIOx_PULLSEL			|=   5 << 29;
		pGPA->GPIOx_PULLSEL_DISABLE_DEFAULT	|=   5 << 29;
//		pGPA->GPIOx_PULLENB			|=   5 << 29;
		pGPA->GPIOx_PULLENB			|=   4 << 29;		// clk is not pull-up.
		pGPA->GPIOx_PULLENB_DISABLE_DEFAULT	|=   5 << 29;

		pGPB->GPIOx_SLEW			&= ~(0x55 << 1);
		pGPB->GPIOx_SLEW_DISABLE_DEFAULT	|=   0x55 << 1;
		pGPB->GPIOx_DRV0			|=   0x55 << 1;
		pGPB->GPIOx_DRV0_DISABLE_DEFAULT	|=   0x55 << 1;
		pGPB->GPIOx_DRV1			|=   0x55 << 1;
		pGPB->GPIOx_DRV1_DISABLE_DEFAULT	|=   0x55 << 1;
		pGPB->GPIOx_PULLSEL			|=   0x55 << 1;
		pGPB->GPIOx_PULLSEL_DISABLE_DEFAULT	|=   0x55 << 1;
		pGPB->GPIOx_PULLENB			|=   0x55 << 1;
		pGPB->GPIOx_PULLENB_DISABLE_DEFAULT	|=   0x55 << 1;
	} else if (PortNum == 1) {
		struct NX_GPIO_RegisterSet *pGPD = pReg_GPIO[3];
		register U32 *pGPIODRegA1 = (U32 *)&pGPD->GPIOxALTFN[1];
		// d 22, 23, 24, 25, 26, 27	// all alt is 1
		*pGPIODRegA1 = (*pGPIODRegA1 & ~0x00FFF000) | 0x00555000;
		pGPD->GPIOx_SLEW			&= ~(0x3F << 22);
		pGPD->GPIOx_SLEW_DISABLE_DEFAULT	|=   0x3F << 22;
		pGPD->GPIOx_DRV0			|=   0x3F << 22;
		pGPD->GPIOx_DRV0_DISABLE_DEFAULT	|=   0x3F << 22;
		pGPD->GPIOx_DRV1			|=   0x3F << 22;
		pGPD->GPIOx_DRV1_DISABLE_DEFAULT	|=   0x3F << 22;
		pGPD->GPIOx_PULLSEL			|=   0x3F << 22;
		pGPD->GPIOx_PULLSEL_DISABLE_DEFAULT	|=   0x3F << 22;
//		pGPD->GPIOx_PULLENB			|=   0x3F << 22;
		pGPD->GPIOx_PULLENB			|=   0x3E << 22;	// clk is not pull-up.
		pGPD->GPIOx_PULLENB_DISABLE_DEFAULT	|=   0x3F << 22;
	} else {
		struct NX_GPIO_RegisterSet *pGPC = pReg_GPIO[2];
		register U32 *pGPIOCRegA1 = (U32 *)&pGPC->GPIOxALTFN[1];
		// c 18, 19, 20, 21, 22, 23	// all alt is 2
		*pGPIOCRegA1 = (*pGPIOCRegA1 & ~0x0000FFF0) | 0x0000AAA0;
		pGPC->GPIOx_SLEW			&= ~(0x3F << 18);
		pGPC->GPIOx_SLEW_DISABLE_DEFAULT	|=   0x3F << 18;
		pGPC->GPIOx_DRV0			|=   0x3F << 18;
		pGPC->GPIOx_DRV0_DISABLE_DEFAULT	|=   0x3F << 18;
		pGPC->GPIOx_DRV1			|=   0x3F << 18;
		pGPC->GPIOx_DRV1_DISABLE_DEFAULT	|=   0x3F << 18;
		pGPC->GPIOx_PULLSEL			|=   0x3F << 18;
		pGPC->GPIOx_PULLSEL_DISABLE_DEFAULT	|=   0x3F << 18;
//		pGPC->GPIOx_PULLENB			|=   0x3F << 18;
		pGPC->GPIOx_PULLENB			|=   0x3E << 18;	// clk is not pull-up.
		pGPC->GPIOx_PULLENB_DISABLE_DEFAULT	|=   0x3F << 18;
	}
}

#if 1
void NX_SDPADSetGPIO(U32 PortNum)
{
	if (PortNum == 0) {
		struct NX_GPIO_RegisterSet *pGPA = pReg_GPIO[0];
		struct NX_GPIO_RegisterSet *pGPB = pReg_GPIO[1];
		register U32 *pGPIOARegA1 = (U32 *)&pGPA->GPIOxALTFN[1];
		register U32 *pGPIOBRegA0 = (U32 *)&pGPB->GPIOxALTFN[0];
		*pGPIOARegA1 &= ~0xCC000000;	// all gpio is 0
		*pGPIOBRegA0 &= ~0x0000CCCC;
		pGPA->GPIOx_SLEW			|=   5 << 29;
		pGPA->GPIOx_SLEW_DISABLE_DEFAULT	|=   5 << 29;
		pGPA->GPIOx_DRV0			&= ~(5 << 29);
		pGPA->GPIOx_DRV0_DISABLE_DEFAULT	|=   5 << 29;
		pGPA->GPIOx_DRV1			&= ~(5 << 29);
		pGPA->GPIOx_DRV1_DISABLE_DEFAULT	|=   5 << 29;
		pGPA->GPIOx_PULLSEL			&= ~(5 << 29);
		pGPA->GPIOx_PULLSEL_DISABLE_DEFAULT	&= ~(5 << 29);
		pGPA->GPIOx_PULLENB			&= ~(5 << 29);
		pGPA->GPIOx_PULLENB_DISABLE_DEFAULT	&= ~(5 << 29);

		pGPB->GPIOx_SLEW			|=   0x55 << 1;
		pGPB->GPIOx_SLEW_DISABLE_DEFAULT	|=   0x55 << 1;
		pGPB->GPIOx_DRV0			&= ~(0x55 << 1);
		pGPB->GPIOx_DRV0_DISABLE_DEFAULT	|=   0x55 << 1;
		pGPB->GPIOx_DRV1			&= ~(0x55 << 1);
		pGPB->GPIOx_DRV1_DISABLE_DEFAULT	|=   0x55 << 1;
		pGPB->GPIOx_PULLSEL			&= ~(0x55 << 1);
		pGPB->GPIOx_PULLSEL_DISABLE_DEFAULT	&= ~(0x55 << 1);
		pGPB->GPIOx_PULLENB			&= ~(0x55 << 1);
		pGPB->GPIOx_PULLENB_DISABLE_DEFAULT	&= ~(0x55 << 1);
	} else if (PortNum == 1) {
		struct NX_GPIO_RegisterSet *pGPD = pReg_GPIO[3];
		// d 22, 23, 24, 25, 26, 27
		register U32 *pGPIODRegA1 = (U32 *)&pGPD->GPIOxALTFN[1];
		*pGPIODRegA1 = (*pGPIODRegA1 & ~0x00FFF000);	// all gpio is 0
		pGPD->GPIOx_SLEW			|=   0x3F << 22;
		pGPD->GPIOx_SLEW_DISABLE_DEFAULT	|=   0x3F << 22;
		pGPD->GPIOx_DRV0			&= ~(0x3F << 22);
		pGPD->GPIOx_DRV0_DISABLE_DEFAULT	|=   0x3F << 22;
		pGPD->GPIOx_DRV1			&= ~(0x3F << 22);
		pGPD->GPIOx_DRV1_DISABLE_DEFAULT	|=   0x3F << 22;
		pGPD->GPIOx_PULLSEL			&= ~(0x3F << 22);
		pGPD->GPIOx_PULLSEL_DISABLE_DEFAULT	&= ~(0x3F << 22);
		pGPD->GPIOx_PULLENB			&= ~(0x3F << 22);
		pGPD->GPIOx_PULLENB_DISABLE_DEFAULT	&= ~(0x3F << 22);
	} else {
		struct NX_GPIO_RegisterSet *pGPC = pReg_GPIO[2];
		register U32 *pGPIOCRegA1 = (U32 *)&pGPC->GPIOxALTFN[1];
		// all gpio is 1
		*pGPIOCRegA1 = (*pGPIOCRegA1 & ~0x0000FFF0) | 0x00005550;
		pGPC->GPIOx_SLEW			|=   0x3F << 18;
		pGPC->GPIOx_SLEW_DISABLE_DEFAULT	|=   0x3F << 18;
		pGPC->GPIOx_DRV0			&= ~(0x3F << 18);
		pGPC->GPIOx_DRV0_DISABLE_DEFAULT	|=   0x3F << 18;
		pGPC->GPIOx_DRV1			&= ~(0x3F << 18);
		pGPC->GPIOx_DRV1_DISABLE_DEFAULT	|=   0x3F << 18;
		pGPC->GPIOx_PULLSEL			&= ~(0x3F << 18);
		pGPC->GPIOx_PULLSEL_DISABLE_DEFAULT	&= ~(0x3F << 18);
		pGPC->GPIOx_PULLENB			&= ~(0x3F << 18);
		pGPC->GPIOx_PULLENB_DISABLE_DEFAULT	&= ~(0x3F << 18);
	}
}
#endif

#if 0
//------------------------------------------------------------------------------
U32 iSDXCBOOT(struct NX_SecondBootInfo *pTBI)
{
	CBOOL result = CFALSE;
	SDXCBOOTSTATUS lSDXCBootStatus;
	SDXCBOOTSTATUS *pSDXCBootStatus = &lSDXCBootStatus;

#if defined(CHIPID_NXP4330)
	pSBI->DBI.SDMMCBI.PortNumber = 0;
#endif

//	pSBI->DBI.SDMMCBI.PortNumber = 1;
//	pSBI->DEVICEADDR = 128 * 1024;

	NX_ASSERT(pSBI->DBI.SDMMCBI.PortNumber < 3);
	pSDXCBootStatus->SDPort = pSBI->DBI.SDMMCBI.PortNumber;

	NX_SDPADSetALT(pSDXCBootStatus->SDPort);

	NX_SDMMC_Init(pSDXCBootStatus);

	//----------------------------------------------------------------------
	// Normal SD(eSD)/MMC ver 4.2 boot
	result = SDMMCBOOT(pSDXCBootStatus, pTBI);

	NX_SDMMC_Close(pSDXCBootStatus);
	NX_SDMMC_Terminate(pSDXCBootStatus);

//	NX_SDPADSetGPIO(pSDXCBootStatus->SDPort);

	return result;
}
#endif
/*----------------------------------------------------------------------------*/
SDXCBOOTSTATUS lcardstatus;
CBOOL init_mmc(unsigned int portnum)
{
	SDXCBOOTSTATUS *pcardstatus = &lcardstatus;

	pcardstatus->SDPort = portnum;

	NX_SDPADSetALT(portnum);

	NX_SDMMC_Init(pcardstatus);

	return CTRUE;
}

void deinit_mmc(unsigned int portnum)
{
	SDXCBOOTSTATUS *pcardstatus = &lcardstatus;
	portnum = portnum;

	NX_SDMMC_Close(pcardstatus);
	NX_SDMMC_Terminate(pcardstatus);

	NX_SDPADSetGPIO(pcardstatus->SDPort);
}
CBOOL load_mmc(unsigned int portnum,
		unsigned int startsector,
		unsigned int sectorcount,
		void *pmem,
		unsigned char *key,
		CBOOL dec)
{
	SDXCBOOTSTATUS *pcardstatus = &lcardstatus;
	CBOOL    result = CFALSE;
	volatile struct NX_SDMMC_RegisterSet * const pSDXCReg =
		pgSDXCReg[pcardstatus->SDPort];

	if (CTRUE != NX_SDMMC_Open(pcardstatus)) {
		printf("cannot detect sdmmc\r\n");
		return CFALSE;
	}

	if (0 == (pSDXCReg->STATUS & NX_SDXC_STATUS_FIFOEMPTY)) {
		dprintf("fifo reset!!!\r\n");
		pSDXCReg->CTRL = NX_SDXC_CTRL_FIFORST;
		while (pSDXCReg->CTRL & NX_SDXC_CTRL_FIFORST)
			;
	}

	dprintf("sd%d load image at %x sector, cnt:%x, target:%x\r\n",
			portnum, startsector, sectorcount, (unsigned long)pmem);

	result = NX_SDMMC_ReadSectors(pcardstatus,
			startsector,
			sectorcount,
			(unsigned int *)pmem);
	if (result == CFALSE) {
		printf("image read failure\r\n");
	}
//	if (dec)
//		Decrypt(pmem, pmem, sectorcount * BLOCK_LENGTH, key);

	return result;
}
enum {
	SECURE_DISPATCHER = 0,
	NON_SECURE_BL = 1,
	SECURE_OS = 2
};
char *bootmsg [] = {
	"secure dispatcher",
	"non-secure bootloader",
	"secure os"
};
extern void startup(void);
struct nx_bootheader * getmyheader(void)
{
	return (struct nx_bootheader *)((U32)startup - 
			sizeof(struct nx_bootheader));
}
int plat_load_image(struct NX_SecondBootInfo *pTBS,
		unsigned int slot, CBOOL dec)
{
	struct nx_bootheader *hdr = (struct nx_bootheader *)getmyheader();
	struct nx_bootheader *bh;
	unsigned char *key = hdr->tbbi.dbi[slot].sdmmcbi.cryptokey;

	struct nx_bootheader loaded_hdr;
	bh = &loaded_hdr;

	dprintf("load %s header\r\n", bootmsg[slot]);
	load_mmc(hdr->tbbi.dbi[0].sdmmcbi.portnumber,
		hdr->tbbi.dbi[slot].sdmmcbi.deviceaddr / BLOCK_LENGTH,
		2, (void*)bh, key, dec);

	if (bh->tbbi.signature != HEADER_ID) {
		printf("wrong boot Sinature(0x%08X)\r\n", bh->tbbi.signature);
		if (slot == SECURE_OS) {
			pTBS->LAUNCHADDR = 0;
			return 0;
		} else
			return 1;
	}

	dprintf("load %s image:%d\r\n", bootmsg[slot], bh->tbbi.loadsize);
	if (bh->tbbi.loadsize == 0)
		printf("load size is zero!! please check it out!\r\n");
	load_mmc(hdr->tbbi.dbi[slot].sdmmcbi.portnumber,
		hdr->tbbi.dbi[slot].sdmmcbi.deviceaddr / BLOCK_LENGTH + 2,
		(bh->tbbi.loadsize + BLOCK_LENGTH - 1) / BLOCK_LENGTH,
		(void*)bh->tbbi.loadaddr, key, dec);

	pTBS->LAUNCHADDR = bh->tbbi.startaddr;

#ifdef SECURE_ON
	return readimage((unsigned char *)bh, (unsigned char *)hdr,
			 (unsigned char *)bh->tbbi.loadaddr);
#else
	return 0;
#endif
}

int sdemmcboot(CBOOL isresume,
		struct NX_SecondBootInfo *pTDS,
		struct NX_SecondBootInfo *pTBS,
		struct NX_SecondBootInfo *pTBNS)
{
	int ret;
	struct nx_bootheader *hdr = (struct nx_bootheader *)getmyheader();

	init_mmc(hdr->tbbi.dbi[0].sdmmcbi.portnumber);

	ret = plat_load_image(pTDS, SECURE_DISPATCHER, CTRUE);
	dprintf("dispatcher load %d\r\n", ret);
	if (ret == 0 && !isresume) {
		ret = plat_load_image(pTBNS, NON_SECURE_BL, CFALSE);
		dprintf("uboot load %d\r\n", ret);
		if (ret == 0) {
			int tmp = plat_load_image(pTBS, SECURE_OS, CTRUE);
			dprintf("secure os %d\r\n", tmp);
		}
	}

	deinit_mmc(hdr->tbbi.dbi[0].sdmmcbi.portnumber);

	return !ret;
}
