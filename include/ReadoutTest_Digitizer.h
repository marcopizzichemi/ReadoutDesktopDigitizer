//--------------------------------------------//
//                                            //
//      Headers for the digitizer reader      //
//                                            //
//--------------------------------------------//

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
  int PrintParameters;
  uint64_t ChannelTriggerMask;
  int SignalPolarity;
  int GlobalTriggerThreshold;
} ParamsType;

int SetDefaultParameters(ParamsType &Params);
int PrintParameters(ParamsType &Params);
int ReadConfigFile(FILE *params, ParamsType &Params, int &SaveList);
bool ConnectToDigitizer(int &handle, int &ret);
int GetBoardInfo(int &handle, CAEN_DGTZ_BoardInfo_t &BoardInfo , int &ret);
int WriteSettingsToDigitizer(int &handle,int &ret,ParamsType &Params);