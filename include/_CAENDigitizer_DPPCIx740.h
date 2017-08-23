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


typedef struct 
{
  char GroupMask;
  uint32_t TimeTag;
  int16_t Charge[64];
} _CAEN_DGTZ_DPPCIx740_Event_t;

int _CAEN_DGTZ_SetNumEvAggregate_DPPCIx740(int handle, int NevAggr);
//int _CAEN_DGTZ_DecodeEvents_DPPCIx740(int handle, char *buffer, int bsize, int *numEvents, _CAEN_DGTZ_DPPCIx740_Event_t *Events); //original version
int _CAEN_DGTZ_DecodeEvents_DPPCIx740(int handle, char *buffer, int bsize, uint32_t *numEvents, _CAEN_DGTZ_DPPCIx740_Event_t *Events); //modified version
