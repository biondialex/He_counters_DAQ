#include <string>

//---------------------------------------------------------------------------------
//---------------------------HARDWARE CONFIGURATION--------------------------------
//---------------------------------------------------------------------------------

//confiure the number of channels and cards in the NETBOX 
//code assumes cards with 4 inputs !!!
#define CHANNEL_NUMBER 16
#define CARD_NUMBER 4

// input range 200mV
#define ADC_RANGE 200

//enable AC input coupling
#define ADC_AC 0

//enable input offset compensation
#define ADC_OFS_COMP 0

//set event size and pretrigger
#define ADC_EVENT_SIZE 206144
#define ADC_PRETRIGGER 26624


//Samples before and afteer ADC_PRETRIGGER where to search for the trigger 
#define SEARCH_PRE_DEFAULT  10000
#define SEARCH_POST_DEFAULT 10000




//---------------------------------------------------------------------------------
//-----------------------INTERNAL PARAMETERS , DO NOT TOUCH------------------------
//---------------------------------------------------------------------------------


#define OFS_MAX 2500
#define OFS_MIN -2500
#define THR_MAX 2500
#define THR_MIN -2500

//---------------------------------------------------------------------------------

#define CONN_DISCONNECTED 0
#define CONN_CONNECTED 1
#define CONN_RUNNING 2
#define CONN_ERROR -1

//---------------------------------------------------------------------------------

using namespace std;

extern string device_ip_addr;

extern int channel_adc_range[CHANNEL_NUMBER];
extern int channel_thr [CHANNEL_NUMBER];
extern int channel_ofs [CHANNEL_NUMBER];
extern int channel_enabled [CHANNEL_NUMBER];
extern int channel_trig_pol [CHANNEL_NUMBER];
extern int channel_search_pre[CHANNEL_NUMBER];   // samples before ADC_PRETRIGGER
extern int channel_search_post[CHANNEL_NUMBER];  // samples after ADC_PRETRIGGER


extern volatile int connection_status;

extern int64_t SampleRate;

int read_config_file ();
