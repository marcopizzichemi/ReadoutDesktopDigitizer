/******************************************************************************
 * 
 * CAEN SpA - Front End Division
 * Via Vetraia, 11 - 55049 - Viareggio ITALY
 * +390594388398 - www.caen.it
 *
 ******************************************************************************/
#include <iostream>     // std::cout, std::end
#include <bitset>       // std::bitset
#include <vector>       // std::vector
#include <numeric>       // std::accumulate
#include <cmath>
#include <stdio.h>
#include <time.h>
#include <sys/timeb.h>

#include "CAENDigitizer.h"
#include "_CAENDigitizer_DPPCIx740.h"
#include "keyb.h"
#include "ReadoutTest_Digitizer.h"

//#include <process.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>


#include <string>
#include <sstream>

//ROOT includes
#include "TROOT.h"
#include "TTree.h"
#include "TFile.h"


//#define popen  _popen    /* redefine POSIX 'deprecated' popen as _popen */
//#define pclose _pclose   /* redefine POSIX 'deprecated' pclose as _pclose */

#define CAEN_USE_DIGITIZERS
#define IGNORE_DPP_DEPRECATED

#define WAVEFORM_ACQMODE  0
#define CHARGE_ACQMODE    1

#define HISTO_NBIN    32768
#define PEDESTAL      0

#define BLT_AGGRNUM   255

#define ENABLE_TEST_PULSE 1

#define CHARGE_CUT 0

#define NUM_CH 32
#define NUM_GR 4

#define SIGMA_THR 5
#define CALIBRATION_LIMIT 1000

//it's about 0.5GB each 10^6 events, so 10^5 makes files of about 50MB
#define ROOTFILELENGTH 100000

/* ============================================================================== */
/* Get time of the day 
/* ============================================================================== */
void TimeOfDay(char *actual_time)
{
  struct tm *timeinfo;
  time_t currentTime;
  time(&currentTime);
  timeinfo = localtime(&currentTime);
  strftime (actual_time,20,"%Y_%d_%m_%H_%M_%S",timeinfo);
}



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
  
  //------------------ //
  // Declare variables //
  //------------------ //
  int ret;
  int handle;
  CAEN_DGTZ_BoardInfo_t BoardInfo;
  CAEN_DGTZ_EventInfo_t eventInfo;
  
  CAEN_DGTZ_UINT16_EVENT_t *Evt = NULL; //original declaration
  void *Evt_VoidType = NULL; //modified declaration
  
  _CAEN_DGTZ_DPPCIx740_Event_t *EvtDPPCI=NULL;
  char *buffer = NULL;
  int i, EvCnt=0, ch, gr, nb=0, PrevEvCnt=0;
  uint32_t NumEvents;
  int Plot=0, ChangeMode=0, SaveList=0;
  int c = 0, NumCh, NumGr, SWtrg=0;
  char * evtptr = NULL;
  uint32_t size, bsize;
  ParamsType Params;
  long CurrTime, PrevTime;
  uint32_t Histo[NUM_CH][HISTO_NBIN];
  ULong64_t ExtendedTimeTag = 0, ett = 0, PrevTimeTag = 0;
  uint32_t PrevTimeTag32 = 0; 
  FILE *gnuplot=NULL, *plotdata, *list=NULL, *params;
  //calibration variable: 
  uint32_t CalibrationMask = 0;
  
  
  //----------------//
  //   Parameters   //
  //----------------//
  SetDefaultParameters(Params); // Set default parameters 
  ReadConfigFile(params, Params, SaveList); // Read config.txt file 
  if(Params.PrintParameters) PrintParameters(Params); // Print parameters
  
  //----------------//
  // Inizialization //
  //----------------//
  if(ConnectToDigitizer(handle,ret)) // Connect to digitizer...
  {
    GetBoardInfo(handle,BoardInfo,ret); // Get board information
    WriteSettingsToDigitizer(handle,ret,Params); // Write setting to digitizer
    if(ret != CAEN_DGTZ_Success) 
    {
      std::cout << "Errors during Digitizer Configuration." << std::endl;
      return -1;
    }
  }
  else
    return -1;
  
  //-------------//
  // Mallocs.... //
  //-------------// 
  /* Malloc Readout Buffer.
  NOTE: The mallocs must be done AFTER digitizer's configuration! */                                        
  ret = CAEN_DGTZ_MallocReadoutBuffer(handle,&buffer,&size);
  // Allocate memory for the events */
  EvtDPPCI = (_CAEN_DGTZ_DPPCIx740_Event_t *)malloc(BLT_AGGRNUM * 1024 * sizeof(_CAEN_DGTZ_DPPCIx740_Event_t));
  
  
  //----------//
  // Pipe.... //
  //----------// 
  /* open gnuplot in a pipe and the data file*/
  gnuplot = popen("gnuplot", "w");
  //fprintf(gnuplot, "set yrange [0:4096]\n");
  if (gnuplot==NULL) 
  {
    std::cout << "Can't open gnuplot" << std::endl;
    return -1;
  }
  
  // clear histograms
  for(int j=0; j<NUM_CH; j++) 
  {
     memset(Histo[j], 0, HISTO_NBIN * sizeof(uint32_t));
  }
  
  // get time of day and create the listmode file name
  char actual_time[20];
  TimeOfDay(actual_time);
  std::string listFileName = "ListFile_"; 
  listFileName += actual_time;
  listFileName += ".txt";
  //std::cout << "listFileName " << listFileName << std::endl;
  

  //----------------------------------------//
  // Create Root TTree Folder and variables //
  //----------------------------------------//
  
  //first, create a new directory
  std::string dirName = "./Run_";
  dirName += actual_time;
  std::string MakeFolder;
  MakeFolder = "mkdir " + dirName;
  system(MakeFolder.c_str());
 
  
//   std::string fileRoot = dirName + "/TTree_"; 
//   fileRoot += actual_time;
//   fileRoot += ".root";
  
  //variables (ExtendedTimeTag has already been created)
  ULong64_t DeltaTimeTag;
  Short_t charge[32];
  //the ttree variable
  TTree *t1 ;
  //strings for the names 
  std::stringstream snames;
  std::stringstream stypes;
  std::string names;
  std::string types;
  int filePart = 0;
  long long int runNumber = 0;
  long long int listNum = 0;
  int NumOfRootFile = 0;
  
  //-------------------//
  // Start Acquisition //
  //-------------------//
  printf("\nPress a key to start the acquisition\n");
  getch();
  ret = CAEN_DGTZ_SWStartAcquisition(handle);
  printf("Acquisition started\n");  
  // Acquisition loop (infinite)
  // Exit only by pressing 'q'
  CurrTime = get_time();
  PrevTime = CurrTime;
  c = 0;
  ch = 0;
  
  
  //-----------------//
  // Calibration run //
  //-----------------// 
  std::cout << "Calibrating DC offsets" << std::endl;
  int DCValues[32];
  int DCOffsetValue[32];
  long int HowManyCyclesINeeded = 0;
  //find the average values of all the 32 channels. It will be used to fine tune the DC offsets of all active ch
  
  //for (int cycle = 0 ; cycle < 2 ; cycle++)
  
  // Next calibration will run until the 32bit number CalibrationMask has at least 1 bit set to 0
  // CalibrationMask starts with a sequence of 32 zeroes, then everytime a pulse is "good" (i.e. 
  // when the sigma of the samples of the pulse is lower than SIGMA_THR samples, the corresponding bitset
  // is raised to 1. When all the 32 channels had "good" pulses, CalibrationMask will be a series of 32 ones,
  // therefore bitwise not operator ~ will give 0, i.e. false, as the while condition, hence the cycle ends
  std::cout << "Running on Software Trigger..." << std::endl;
  while(~CalibrationMask)
  {
    
    //send a SWTrigger
    CAEN_DGTZ_SendSWtrigger(handle); /* Send a SW Trigger */
    /* Read a block of data from the digitizer */
    ret = CAEN_DGTZ_ReadData(handle, CAEN_DGTZ_SLAVE_TERMINATED_READOUT_MBLT, buffer, &bsize); /* Read the buffer from the digitizer */   
    if (ret) 
    {
      printf("Readout Error\n");
      break;
    }
    if (bsize != 0) //perform the following only if the digitizer has something in the readout buffer
    {
      if (Params.AcqMode == WAVEFORM_ACQMODE) //wavefore mode, but program always should start in waveform mode!!! (otherwise malloc is too little)
      {
	CAEN_DGTZ_GetNumEvents(handle, buffer, bsize, &NumEvents);
	//std::cout << "NumEvents = " << NumEvents << std::endl; 
	for (i=0; i<NumEvents; i++) 
	{
	  HowManyCyclesINeeded++;
	  /* Get the Infos and pointer to the event */
	  CAEN_DGTZ_GetEventInfo(handle, buffer, bsize, i, &eventInfo, &evtptr);
	  /* Decode the event to get the data */
	  CAEN_DGTZ_DecodeEvent(handle, evtptr, &Evt_VoidType);	  
	  Evt = (CAEN_DGTZ_UINT16_EVENT_t *) (Evt_VoidType);
	  for(int channel = 0 ; channel < 32 ; channel++)
	  {
	    if( !(CalibrationMask & (1<<channel) ) )//do it only if a pulse with sigma < 5 was not already found
	    {
	      //std::cout << "ciao " << channel << std::endl;
	      long long int ChSum = 0;
	      std::vector<int> InputVector;
	      for(int k=0; k<(int)Evt->ChSize[channel]; k++) 
	      {
		//ChSum += Evt->DataChannel[channel][k];
		InputVector.push_back(Evt->DataChannel[channel][k]);
	      }
	      double sum = std::accumulate(InputVector.begin(), InputVector.end(), 0.0);
	      double mean = sum / InputVector.size();
	      double sq_sum = std::inner_product(InputVector.begin(), InputVector.end(), InputVector.begin(), 0.0);
	      double stdev = std::sqrt(sq_sum / InputVector.size() - mean * mean);
	      
	      if (stdev < SIGMA_THR) //if the sigma is low enough, accept the data to calculate the DC offests
		CalibrationMask |= (1<<channel);
	      
	      //std:: cout << std::bitset<32>(CalibrationMask) << std::endl;
	      //std::cout << "InputVector " << i << " " << channel << " Mean " << mean << " stdev "  << stdev << std::endl;
	      //int ChAverage = ChSum / Evt->ChSize[channel];
	      DCValues[channel] = (int) mean;
	      //printf("%d \n",ChAverage);
	    }
	  }
	  CAEN_DGTZ_FreeEvent(handle, &Evt_VoidType);
	}
      } 
    }
    std::cout << "\r" << "Cycle num = " << HowManyCyclesINeeded << " Channel map " << std::bitset<32>(CalibrationMask) << std::flush;
    if (HowManyCyclesINeeded > CALIBRATION_LIMIT)
    {
      std::cout << "\nWARNING: Impossible to complete calibration correctly (too many cycles)!" << std::endl;
      break;
    }
  }
  std::cout << "\n" << HowManyCyclesINeeded << " event(s) needed to complete calibration" << std::endl;
  //Compute DC offsets 
  //run on the groups, find the average DC
  int AvDC[4];
  for (int gr = 0 ; gr < 4 ; gr++)
  {
    int SumDC = 0;
    int ChannelsOn = 0;
    uint32_t ActiveTriggerMask;
    ret |= CAEN_DGTZ_GetChannelGroupMask(handle,gr,&ActiveTriggerMask);                   
    for(i=0 ; i < 8 ; i++) //sum all the DC values for that group
    {
      //perform sum only if the channel is actively taking part to the triggering!
      SumDC += ((ActiveTriggerMask >> i) & 1) * DCValues[gr*8+i];
      ChannelsOn += ((ActiveTriggerMask >> i) & 1);
    }
    if(ChannelsOn) 
      AvDC[gr] = SumDC / ChannelsOn; //find the average DC value of the group
    else 
      AvDC[gr] = 0;
    //std::cout << "AvDC[gr] " << AvDC[gr] << std::endl;
    for(i=0 ; i < 8 ; i++) //find the offsets needed 
    {
      DCOffsetValue[gr*8+i] = (AvDC[gr] - DCValues[gr*8+i]) * ((ActiveTriggerMask >> i) & 1);
      //std::cout << "DCOffsetValue[" << gr*8+i << "] " << DCOffsetValue[gr*8+i] << std::endl;
    }
  }
  uint32_t ChannelDCOffsetMask_1[4],ChannelDCOffsetMask_2[4];
  //Write the masks
  for(i = 0 ; i < 4 ; i++)
  {
//     std::cout << DCOffsetValue[i*8] << "\t" << std::bitset<8>(DCOffsetValue[i*8]) << std::endl;
//     std::cout << DCOffsetValue[i*8+1]<< "\t" << std::bitset<8>(DCOffsetValue[i*8+1]) << std::endl;
//     std::cout << DCOffsetValue[i*8+2]<< "\t" << std::bitset<8>(DCOffsetValue[i*8+2]) << std::endl;
//     std::cout << DCOffsetValue[i*8+3]<< "\t" << std::bitset<8>(DCOffsetValue[i*8+3]) << std::endl;
//     std::cout << DCOffsetValue[i*8+4]<< "\t" << std::bitset<8>(DCOffsetValue[i*8+4]) << std::endl;
//     std::cout << DCOffsetValue[i*8+5]<< "\t" << std::bitset<8>(DCOffsetValue[i*8+5]) << std::endl;
//     std::cout << DCOffsetValue[i*8+6]<< "\t" << std::bitset<8>(DCOffsetValue[i*8+6]) << std::endl;
//     std::cout << DCOffsetValue[i*8+7]<< "\t" << std::bitset<8>(DCOffsetValue[i*8+7]) << std::endl;
    ChannelDCOffsetMask_1[i] =  ((DCOffsetValue[i*8]) & 0xFF) | (( DCOffsetValue[i*8+1] << 8)& 0xFF00) | (( DCOffsetValue[i*8+2] << 16) & 0xFF0000 ) | (( DCOffsetValue[i*8+3] << 24) & 0xFF000000);
    ChannelDCOffsetMask_2[i] =  ((DCOffsetValue[i*8+4]) & 0xFF) | (( DCOffsetValue[i*8+5] << 8)& 0xFF00) | (( DCOffsetValue[i*8+6] << 16) & 0xFF0000 ) | (( DCOffsetValue[i*8+7] << 24) & 0xFF000000);
//     std::cout << std::bitset<32>(ChannelDCOffsetMask_1[i]) << std::endl;
//     std::cout << std::bitset<32>(ChannelDCOffsetMask_2[i]) << std::endl;
  }
  //set the offsets by writing the registers
//   ret |= CAEN_DGTZ_WriteRegister(handle, 0x10C0,ChannelDCOffsetMask_1[0] );
//   ret |= CAEN_DGTZ_WriteRegister(handle, 0x10C4,ChannelDCOffsetMask_2[0] );
//   ret |= CAEN_DGTZ_WriteRegister(handle, 0x11C0,ChannelDCOffsetMask_1[1] );
//   ret |= CAEN_DGTZ_WriteRegister(handle, 0x11C4,ChannelDCOffsetMask_2[1] );
//   ret |= CAEN_DGTZ_WriteRegister(handle, 0x12C0,ChannelDCOffsetMask_1[2] );
//   ret |= CAEN_DGTZ_WriteRegister(handle, 0x12C4,ChannelDCOffsetMask_2[2] );
//   ret |= CAEN_DGTZ_WriteRegister(handle, 0x13C0,ChannelDCOffsetMask_1[3] );
//   ret |= CAEN_DGTZ_WriteRegister(handle, 0x13C4,ChannelDCOffsetMask_2[3] );
  std::cout << "done!" << std::endl;
  
  //-----------------------------//
  // Set self Trigger Thresholds //
  //-----------------------------// 
  NumGr=NUM_GR;
  if(Params.GlobalTriggerThreshold) //if a global threshold different from 0 has been specified, modify the trigger thresholds of each group, according to the new DC values and to polarity
  {
    if(Params.SignalPolarity) //Negative polarity
    {
      for(int i = 0 ; i < NumGr ; i++)
      {
	Params.TriggerThreshold[i] = AvDC[i] - Params.GlobalTriggerThreshold;
	std::cout << "AvDC[" << i << "]" << AvDC[i] << std::endl;
      }
    }
    else //Positive polarity
    {
      for(int i = 0 ; i < NumGr ; i++)
      {
	Params.TriggerThreshold[i] = AvDC[i] + Params.GlobalTriggerThreshold;
	std::cout << "AvDC[" << i << "]" << AvDC[i] << std::endl;
      }
    }
  }
  //set self trigger thresholds
  for(int i=0; i<NumGr; i++)
  {
    ret |= CAEN_DGTZ_SetGroupTriggerThreshold(handle,i,Params.TriggerThreshold[i]);    
    printf("Trigger Threshold for Group %d = %d\n",i,Params.TriggerThreshold[i]); 
  }
  

  //-----------------//
  // Readout loop    //
  //-----------------//
  while(1) //infinite loop, with break and continue...
  {
    
    if (kbhit()) //take the command from keyboard, if any...
    {
      c = getch(); 
      if (c=='q')	break;
      if (c=='t') 
	SWtrg ^= 1;
      if (c=='p') Plot = 1;
      if (c=='l') SaveList = 1;
      if (c=='r')
      {
	for(int j=0; j<NUM_CH; j++) 
	{
	  memset(Histo[j], 0, HISTO_NBIN * sizeof(uint32_t));
	}
      }
      if (c==' ') ChangeMode = 1;
      if (c=='c') 
      {
	printf("Channel = ");
	scanf("%d", &ch);
	// HACK decommentare per resettare istogramma
// 	for(int j=0; j<NUM_CH; j++) 
// 	{
// 	  memset(Histo[j], 0, HISTO_NBIN * sizeof(uint32_t));
// 	}
      }
//       if (c=='h') 
//       {
// 	Histograms = 1;
//       }
    }
    
    if( listNum == 0 ){
      t1 = new TTree("adc","adc"); 
      t1->Branch("ExtendedTimeTag",&ExtendedTimeTag,"ExtendedTimeTag/l"); 	//absolute time tag of the event
      t1->Branch("DeltaTimeTag",&DeltaTimeTag,"DeltaTimeTag/l"); 			//delta time from previous event
      for (int i = 0 ; i < 32 ; i++)
      {
	//empty the stringstreams
        snames.str(std::string());
        stypes.str(std::string());
        charge[i] = 0;
        snames << "ch" << i;
        stypes << "ch" << i << "/S";
        names = snames.str();
        types = stypes.str();
        t1->Branch(names.c_str(),&charge[i],types.c_str());
      }
      std::cout << "RunNumber = " << runNumber << std::endl;
    }
    else if( (listNum != 0) && ( (int)(listNum/ROOTFILELENGTH) > NumOfRootFile )){
      NumOfRootFile++;
      //save previous ttree
      //file name
      std::stringstream fileRootStream;
      std::string fileRoot;
      fileRootStream << dirName << "/TTree_" << filePart << "_" << actual_time << ".root";
      fileRoot = fileRootStream.str();
      std::cout << "Saving root file "<< fileRoot << "..." << std::endl;
      TFile* fTree = new TFile(fileRoot.c_str(),"recreate");
      fTree->cd();
      t1->Write();
      fTree->Close();
      //delete previous ttree
      delete t1;
      
      //create new ttree
      t1 = new TTree("adc","adc"); 
      t1->Branch("ExtendedTimeTag",&ExtendedTimeTag,"ExtendedTimeTag/l"); 	//absolute time tag of the event
      t1->Branch("DeltaTimeTag",&DeltaTimeTag,"DeltaTimeTag/l"); 			//delta time from previous event
      for (int i = 0 ; i < 32 ; i++)
      {
	//empty the stringstreams
        snames.str(std::string());
        stypes.str(std::string());
        charge[i] = 0;
        snames << "ch" << i;
        stypes << "ch" << i << "/S";
        names = snames.str();
        types = stypes.str();
        t1->Branch(names.c_str(),&charge[i],types.c_str());
      }
      filePart++;
      std::cout << "ListNum = " << listNum << std::endl;
	    //std::cout << counter << std::endl;
	    
    }    
    
    
    
    //play with time...
    CurrTime = get_time();
    if ((CurrTime-PrevTime)>1000) 
    {
      printf("Tot Num Events = %d\n", EvCnt);
      printf("Trigger Rate = %.2f KHz\n", (float)(EvCnt-PrevEvCnt) / (CurrTime-PrevTime));
      printf("Readout Rate = %.2f MB/s\n\n", (float)nb / 1024 / (CurrTime-PrevTime));
      //printf("b=%d\n", bsize);
      nb = 0;
      PrevTime = CurrTime;
      PrevEvCnt = EvCnt;
      Plot=1;
    }
    
    if (SWtrg) CAEN_DGTZ_SendSWtrigger(handle); /* Send a SW Trigger */
    
    /* Read a block of data from the digitizer */
    ret = CAEN_DGTZ_ReadData(handle, CAEN_DGTZ_SLAVE_TERMINATED_READOUT_MBLT, buffer, &bsize); /* Read the buffer from the digitizer */
    
    if (ret) 
    {
      printf("Readout Error\n");
      break;
    }
    
    if (bsize != 0) //perform the following only if the digitizer has something in the readout buffer
    {
      nb += bsize;
      if (Params.AcqMode == WAVEFORM_ACQMODE) //wavefore mode
      {
	
	CAEN_DGTZ_GetNumEvents(handle, buffer, bsize, &NumEvents);
	for (i=0; i<NumEvents; i++) 
	{
	  //std::cout << "aaaa" << std::endl;
	  /* Get the Infos and pointer to the event */
	  CAEN_DGTZ_GetEventInfo(handle, buffer, bsize, i, &eventInfo, &evtptr);
	  /* Decode the event to get the data */
	  CAEN_DGTZ_DecodeEvent(handle, evtptr, &Evt_VoidType);
	  
	  Evt = (CAEN_DGTZ_UINT16_EVENT_t *) (Evt_VoidType);
	  //int aaaa = Evt->ChSize[ch];
	  //std::cout << "Evt->ChSize[ch] = " << aaaa << std::endl;
	  
	  /* Save Waveform and Plot it using gnuplot */
	  if (Plot) 
	  {
	    plotdata = fopen("PlotData.txt", "w");
	    for(i=0; i<(int)Evt->ChSize[ch]; i++) 
	    {
	      if (Params.GateTrace) 
	      {
		fprintf(plotdata, "%d ", Evt->DataChannel[ch][i] & 0xFFE);  // samples
		fprintf(plotdata, "%d\n", 2000 + 200 * (Evt->DataChannel[ch][i] & 0x001));  // gate
	      } 
	      else 
	      {
		fprintf(plotdata, "%d ", Evt->DataChannel[ch][i]);  // samples
	      }
	    }
	    fclose(plotdata);
            fprintf(gnuplot, "set term x11 noraise nopersist\n");
	    if (Params.GateTrace) 
	      fprintf(gnuplot, "plot 'PlotData.txt' using 1 with steps, 'PlotData.txt' using 2  with steps \n");
	    else
	      fprintf(gnuplot, "plot 'PlotData.txt' with steps \n");						
	    fflush(gnuplot);
	    Plot=0;
	  }
	  CAEN_DGTZ_FreeEvent(handle, &Evt_VoidType);
	}
      } 
      else //histogram mode
      {
	ret = _CAEN_DGTZ_DecodeEvents_DPPCIx740(handle, buffer, bsize, &NumEvents, EvtDPPCI);
	for(i=0; i<NumEvents; i++) 
	{
	  int Charge;
// 	  std::cout << std::bitset<4>(EvtDPPCI[i].GroupMask) << std::endl;
	  
	  //------KEY POINT--------
	  // next if fills the histogram. the original if is set to do the following
	  // 1. checks if the channel selected as trigger (ch) belongs to an active group, via 
	  //   (EvtDPPCI[i].GroupMask & (1<<(ch/8)))
	  // ch/8 gives the int part so it defines the group, then 1 is shifted of ch/8 bit and an AND is performed with the group mask
	  // 2. Check that the charge is lower than the HISTO_BIN variable
	  // 3. Check that the charge is not negative
	  //
	  // Now, 2 and 3 maybe are not really necessary, and in fact they have to be tested. The bin number can easily be increased
	  // but most importantly the charge set to negative could solve the problem of the FIFO. But on the other hand
	  // according to CAEN the integration is correct only for negative signals (and i guess is inverted directly in the firmware 
	  // so it might be that it doesn't work)
	  //-----------------------
	  for(int j=0; j<NUM_CH; j++) 
	  {
	    if(Params.SignalPolarity) // Negative polarity
	    {
	      if ( (EvtDPPCI[i].GroupMask & (1<<(j/8))) && EvtDPPCI[i].Charge[j]<HISTO_NBIN && ( ((EvtDPPCI[i].Charge[j]) + PEDESTAL) > 0 ) ) 
	      {
		Charge = PEDESTAL + EvtDPPCI[i].Charge[j];
		Histo[j][Charge]++;
	      }
	    }
	    else// Positive polarity
	    {
	      if ( (EvtDPPCI[i].GroupMask & (1<<(j/8))) && -EvtDPPCI[i].Charge[j]<HISTO_NBIN && ( -((EvtDPPCI[i].Charge[j]) + PEDESTAL) > 0 ) ) 
	      {
		Charge = -(PEDESTAL + EvtDPPCI[i].Charge[j]);
		Histo[j][Charge]++;
	      }
	    }
	  }
	  
	  if (EvtDPPCI[i].TimeTag < PrevTimeTag32)
	    ett++;
	  ExtendedTimeTag = (ett << 32) + (uint64_t)EvtDPPCI[i].TimeTag;
	  DeltaTimeTag = ExtendedTimeTag-PrevTimeTag;
	  
	  if (SaveList) 
	  {
	    //double maxCharge = 0;
	    //double secondCharge = 0;
	    //columnsum=0;
	    //rowsum=0;
	    //total=0;
	    int j;
//	    if (list == NULL) 
//	    {
//	      list = fopen(listFileName.c_str(), "w");
//	      std::cout << "Saving '" << listFileName << "'" << std::endl;
//	      //printf("Saving 'ListFile.txt'\n", ch);
//	    }
//	    fprintf(list, "%16llu %16llu ", ExtendedTimeTag, ExtendedTimeTag-PrevTimeTag);
	    for(j=0; j<NUM_CH; j++)
	    {
	      if (EvtDPPCI[i].GroupMask & (1<<(j/8)))
	      {
		if(Params.SignalPolarity){// Negative polarity
		  //fprintf(list, "%8d ", EvtDPPCI[i].Charge[j]);
		  charge[j]= EvtDPPCI[i].Charge[j];             //for the ttree
		}
		else{// Positive polarity
		  //fprintf(list, "%8d ", -EvtDPPCI[i].Charge[j]);
		  charge[j]= -EvtDPPCI[i].Charge[j];             //for the ttree
		}
// 		if (charge[j] > maxCharge) //find trigger channel
// 		{
// 		  maxCharge = charge[j];
// 		  TriggerChannel = j;
// 		}
// 		//compute the flood histo position
// 		total+=charge[j];
// 	        rowsum += charge[j]*xmppc[j];
// 	        columnsum += charge[j]*ymppc[j];
		
		//secondCharge = maxCharge; //we didn't really need the first on second but i don't want to modify the analysis to get rid of it TODO
		
		//original was preventing negative charges to be dumped
// 		if (EvtDPPCI[i].Charge[j] >= CHARGE_CUT)
// 		  fprintf(list, "%8d ", EvtDPPCI[i].Charge[j]);
// 		else
// 		  fprintf(list, "%8d ", 0);
	      }
	    }
	    
	    //compute flood x and y
	    //floodx=rowsum/total;
	    //floody=columnsum/total;
	    //compute first on second ratio
	    //firstonsecond = maxCharge / secondCharge;
	    
	    t1->Fill();//fills the tree with the data
//	    fprintf(list, "\n");
	    
	    //increase the run number
            listNum++;
	    
	    
	  }
	  PrevTimeTag = ExtendedTimeTag;
	  PrevTimeTag32 = EvtDPPCI[i].TimeTag;
	}
	if (Plot) {
	  plotdata = fopen("PlotData.txt", "w");
	  for(i=0; i<HISTO_NBIN; i++)
	    fprintf(plotdata, "%d\n", Histo[ch][i]);
	  fclose(plotdata);
          fprintf(gnuplot, "set term x11 noraise nopersist\n");
	  fprintf(gnuplot, "plot 'PlotData.txt' with steps  ti \"Channel %d, Num of Events = %d\"\n",ch,EvCnt);
	  fflush(gnuplot);
	  Plot=0;
	}
      }
      EvCnt += NumEvents;
      
      if (ChangeMode) 
      {
	
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
      
    }
    runNumber++;
  } // end of readout loop
  // 
  
  std::cout << "Disconnecting digitizer..." << std::endl;
  // Free the buffers and close the digitizer
  ret = CAEN_DGTZ_FreeReadoutBuffer(&buffer);
  ret = CAEN_DGTZ_CloseDigitizer(handle);
  if (EvtDPPCI != NULL)
    free(EvtDPPCI);
  if (gnuplot != NULL)
    fclose(gnuplot);
  if (list != NULL)
    fclose(list);
  
  std::stringstream fileRootStreamFinal;
  std::string fileRootFinal;
  fileRootStreamFinal << dirName << "/TTree_" << filePart << "_" << actual_time << ".root";
  fileRootFinal = fileRootStreamFinal.str();
  std::cout << "Saving root file "<< fileRootFinal << "..." << std::endl;
  TFile* fTreeFinal = new TFile(fileRootFinal.c_str(),"recreate");
  fTreeFinal->cd();
  t1->Write();
  fTreeFinal->Close();
  
  std::cout << "... bye!" << std::endl;
  
}

int SetDefaultParameters(ParamsType &Params)
{
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
  Params.AcqMode = WAVEFORM_ACQMODE;  // Acquisition Mode (WAVEFORM_ACQMODE or CHARGE_ACQMODE)
  Params.PrintParameters = 0;
  Params.SignalPolarity = 0;          //LEGEND SignalPolarity = 0 -> positive polarity
                                      //                      = 1 -> negative polarity
  Params.GlobalTriggerThreshold = 0;  // global trigger threshold

  return 0;
}

int PrintParameters(ParamsType &Params)
{
  std::cout << "--------------------------- Parameters -------------------------" << std::endl;
  std::cout << "Params.RecordLength        	= " << Params.RecordLength        << std::endl;
  std::cout << "Params.PreTrigger          	= " << Params.PreTrigger          << std::endl; 
  std::cout << "Params.ActiveChannel       	= " << Params.ActiveChannel       << std::endl;
  std::cout << "Params.BaselineMode        	= " << Params.BaselineMode        << std::endl;
  std::cout << "Params.FixedBaseline       	= " << Params.FixedBaseline       << std::endl;
  std::cout << "Params.GateOffset          	= " << Params.GateOffset          << std::endl;  
  std::cout << "Params.GateWidth           	= " << Params.GateWidth           << std::endl; 
  std::cout << "Params.TriggerThreshold[0] 	= " << Params.TriggerThreshold[0] << std::endl; 
  std::cout << "Params.TriggerThreshold[1] 	= " << Params.TriggerThreshold[1] << std::endl; 
  std::cout << "Params.TriggerThreshold[2] 	= " << Params.TriggerThreshold[2] << std::endl; 
  std::cout << "Params.TriggerThreshold[3] 	= " << Params.TriggerThreshold[3] << std::endl; 
  std::cout << "Params.TriggerThreshold[4] 	= " << Params.TriggerThreshold[4] << std::endl; 
  std::cout << "Params.TriggerThreshold[5] 	= " << Params.TriggerThreshold[5] << std::endl; 
  std::cout << "Params.TriggerThreshold[6] 	= " << Params.TriggerThreshold[6] << std::endl; 
  std::cout << "Params.TriggerThreshold[7] 	= " << Params.TriggerThreshold[7] << std::endl; 
  std::cout << "Params.DCoffset[0]         	= " << Params.DCoffset[0]         << std::endl; 
  std::cout << "Params.DCoffset[1]         	= " << Params.DCoffset[1]         << std::endl; 
  std::cout << "Params.DCoffset[2]         	= " << Params.DCoffset[2]         << std::endl; 
  std::cout << "Params.DCoffset[3]         	= " << Params.DCoffset[3]         << std::endl; 
  std::cout << "Params.DCoffset[4]         	= " << Params.DCoffset[4]         << std::endl; 
  std::cout << "Params.DCoffset[5]         	= " << Params.DCoffset[5]         << std::endl; 
  std::cout << "Params.DCoffset[6]         	= " << Params.DCoffset[6]         << std::endl; 
  std::cout << "Params.DCoffset[7]         	= " << Params.DCoffset[7]         << std::endl; 
  std::cout << "Params.GateTrace           	= " << Params.GateTrace           << std::endl; 
  std::cout << "Params.ChargeSensitivity   	= " << Params.ChargeSensitivity   << std::endl; 
  std::cout << "Params.NevAggr             	= " << Params.NevAggr             << std::endl;         
  std::cout << "Params.AcqMode             	= " << Params.AcqMode             << std::endl; 
  std::cout << "Params.GlobalTriggerThreshold   = " << Params.GlobalTriggerThreshold << std::endl;
  std::cout << "----------------------------------------------------------------" << std::endl;
  
  return 0;
}

int ReadConfigFile(FILE *params, ParamsType &Params, int &SaveList)
{
  params = fopen("config.txt", "r");
  if (params != NULL) 
  {
    while (!feof(params)) 
    {
      char str[100];
      fscanf(params, "%s", str);
      if (strcmp(str, "TriggerThreshold") == 0) 
      {
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
      if (strcmp(str, "DCoffset") == 0) 
      {
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
	fscanf(params, "%lx", &Params.ChannelTriggerMask);	
      if (strcmp(str, "PrintParameters") == 0) 
	fscanf(params, "%d", &Params.PrintParameters);
        //fscanf(params, "%llx", &Params.ChannelTriggerMask);	//was like this, compiler gave a warning
      if (strcmp(str, "SignalPolarity") == 0) 
	fscanf(params, "%d", &Params.SignalPolarity);
        //fscanf(params, "%llx", &Params.ChannelTriggerMask);	//was like this, compiler gave a warning
      if (strcmp(str, "GlobalTriggerThreshold") == 0) 
	fscanf(params, "%d", &Params.GlobalTriggerThreshold);
    }
    fclose(params);
  }
  
  return 0;
  
}


bool ConnectToDigitizer(int &handle, int &ret)
{
  std::cout << "Connecting to Digitizer..." << std::endl;
  ret = CAEN_DGTZ_OpenDigitizer(CAEN_DGTZ_USB, 0, 0, 0, &handle); 
  if(ret != CAEN_DGTZ_Success) 
  {
    printf("Can't open digitizer\n");
    return false;
  }
  else //connection successful 
  {
    return true;
  }
}

int GetBoardInfo(int &handle, CAEN_DGTZ_BoardInfo_t &BoardInfo , int &ret)
{
  ret = CAEN_DGTZ_GetInfo(handle, &BoardInfo);
  printf("\nConnected to CAEN Digitizer Model %s\n", BoardInfo.ModelName);
  printf("ROC FPGA Release is %s\n", BoardInfo.ROC_FirmwareRel);
  printf("AMC FPGA Release is %s\n\n", BoardInfo.AMC_FirmwareRel);
  return 0;
}

int WriteSettingsToDigitizer(int &handle,int &ret,ParamsType &Params)
{
  
  uint32_t DppCtrl;
  //Reset Digitizer 
  ret |= CAEN_DGTZ_Reset(handle);                                                  
  
  // Enable groups 
  ret |= CAEN_DGTZ_SetGroupEnableMask(handle,Params.GroupMask);                    
  
  //debug
  //uint32_t answer;
  //ret |= CAEN_DGTZ_GetGroupEnableMask(handle,&answer);                    /* check */
  //std::cout << "answer = " << std::bitset<4>(answer) << std::endl;
  
  //hardcoded for our digitizer since the num of ch got from CAEN_DGTZ_GetInfo says 4...
  int NumCh=NUM_CH;
  int NumGr=NUM_GR;
  
  // Set selfTrigger threshold
//   for(int i=0; i<NumGr; i++)
//   {
//     ret |= CAEN_DGTZ_SetGroupTriggerThreshold(handle,i,Params.TriggerThreshold[i]);    
//     //printf("%d\t",Params.TriggerThreshold[i]); //MOD
//   }
  
  // Enable self trigger for the acquisition 
  ret |= CAEN_DGTZ_SetGroupSelfTrigger(handle,CAEN_DGTZ_TRGMODE_ACQ_ONLY,0xFF);    //self trigger set to ACQ_ONLY to all groups, in fact to 8 groups (so in fact 0xF should be enough for us)
  
  // enable channels for triggering according to ChannelTriggerMask 32bit number
  for(int i=0; i<NumGr; i++)
    ret |= CAEN_DGTZ_SetChannelGroupMask(handle, i, (Params.ChannelTriggerMask>>(i*8)) & 0xFF);    
  
  //print group enabled and trigger config status
  uint32_t printGroupEnableMask;
  uint32_t printChannelTriggerMask[4];
  //uint32_t groupTriggerThresholdMask;
  ret |= CAEN_DGTZ_GetGroupEnableMask(handle,&printGroupEnableMask);                    /* check */
  for(int i=0; i<NumGr; i++)
  {
    ret |= CAEN_DGTZ_GetChannelGroupMask(handle,i,&printChannelTriggerMask[i]);                    /* check */
  }
  std::cout << "Gr" << "\t" << "On/Off" << "\t" << "Tr. Channel Mask" << std::endl;;
  for(int i=0; i<NumGr; i++)
  {
    std::cout << i << "\t";
    std::cout << ((printGroupEnableMask >> i) & 1) << "\t";
    std::cout << std::bitset<8>(printChannelTriggerMask[i]) << std::endl;
    //std::cout << std::bitset<8>(((Params.ChannelTriggerMask>>(i*8)) & 0xFF)) << std::endl;
  }
  
  /* Set the behaviour when a SW tirgger arrives */
  ret |= CAEN_DGTZ_SetSWTriggerMode(handle,CAEN_DGTZ_TRGMODE_ACQ_ONLY);          
  
  if(Params.SignalPolarity)
  {
    ret |= CAEN_DGTZ_WriteRegister(handle, 0x8004, 1<<6); /* set trigger edge to negative */
    std::cout << "Trigger Edge Set to Negative" << std::endl;
  }
  else
  {
    ret |= CAEN_DGTZ_WriteRegister(handle, 0x8008, 1<<6); /* set trigger edge to positive */
    std::cout << "Trigger Edge Set to Positive" << std::endl;
  }
    
  ret |= CAEN_DGTZ_SetMaxNumAggregatesBLT(handle, BLT_AGGRNUM);                    /* Set the max number of events/aggregates to transfer in a sigle readout */
  ret |= CAEN_DGTZ_SetAcquisitionMode(handle,CAEN_DGTZ_SW_CONTROLLED);             /* Set the start/stop acquisition control */
  
  DppCtrl = ((Params.BaselineMode & 0x3) << 4) | (Params.ChargeSensitivity & 0xF) | ((Params.GateTrace & 1) << 8);
  ret |= CAEN_DGTZ_WriteRegister(handle, 0x8040, DppCtrl);
  ret |= CAEN_DGTZ_WriteRegister(handle, 0x803C, Params.PreTrigger);               /* Set Pre Trigger (in samples) */
  ret |= CAEN_DGTZ_WriteRegister(handle, 0x8030, Params.GateWidth);                /* Set Gate Width (in samples) */
  ret |= CAEN_DGTZ_WriteRegister(handle, 0x8034, Params.GateOffset);               /* Set Gate Offset (in samples) */
  ret |= CAEN_DGTZ_WriteRegister(handle, 0x8038, Params.FixedBaseline);            /* Set Baseline (used in fixed baseline mode only) */
  
  /* Set DC offset */
  for (int i=0; i<4; i++)
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
  return 0;
}


// int RootPlots(WaveDumpConfig_t *WDcfg, WaveDumpRun_t *WDrun, CAEN_DGTZ_EventInfo_t *EventInfo, CAEN_DGTZ_X742_EVENT_t *Event,
// 	       FILE* pipe, )
//  {
//    
//    for (int iOut = 0 ; iOut < 16 ; iOut++) //not so general implementation...
//    {
//      //if(iOut == TriggerChannel) fprintf(pipe,"%e\n",-WDrun->FinalIntegral[iOut]);
//      else fprintf(pipe,"%e\n",WDrun->FinalIntegral[iOut]);
//      //printf("%f\n",-WDrun->FinalIntegral[iOut]);
//    }
//    fflush(pipe);
//    return 0;
//  } 
