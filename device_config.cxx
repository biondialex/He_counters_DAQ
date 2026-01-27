#include <string>
#include <cstring>
#include <iostream>
#include <fstream>
#include "device_config.h"

using namespace std;

string device_ip_addr = "0.0.0.0";

int channel_thr [CHANNEL_NUMBER]; 
int channel_ofs [CHANNEL_NUMBER];
int channel_enabled [CHANNEL_NUMBER];
int channel_trig_pol [CHANNEL_NUMBER];
int channel_search_pre [CHANNEL_NUMBER];
int channel_search_post[CHANNEL_NUMBER];
int channel_adc_range[CHANNEL_NUMBER];

int64_t SampleRate = 0;

volatile int connection_status = CONN_DISCONNECTED;


int read_config_file () {
    string param_line;

    //initialize parameter tables with default values

    for (int i = 0; i < CHANNEL_NUMBER ; i++ ) {
	channel_thr [i] = 0;
	channel_ofs [i] = 0;
	channel_enabled [i] = 0;
	channel_trig_pol [i] = 0;
	channel_search_pre[i]  = SEARCH_PRE_DEFAULT;   // or 0
    channel_search_post[i] = SEARCH_POST_DEFAULT;
	channel_adc_range[i] = ADC_RANGE;
    }

    //read configuration file
    ifstream config_file ("spectr_acq.cfg", ifstream::in);
    
    if (config_file.is_open()) {

	while ( getline (config_file , param_line) ) {

	    for (int ch_num = 0 ; ch_num < CHANNEL_NUMBER ; ch_num ++) { // check for channel parameters
		string ch_template = string("CH")  + to_string(ch_num) + string("_");
		if ( strstr ( param_line.c_str() , ch_template.c_str()) != NULL ) { // check channel number
		    if (strstr ( param_line.c_str() , "ENABLED") != NULL) { // check ENABLED flag
			string s_value = param_line.substr ( param_line.find ( string ("=")) + 1 , param_line.length() );
			channel_enabled [ch_num] = stoi(s_value);
			if ((channel_enabled [ch_num] > 1) || ( channel_enabled [ch_num] < 0)) channel_enabled [ch_num] = 0;
		    }

		    if (strstr ( param_line.c_str() , "TR_POL") != NULL) { // check trigger polarity flag
			string s_value = param_line.substr ( param_line.find ( string ("=")) + 1 , param_line.length() );
			channel_trig_pol [ch_num] = stoi(s_value);
			if ((channel_trig_pol [ch_num] > 3) || ( channel_trig_pol [ch_num] < 0)) channel_trig_pol [ch_num] = 0;
		    }

		    if (strstr ( param_line.c_str() , "TR_LEVEL") != NULL) { // check trigger level
			string s_value = param_line.substr ( param_line.find ( string ("=")) + 1, param_line.length() );
			channel_thr [ch_num] = stoi(s_value);
			if ((channel_thr [ch_num] > THR_MAX))  channel_thr [ch_num] = THR_MAX;
			if ((channel_thr [ch_num] < THR_MIN))  channel_thr [ch_num] = THR_MIN;
		    }

		    if (strstr ( param_line.c_str() , "OFS") != NULL) { // check channel offset
			string s_value = param_line.substr ( param_line.find ( string ("=")) + 1, param_line.length() );
			channel_ofs [ch_num] = stoi(s_value);
			if ((channel_ofs [ch_num] > OFS_MAX))  channel_ofs [ch_num] = OFS_MAX;
			if ((channel_ofs [ch_num] < OFS_MIN))  channel_ofs [ch_num] = OFS_MIN;
		    }
			
			if (strstr(param_line.c_str(), "SRCH_PRE") != NULL) {
				string s_value = param_line.substr(param_line.find(string("=")) + 1,
												param_line.length());
				int v = stoi(s_value);
				if (v < 0) v = 0;
				if (v > ADC_PRETRIGGER) v = ADC_PRETRIGGER; 
				channel_search_pre[ch_num] = v;
			}

			if (strstr(param_line.c_str(), "SRCH_POST") != NULL) {
				string s_value = param_line.substr(param_line.find(string("=")) + 1,
												param_line.length());
				int v = stoi(s_value);
				if (v < 0) v = 0;
				if (v > (ADC_EVENT_SIZE - ADC_PRETRIGGER))
					v = ADC_EVENT_SIZE - ADC_PRETRIGGER; 
				channel_search_post[ch_num] = v;
			}
			
			if (strstr(param_line.c_str(), "ADC_RANGE") != NULL) {
				std::string s_value = param_line.substr(param_line.find(std::string("=")) + 1,
														param_line.length());
				int v = stoi(s_value);

				// allow only known values
				if (v != 200 && v != 500 && v != 1000 && v != 2500)
					v = ADC_RANGE;

				channel_adc_range[ch_num] = v;
			}


		}

	    }

	    if (strstr ( param_line.c_str() , "IP_ADDR") != NULL) { // check IP address
		device_ip_addr = param_line.substr ( param_line.find ( string ("=")) + 1, param_line.length() );
	    }

	}

	for (int ch = 0; ch < CHANNEL_NUMBER; ++ch) {
		int r = channel_adc_range[ch];
		if (r <= 0) r = ADC_RANGE;

		if (channel_thr[ch] >  r) channel_thr[ch] =  r;
		if (channel_thr[ch] < -r) channel_thr[ch] = -r;

		if (channel_ofs[ch] >  r) channel_ofs[ch] =  r;
		if (channel_ofs[ch] < -r) channel_ofs[ch] = -r;
	}


	config_file.close ();
	return (1);
    } else return (0);
}