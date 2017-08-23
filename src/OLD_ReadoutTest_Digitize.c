/******************************************************************************
* 
* CAEN SpA - Front End Division
* Via Vetraia, 11 - 55049 - Viareggio ITALY
* +390594388398 - www.caen.it
*
******************************************************************************/

#include <stdio.h>
#include <time.h>
#include <sys/timeb.h>

#include "CAENDigitizer.h"
#include "_CAENDigitizer_DPPCIx740.h"
#include "keyb.h"

//#include <process.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>


//#define popen  _popen    /* redefine POSIX 'deprecated' popen as _popen */
//#define pclose _pclose   /* redefine POSIX 'deprecated' pclose as _pclose */

#define CAEN_USE_DIGITIZERS
#define IGNORE_DPP_DEPRECATED

#define WAVEFORM_ACQMODE  0
#define CHARGE_ACQMODE    1

#define HISTO_NBIN    4096
#define PEDESTAL      100

#define BLT_AGGRNUM   255

#define ENABLE_TEST_PULSE 1

#define CHARGE_CUT 100

typedef struct 
{
	int RecordLength;   
	int PreTrigger;     
	int GroupMask;
	int ActiveChannel;
	int GateWidth;
	int GateOffset;
	int ChargeSensitivity;
	int FixedBaseline;
	int BaselineMode;
	int TriggerThreshold[8];
	int DCoffset[8];
	int AcqMode;
	int GateTrace;
	int NevAggr;
	uint64_t ChannelTriggerMask;
} ParamsType;


/* ============================================================================== */
/* Get time in milliseconds from the computer internal clock */
/* ============================================================================== */
long get_time()
{
    long time_ms;
#ifdef WIN32
    struct _timeb timebuffer;
    _ftime( &timebuffer );
    time_ms = (long)timebuffer.time * 1000 + (long)timebuffer.millitm;
#else
    struct timeval t1;
    struct timezone tz;
    gettimeofday(&t1, &tz);
    time_ms = (t1.tv_sec) * 1000 + t1.tv_usec / 1000;
#endif
    return time_ms;
}


/* ============================================================================== */
/* main */
/* ============================================================================== */
int main(int argc, char* argv[])
{
	int ret;
	int	handle;
    CAEN_DGTZ_BoardInfo_t BoardInfo;
	CAEN_DGTZ_EventInfo_t eventInfo;
	CAEN_DGTZ_UINT16_EVENT_t *Evt = NULL;
	_CAEN_DGTZ_DPPCIx740_Event_t *EvtDPPCI=NULL;
	char *buffer = NULL;
	int i, EvCnt=0, ch, gr, nb=0, PrevEvCnt=0, NumEvents;
	int Plot=0, ChangeMode=0, SaveList=0;
	int c = 0, NumCh, NumGr, SWtrg=0;
	char * evtptr = NULL;
	uint32_t size, bsize, DppCtrl;
	ParamsType Params;
	long CurrTime, PrevTime;
	uint32_t Histo[HISTO_NBIN];
	uint64_t ExtendedTimeTag = 0, ett = 0, PrevTimeTag = 0;
	uint32_t PrevTimeTag32 = 0; 
	FILE *gnuplot=NULL, *plotdata, *list=NULL, *params;

	/* set parameters */
	Params.RecordLength = 200;			// Number of samples in the acquisition window (waveform mode) 
	Params.PreTrigger = 100;  			// PreTrigger is in number of samples
	Params.ActiveChannel = 0;			// Channel used for the data analysis (plot, histograms, etc...)
	Params.BaselineMode = 2;			// Baseline used in the DPP-CI: 0=Fixed, 1=4samples, 2=16 samples, 3=64samples
	Params.FixedBaseline = 2100;		// fixed baseline (used when BaselineMode = 0)
	Params.GateOffset = -20;			// Position of the gate respect to the trigger (num of samples before)
	Params.GateWidth = 40;				// Gate Width in samples
	Params.TriggerThreshold[0] = 3400;     // Threshold for the self trigger
	Params.TriggerThreshold[1] = 3400;     // Threshold for the self trigger
	Params.TriggerThreshold[2] = 3400;     // Threshold for the self trigger
	Params.TriggerThreshold[3] = 3400;     // Threshold for the self trigger
	Params.TriggerThreshold[4] = 3400;     // Threshold for the self trigger
	Params.TriggerThreshold[5] = 3400;     // Threshold for the self trigger
	Params.TriggerThreshold[6] = 3400;     // Threshold for the self trigger
	Params.TriggerThreshold[7] = 3400;     // Threshold for the self trigger
	Params.DCoffset[0] = 8000;//0x8000; // DC offset adjust in DAC counts (0x8000 = mid scale)
	Params.DCoffset[1] = 8000;//0x8000; // DC offset adjust in DAC counts (0x8000 = mid scale)
	Params.DCoffset[2] = 8000;//0x8000; // DC offset adjust in DAC counts (0x8000 = mid scale)
	Params.DCoffset[3] = 8000;//0x8000; // DC offset adjust in DAC counts (0x8000 = mid scale)
	Params.DCoffset[4] = 8000;//0x8000; // DC offset adjust in DAC counts (0x8000 = mid scale)
	Params.DCoffset[5] = 8000;//0x8000; // DC offset adjust in DAC counts (0x8000 = mid scale)
	Params.DCoffset[6] = 8000;//0x8000; // DC offset adjust in DAC counts (0x8000 = mid scale)
	Params.DCoffset[7] = 8000;//0x8000; // DC offset adjust in DAC counts (0x8000 = mid scale)
	Params.GateTrace = 1;               // Enable Gate trace in waveform mode
	Params.ChargeSensitivity = 2;       // Charge sesnitivity (0=max, 7=min)
	Params.NevAggr = 1;                 // Number of event per aggregate (buffer). 0=automatic
	Params.AcqMode = CHARGE_ACQMODE;  // Acquisition Mode (WAVEFORM_ACQMODE or CHARGE_ACQMODE)

	params = fopen("config.txt", "r");
	if (params != NULL) {
		while (!feof(params)) {
			char str[100];
			fscanf(params, "%s", str);
			if (strcmp(str, "TriggerThreshold") == 0) {
				int ch;
				fscanf(params, "%d", &ch);
				fscanf(params, "%d", &Params.TriggerThreshold[ch]);
			}
			if (strcmp(str, "RecordLength") == 0) 
				fscanf(params, "%d", &Params.RecordLength);
			if (strcmp(str, "PreTrigger") == 0)
				fscanf(params, "%d", &Params.PreTrigger);
			if (strcmp(str, "ActiveChannel") == 0) 
				fscanf(params, "%d", &Params.ActiveChannel);
			if (strcmp(str, "BaselineMode") == 0) 
				fscanf(params, "%d", &Params.BaselineMode);
			if (strcmp(str, "FixedBaseline") == 0) 
				fscanf(params, "%d", &Params.FixedBaseline);
			if (strcmp(str, "GateOffset") == 0) 
				fscanf(params, "%d", &Params.GateOffset);
			if (strcmp(str, "GateWidth") == 0) 
				fscanf(params, "%d", &Params.GateWidth);
			if (strcmp(str, "DCoffset") == 0) {
				int ch;
				fscanf(params, "%d", &ch);
				fscanf(params, "%d", &Params.DCoffset[ch]);
			}
			if (strcmp(str, "ChargeSensitivity") == 0) 
				fscanf(params, "%d", &Params.ChargeSensitivity);
			if (strcmp(str, "NevAggr") == 0) 
				fscanf(params, "%d", &Params.NevAggr);
			if (strcmp(str, "GroupMask") == 0) 
				fscanf(params, "%x", &Params.GroupMask);
			if (strcmp(str, "SaveList") == 0) 
				fscanf(params, "%d", &SaveList);
			if (strcmp(str, "ChannelTriggerMask") == 0) 
				fscanf(params, "%llx", &Params.ChannelTriggerMask);	
		}
		fclose(params);
	}

	/* Connect to the digitizer */
	ret = CAEN_DGTZ_OpenDigitizer(CAEN_DGTZ_USB, 0, 0, 0, &handle); 
    if(ret != CAEN_DGTZ_Success) {
        printf("Can't open digitizer\n");
        goto QuitProgram;
    }

    /* Get board information */
	
    ret = CAEN_DGTZ_GetInfo(handle, &BoardInfo);
    printf("\nConnected to CAEN Digitizer Model %s\n", BoardInfo.ModelName);
    printf("ROC FPGA Release is %s\n", BoardInfo.ROC_FirmwareRel);
    printf("AMC FPGA Release is %s\n\n", BoardInfo.AMC_FirmwareRel);
	NumCh = BoardInfo.Channels;
	if ((ch = Params.ActiveChannel) >= NumCh)
		ch = NumCh-1;

	ch = Params.ActiveChannel;
	gr = Params.ActiveChannel/8;
	NumCh=32;
	NumGr=NumCh/8;


	/* Write settings into the digitizer */
	ret |= CAEN_DGTZ_Reset(handle);                                                  /* Reset Digitizer */
	ret |= CAEN_DGTZ_SetGroupEnableMask(handle,Params.GroupMask);                    /* Enable groups */
	for(i=0; i<NumGr; i++)
	{
		ret |= CAEN_DGTZ_SetGroupTriggerThreshold(handle,i,Params.TriggerThreshold[i]);     /* Set selfTrigger threshold */
		printf("%d\t",Params.TriggerThreshold[i]); //MOD
	}
	ret |= CAEN_DGTZ_SetGroupSelfTrigger(handle,CAEN_DGTZ_TRGMODE_ACQ_ONLY,0xFF);    /* Enable self trigger for the acquisition */
	
	for(i=0; i<NumGr; i++)
		ret |= CAEN_DGTZ_SetChannelGroupMask(handle, i, (Params.ChannelTriggerMask>>(i*8)) & 0xFF);    /* Use channel ch for triggering */

	ret |= CAEN_DGTZ_SetSWTriggerMode(handle,CAEN_DGTZ_TRGMODE_ACQ_ONLY);            /* Set the behaviour when a SW tirgger arrives */
	ret |= CAEN_DGTZ_WriteRegister(handle, 0x8008, 1<<6);							 /* trigger edge: 1 = negative, 0 = positive */
	CAEN_DGTZ_PulsePolarity_t pol,pol1;
	CAEN_DGTZ_GetChannelPulsePolarity (handle , 0 , &pol);
	printf("\nregister before %d\n",pol);
	ret |= CAEN_DGTZ_SetChannelPulsePolarity (handle, 0 ,1);       /*set pulse polarity = positive */
	CAEN_DGTZ_GetChannelPulsePolarity (handle , 0 , &pol1);
	printf("\nregister after %d\n",pol1);

        //ret |= CAEN_DGTZ_WriteRegister(handle, 0x8004, 0);							
	//uint32_t RegNum;
// 	/*ret |=*/ CAEN_DGTZ_ReadRegister(handle,0x8004,&RegNum);
// 	printf("\nregister %zu\n",RegNum);
	//ret |= CAEN_DGTZ_SetChannelPulsePolarity (handle,0,CAEN_DGTZ_PulsePolarityPositive);       /*set pulse polarity = positive */
	
	ret |= CAEN_DGTZ_SetMaxNumAggregatesBLT(handle, BLT_AGGRNUM);                    /* Set the max number of events/aggregates to transfer in a sigle readout */
    ret |= CAEN_DGTZ_SetAcquisitionMode(handle,CAEN_DGTZ_SW_CONTROLLED);             /* Set the start/stop acquisition control */

	DppCtrl = ((Params.BaselineMode & 0x3) << 4) | (Params.ChargeSensitivity & 0xF) | ((Params.GateTrace & 1) << 8);
	ret |= CAEN_DGTZ_WriteRegister(handle, 0x8040, DppCtrl);
	ret |= CAEN_DGTZ_WriteRegister(handle, 0x803C, Params.PreTrigger);               /* Set Pre Trigger (in samples) */
	ret |= CAEN_DGTZ_WriteRegister(handle, 0x8030, Params.GateWidth);                /* Set Gate Width (in samples) */
	ret |= CAEN_DGTZ_WriteRegister(handle, 0x8034, Params.GateOffset);               /* Set Gate Offset (in samples) */
	ret |= CAEN_DGTZ_WriteRegister(handle, 0x8038, Params.FixedBaseline);            /* Set Baseline (used in fixed baseline mode only) */

	/* Set DC offset */
	for (i=0; i<4; i++)
		ret |= CAEN_DGTZ_SetGroupDCOffset(handle, i, Params.DCoffset[i]);

	if (Params.AcqMode == WAVEFORM_ACQMODE) {
		ret |= CAEN_DGTZ_SetRecordLength(handle,Params.RecordLength);           /* Set the waveform lenght (in samples) */
	} else {
		_CAEN_DGTZ_SetNumEvAggregate_DPPCIx740(handle, Params.NevAggr);         /* Set number of event per memory buffer */
		ret |= CAEN_DGTZ_WriteRegister(handle, 0x8004, 0x00020000);             /* enable Charge mode */
	}

	/* enable test pulses on TRGOUT/GPO */
	if (ENABLE_TEST_PULSE) {
		uint32_t d32;
		ret |= CAEN_DGTZ_ReadRegister(handle, 0x811C, &d32);  
		ret |= CAEN_DGTZ_WriteRegister(handle, 0x811C, d32 | (1<<15));         
		ret |= CAEN_DGTZ_WriteRegister(handle, 0x8168, 2);         
	}

	if(ret != CAEN_DGTZ_Success) {
        printf("Errors during Digitizer Configuration.\n");
        goto QuitProgram;
    }

	/* Malloc Readout Buffer.
    NOTE: The mallocs must be done AFTER digitizer's configuration! */
    ret = CAEN_DGTZ_MallocReadoutBuffer(handle,&buffer,&size);
	// Allocate memory for the events */
	EvtDPPCI = (_CAEN_DGTZ_DPPCIx740_Event_t *)malloc(BLT_AGGRNUM * 1024 * sizeof(_CAEN_DGTZ_DPPCIx740_Event_t));

	/* open gnuplot in a pipe and the data file*/
	gnuplot = popen("gnuplot", "w");
	fprintf(gnuplot, "set yrange [0:4096]\n");
	if (gnuplot==NULL) {
        printf("Can't open gnuplot\n");
        goto QuitProgram;
    }

	// clear histogram
	memset(Histo, 0, HISTO_NBIN * sizeof(uint32_t));

    /* Start Acquisition */
	printf("\nPress a key to start the acquisition\n");
	getch();
    ret = CAEN_DGTZ_SWStartAcquisition(handle);
	printf("Acquisition started\n");

    // Acquisition loop (infinite)
    // Exit only by pressing 'q'
	CurrTime = get_time();
	PrevTime = CurrTime;
	c = 0;
	while(1) {
		if (!kbhit()) 
            goto go;
        
        c = getch(); 
		if (c=='q')	break;
		if (c=='t') 
            SWtrg ^= 1;
		if (c=='p') Plot = 1;
		if (c=='l') SaveList = 1;
		if (c=='r')	memset(Histo, 0, HISTO_NBIN * sizeof(uint32_t));
		if (c==' ') ChangeMode = 1;
		if (c=='c') {
			printf("Channel = ");
			scanf("%d", &ch);
            // HACK decommentare per resettare istogramma
			memset(Histo, 0, HISTO_NBIN * sizeof(uint32_t));
		}

go:
		CurrTime = get_time();
		if ((CurrTime-PrevTime)>1000) {
			printf("Tot Num Events = %d\n", EvCnt);
			printf("Trigger Rate = %.2f KHz\n", (float)(EvCnt-PrevEvCnt) / (CurrTime-PrevTime));
			printf("Readout Rate = %.2f MB/s\n\n", (float)nb / 1024 / (CurrTime-PrevTime));
			//printf("b=%d\n", bsize);
			nb = 0;
			PrevTime = CurrTime;
			PrevEvCnt = EvCnt;
			Plot=1;
		}

		if (SWtrg)
			CAEN_DGTZ_SendSWtrigger(handle); /* Send a SW Trigger */

		/* Read a block of data from the digitizer */
		ret = CAEN_DGTZ_ReadData(handle, CAEN_DGTZ_SLAVE_TERMINATED_READOUT_MBLT, buffer, &bsize); /* Read the buffer from the digitizer */
		if (ret) {
			printf("Readout Error\n");
			goto QuitProgram;
		}

		if (bsize == 0) 
			continue;

		nb += bsize;
		if (Params.AcqMode == WAVEFORM_ACQMODE) {

			CAEN_DGTZ_GetNumEvents(handle, buffer, bsize, &NumEvents);
			for (i=0; i<NumEvents; i++) {
				/* Get the Infos and pointer to the event */
				CAEN_DGTZ_GetEventInfo(handle, buffer, bsize, i, &eventInfo, &evtptr);
				/* Decode the event to get the data */
				CAEN_DGTZ_DecodeEvent(handle, evtptr, &Evt);

				/* Save Waveform and Plot it using gnuplot */
				if (Plot) {
					plotdata = fopen("PlotData.txt", "w");
					for(i=0; i<(int)Evt->ChSize[ch]; i++) {
						if (Params.GateTrace) {
							fprintf(plotdata, "%d ", Evt->DataChannel[ch][i] & 0xFFE);  // samples
							fprintf(plotdata, "%d\n", 2000 + 200 * (Evt->DataChannel[ch][i] & 0x001));  // gate
						} else {
							fprintf(plotdata, "%d ", Evt->DataChannel[ch][i]);  // samples
						}
					}
					fclose(plotdata);
					if (Params.GateTrace) 
						fprintf(gnuplot, "plot 'PlotData.txt' using 1 with steps , 'PlotData.txt' using 2  with steps \n");
					else
						fprintf(gnuplot, "plot 'PlotData.txt' with steps \n");						
					fflush(gnuplot);
					Plot=0;
				}
				CAEN_DGTZ_FreeEvent(handle, &Evt);
			}
		} else {
			ret = _CAEN_DGTZ_DecodeEvents_DPPCIx740(handle, buffer, bsize, &NumEvents, EvtDPPCI);
			for(i=0; i<NumEvents; i++) {
                int Charge;
                if ( (EvtDPPCI[i].GroupMask & (1<<(ch/8))) && EvtDPPCI[i].Charge[ch]<HISTO_NBIN && EvtDPPCI[i].Charge[ch]>0) {
				   Charge = PEDESTAL + EvtDPPCI[i].Charge[ch];
                   Histo[Charge]++;
                }
				
				if (EvtDPPCI[i].TimeTag < PrevTimeTag32)
					ett++;
				ExtendedTimeTag = (ett << 32) + (uint64_t)EvtDPPCI[i].TimeTag;

				if (SaveList) {
					int j;
					if (list == NULL) {
						list = fopen("ListFile.txt", "w");
						printf("Saving 'ListFile.txt'\n", ch);
					}
					fprintf(list, "%16llu %16llu ", ExtendedTimeTag, ExtendedTimeTag-PrevTimeTag);
					for(j=0; j<64; j++) {
						if (EvtDPPCI[i].GroupMask & (1<<(j/8)))
                            if (EvtDPPCI[i].Charge[j] >= CHARGE_CUT)
							  fprintf(list, "%8d ", EvtDPPCI[i].Charge[j]);
                            else
                              fprintf(list, "%8d ", 0);
					}
					fprintf(list, "\n");
				}
				PrevTimeTag = ExtendedTimeTag;
				PrevTimeTag32 = EvtDPPCI[i].TimeTag;
			}
			if (Plot) {
				plotdata = fopen("PlotData.txt", "w");
				for(i=0; i<HISTO_NBIN; i++)
					fprintf(plotdata, "%d\n", Histo[i]);
				fclose(plotdata);
				fprintf(gnuplot, "plot 'PlotData.txt' with steps \n");
				fflush(gnuplot);
				Plot=0;
			}
		}
		EvCnt += NumEvents;

		if (ChangeMode) {
			ChangeMode = 0;
			CAEN_DGTZ_SWStopAcquisition(handle);
			if (Params.AcqMode == CHARGE_ACQMODE) {
				Params.AcqMode = WAVEFORM_ACQMODE;
				CAEN_DGTZ_WriteRegister(handle, 0x8008, 0x00020000);             // disable Charge mode
				CAEN_DGTZ_SetRecordLength(handle,Params.RecordLength);           // Set the waveform lenght (in samples)
				printf("Switched to Waveform mode\n");
			} else {
				Params.AcqMode = CHARGE_ACQMODE;
				_CAEN_DGTZ_SetNumEvAggregate_DPPCIx740(handle, Params.NevAggr);
				CAEN_DGTZ_WriteRegister(handle, 0x8004, 0x00020000);             // enable Charge mode
				printf("Switched to Charge mode\n");
			}
			Sleep(100);
			CAEN_DGTZ_SWStartAcquisition(handle);
		}
		c = 0;
    } // end of readout loop

QuitProgram:
    // Free the buffers and close the digitizer
	ret = CAEN_DGTZ_FreeReadoutBuffer(&buffer);
    ret = CAEN_DGTZ_CloseDigitizer(handle);
	if (EvtDPPCI != NULL)
		free(EvtDPPCI);
	if (gnuplot != NULL)
		fclose(gnuplot);
	if (list != NULL)
		fclose(list);
	return 0;
}

