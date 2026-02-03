#ifndef SPECTRUM_ACQ_H
#define SPECTRUM_ACQ_H
#include "c_header/dlltyp.h"
#include "c_header/regs.h"
#include "c_header/spcerr.h"
#include "c_header/spcm_drv.h"
#include <cstdint>

extern drv_handle hDrv[CARD_NUMBER];

extern volatile int thread_lock_flag [CARD_NUMBER];

extern volatile int display_data [CHANNEL_NUMBER][ADC_EVENT_SIZE]; 
extern volatile int display_refresh_flags [CHANNEL_NUMBER];



#define WF_ARCHIVE_CAPACITY 32
constexpr int ENERGY_NBINS = 5000;
constexpr double ENERGY_MIN = 0.0; //mV
constexpr double ENERGY_MAX = 2500;

extern int8_t  waveform_archive_raw[CHANNEL_NUMBER][WF_ARCHIVE_CAPACITY][ADC_EVENT_SIZE];
extern int64_t waveform_archive_time[CHANNEL_NUMBER][WF_ARCHIVE_CAPACITY];
extern uint32_t waveform_archive_count[CHANNEL_NUMBER];
extern uint32_t waveform_archive_write_index[CHANNEL_NUMBER];
extern uint64_t energy_spectrum[CHANNEL_NUMBER][ENERGY_NBINS];


int open_spectrum_device ();

int configure_spectrum_device ();

void close_spectrum_device ();

void start_spectrum_acq ();
void stop_spectrum_acq ();

bool open_run_file(const std::string& filename);
void flush_all_buffers_to_file();
void close_run_file();
bool flush_channel_buffer_to_file(int ch);

bool software_trigger_pass(int chGlobal,const int8_t* src,int nEnabled,int k);

void clear_energy_spectra();

void *acq_loop (void* arg);

void start_run_visual_timer();
double get_run_visual_elapsed_seconds();

// Energy filter (used by offline analysis too)
double energy_filter(const int8_t* in,
                     int           N,
                     int           pretrigger,
                     int           window_size,
                     int           n_passes);


#endif 
