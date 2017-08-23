/******************************************************************************
* 
* CAEN SpA - Front End Division
* Via Vetraia, 11 - 55049 - Viareggio ITALY
* +390594388398 - www.caen.it
*
***************************************************************************//**
PATCH of the CAENDigitizer for the DPP-CI in the x740 models
******************************************************************************/


#include <CAENDigitizer.h>
#include "_CAENDigitizer_DPPCIx740.h"



int _CAEN_DGTZ_SetNumEvAggregate_DPPCIx740(int handle, int NevAggr)
{
    int ret=0;
    uint32_t d32;

    if (NevAggr == 0) {
        CAEN_DGTZ_ReadRegister(handle, 0x8000, &d32);
        if (d32 & (1<<17)) {  // charge mode
            ret |= CAEN_DGTZ_WriteRegister(handle, 0x800C, 0xA);            // Memory paging 
            ret |= CAEN_DGTZ_WriteRegister(handle, 0x8020, 16);                // Custom size (num events per aggregate) 
        } else {
            ret |= CAEN_DGTZ_WriteRegister(handle, 0x800C, 0x3);
            ret |= CAEN_DGTZ_WriteRegister(handle, 0x8020, 1);
        }
    } else {
        ret |= CAEN_DGTZ_WriteRegister(handle, 0x8020, NevAggr);
    }

    return ret;
}


//int _CAEN_DGTZ_DecodeEvents_DPPCIx740(int handle, char *buffer, int bsize, int *numEvents, _CAEN_DGTZ_DPPCIx740_Event_t *Events); //original version
int _CAEN_DGTZ_DecodeEvents_DPPCIx740(int handle, char *buffer, int bsize, uint32_t *numEvents, _CAEN_DGTZ_DPPCIx740_Event_t *Events)//modified version
{
    int pnt = 0, i, j, ev=0, evp=0, aggrp=0, evpl=0,  grmask, nev, totnev=0, ngr=0;
    uint32_t *buff32 = (uint32_t *)buffer;

    while(pnt < (bsize/4)) {
        grmask = (char)(buff32[pnt+1] & 0xFF);  // group mask
        ngr = 0;
        for(i=0; i<8; i++) {
            if (grmask & (1<<i))
                ngr++;
        }
        nev = (((buff32[pnt] & 0x0FFFFFFF) - 4) / ngr) / 5; // Num events per group

        totnev += nev;
        pnt += 4;


        for(i=0; i<8; i++) {
            if (grmask & (1<<i)) {
                evpl = 0; // Restore Event Pointer after each Group loop
                for(ev=0; ev<nev; ev++) {
                    evp = aggrp+evpl;
                    Events[evp].GroupMask = grmask;
                    Events[evp].TimeTag = buff32[pnt];
                    pnt++;
                    for(j=0; j<4; j++) {
                        Events[evp].Charge[i*8+j*2] = buff32[pnt] & 0xFFFF;
                        Events[evp].Charge[i*8+j*2+1] = (buff32[pnt]>>16) & 0xFFFF;
                        pnt++;
                    }
                    evpl++;
                }
            }
        }

        aggrp += evpl; 

    }

    *numEvents = totnev;
    return 0;
}


