#include <string>
#include <iostream>
#include <fstream>
#include <cstring>
#include <thread>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <cstdint>
#include <sys/mman.h>
#include "c_header/dlltyp.h"
#include "c_header/regs.h"
#include "c_header/spcerr.h"
#include "c_header/spcm_drv.h"
#include "device_config.h"
#include "WaveformFileFormat.h"
#include "spectrum_acq.h"    // for SampleRate, CHANNEL_NUMBER
#include <mutex>
#include <atomic>



drv_handle hDrv[CARD_NUMBER];
volatile int thread_lock_flag[CARD_NUMBER];

volatile int display_data[CHANNEL_NUMBER][ADC_EVENT_SIZE];
volatile int display_refresh_flags[CHANNEL_NUMBER];

int8_t  waveform_archive_raw[CHANNEL_NUMBER][WF_ARCHIVE_CAPACITY][ADC_EVENT_SIZE];
int64_t waveform_archive_time[CHANNEL_NUMBER][WF_ARCHIVE_CAPACITY];
uint32_t waveform_archive_count[CHANNEL_NUMBER]       = {0};
uint32_t waveform_archive_write_index[CHANNEL_NUMBER] = {0};
uint64_t energy_spectrum[CHANNEL_NUMBER][ENERGY_NBINS] = {{0}};
static std::atomic<int64_t> g_run_vis_start_ns{0};




static std::ofstream g_run_file;
static bool          g_run_file_open = false;
static FileHeader    g_file_header;
static std::mutex    g_file_mutex;   // protect file writes if multiple threads


/*
**************************************************************************
memory allocation (page aligned)
**************************************************************************
*/
void* pvAllocMemPageAligned(uint64 qwBytes) {
    void* pvTmp;
    int fd = open("/dev/zero", O_RDONLY);
    pvTmp = (void*) mmap(NULL, qwBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);

    // set everything to zero to get memory allocated in physical mem
    if (pvTmp != MAP_FAILED)
        memset(pvTmp, 0, qwBytes);
    else
        pvTmp = NULL;

    close(fd);
    return (pvTmp);
}

void vFreeMemPageAligned(void* pvAdr, uint64 qwBytes) {
    munmap(pvAdr, qwBytes);
}

// ***********************************************************************

int open_spectrum_device() {
    char szErrorBuf[ERRORTEXTLEN];

    // open card handles
    for (int card_index = 0; card_index < CARD_NUMBER; card_index++) {
        std::string sDeviceOpen = "TCPIP::" + device_ip_addr + "::inst" + std::to_string(card_index) + "::INSTR";
        std::cout << "Opening card : " << sDeviceOpen << std::endl;
        hDrv[card_index] = spcm_hOpen(sDeviceOpen.c_str());
        if (!hDrv[card_index]) {
            spcm_dwGetErrorInfo_i64(hDrv[card_index], NULL, NULL, szErrorBuf);
            std::cout << "SPECTRUM driver open error: " << std::string(szErrorBuf) << std::endl;
            return (0);
        }
    }

    // open starhub handle
    std::cout << "Opening starhub" << std::endl;
    hDrv[CARD_NUMBER] = spcm_hOpen("sync0");
    if (!hDrv[CARD_NUMBER]) {
        spcm_dwGetErrorInfo_i64(hDrv[CARD_NUMBER], NULL, NULL, szErrorBuf);
        std::cout << "SPECTRUM driver open error: " << std::string(szErrorBuf) << std::endl;
        return (0);
    }

    return (1);
}

// ***********************************************************************

int configure_spectrum_device() {
    char szErrorBuf[ERRORTEXTLEN];

    for (int card_index = 0; card_index < CARD_NUMBER; card_index++) {
        uint32 dwError = 0;

        std::cout << "Configuring  card : " << std::to_string(card_index) << std::endl;

        int ch0 = card_index*4 + 0;
        int ch1 = card_index*4 + 1;
        int ch2 = card_index*4 + 2;
        int ch3 = card_index*4 + 3;

        // enable channels
        int32_t cnf_ch_enabled = 0;
        if (channel_enabled[ch0]) cnf_ch_enabled |= CHANNEL0;
        if (channel_enabled[ch1]) cnf_ch_enabled |= CHANNEL1;
        if (channel_enabled[ch2]) cnf_ch_enabled |= CHANNEL2;
        if (channel_enabled[ch3]) cnf_ch_enabled |= CHANNEL3;

        if (cnf_ch_enabled == 0) continue; // do not configure the card if no inputs are enabled
        dwError += spcm_dwSetParam_i32(hDrv[card_index], SPC_CHENABLE, cnf_ch_enabled);
		
        // set input range
        dwError += spcm_dwSetParam_i32(hDrv[card_index], SPC_AMP0, channel_adc_range[ch0]);
        dwError += spcm_dwSetParam_i32(hDrv[card_index], SPC_AMP1, channel_adc_range[ch1]);
        dwError += spcm_dwSetParam_i32(hDrv[card_index], SPC_AMP2, channel_adc_range[ch2]);
        dwError += spcm_dwSetParam_i32(hDrv[card_index], SPC_AMP3, channel_adc_range[ch3]);

        // set input offset
        dwError += spcm_dwSetParam_i32(hDrv[card_index], SPC_OFFS0, channel_ofs[ch0]);
        dwError += spcm_dwSetParam_i32(hDrv[card_index], SPC_OFFS1, channel_ofs[ch1]);
        dwError += spcm_dwSetParam_i32(hDrv[card_index], SPC_OFFS2, channel_ofs[ch2]);
        dwError += spcm_dwSetParam_i32(hDrv[card_index], SPC_OFFS3, channel_ofs[ch3]);

        // enable AC coupling
        dwError += spcm_dwSetParam_i32(hDrv[card_index], SPC_ACDC0, ADC_AC);
        dwError += spcm_dwSetParam_i32(hDrv[card_index], SPC_ACDC1, ADC_AC);
        dwError += spcm_dwSetParam_i32(hDrv[card_index], SPC_ACDC2, ADC_AC);
        dwError += spcm_dwSetParam_i32(hDrv[card_index], SPC_ACDC3, ADC_AC);

        // enable input offset compensation
        dwError += spcm_dwSetParam_i32(hDrv[card_index], SPC_ACDC_OFFS_COMPENSATION0, ADC_OFS_COMP);
        dwError += spcm_dwSetParam_i32(hDrv[card_index], SPC_ACDC_OFFS_COMPENSATION1, ADC_OFS_COMP);
        dwError += spcm_dwSetParam_i32(hDrv[card_index], SPC_ACDC_OFFS_COMPENSATION2, ADC_OFS_COMP);
        dwError += spcm_dwSetParam_i32(hDrv[card_index], SPC_ACDC_OFFS_COMPENSATION3, ADC_OFS_COMP);

        // configure readout mode - acquisition to onboard memory
        dwError += spcm_dwSetParam_i32(hDrv[card_index], SPC_CARDMODE, SPC_REC_STD_SINGLE); // single standard mode

        // configure pre and post trigger memory
        dwError += spcm_dwSetParam_i32(hDrv[card_index], SPC_MEMSIZE, ADC_EVENT_SIZE);
        dwError += spcm_dwSetParam_i32(hDrv[card_index], SPC_POSTTRIGGER, ADC_EVENT_SIZE - ADC_PRETRIGGER );

        // set clock mode to internal , no clock output
        dwError += spcm_dwSetParam_i32(hDrv[card_index], SPC_CLOCKMODE, SPC_CM_INTPLL);
        dwError += spcm_dwSetParam_i32(hDrv[card_index], SPC_CLOCKOUT, 0);

        // set card to maximum sampling rate
        int64 llMaxSamplingrate = 0;
        spcm_dwGetParam_i64(hDrv[card_index], SPC_PCISAMPLERATE, &llMaxSamplingrate);
        spcm_dwSetParam_i64(hDrv[card_index], SPC_SAMPLERATE, llMaxSamplingrate);
        SampleRate = llMaxSamplingrate;

        // set card trigger
        dwError += spcm_dwSetParam_i32(hDrv[card_index], SPC_TRIG_ORMASK, SPC_TMASK_NONE); // disable software trigger
        dwError += spcm_dwSetParam_i32(hDrv[card_index], SPC_TRIG_ANDMASK, 0);

        if ( channel_enabled [card_index * 4 ] ) { 
	    if ( channel_trig_pol [card_index * 4] == 3) dwError += spcm_dwSetParam_i32 (hDrv[card_index], SPC_TRIG_CH0_MODE, SPC_TM_BOTH); //set trigger on both edges
	    if ( channel_trig_pol [card_index * 4] == 2) dwError += spcm_dwSetParam_i32 (hDrv[card_index], SPC_TRIG_CH0_MODE, SPC_TM_POS); //set trigger on positive edge
	    if ( channel_trig_pol [card_index * 4] == 1) dwError += spcm_dwSetParam_i32 (hDrv[card_index], SPC_TRIG_CH0_MODE, SPC_TM_NEG); //set trigger on negative edge
	    if ( channel_trig_pol [card_index * 4] == 0) dwError += spcm_dwSetParam_i32 (hDrv[card_index], SPC_TRIG_CH0_MODE, SPC_TM_NONE); //disable trigger on this channel
	    dwError += spcm_dwSetParam_i32 (hDrv[card_index], SPC_TRIG_CH0_LEVEL0, channel_thr [ch0] * 127 / channel_adc_range[ch0]); //set trigger level
		} else 
			dwError += spcm_dwSetParam_i32 (hDrv[card_index], SPC_TRIG_CH0_MODE, SPC_TM_NONE); // disable trigger from channel 0
			
		if ( channel_enabled [card_index * 4 + 1] ) {
			if ( channel_trig_pol [card_index * 4 + 1] == 3) dwError += spcm_dwSetParam_i32 (hDrv[card_index], SPC_TRIG_CH1_MODE, SPC_TM_BOTH); //set trigger on both edges
			if ( channel_trig_pol [card_index * 4 + 1] == 2) dwError += spcm_dwSetParam_i32 (hDrv[card_index], SPC_TRIG_CH1_MODE, SPC_TM_POS); //set trigger on positive edge
			if ( channel_trig_pol [card_index * 4 + 1] == 1) dwError += spcm_dwSetParam_i32 (hDrv[card_index], SPC_TRIG_CH1_MODE, SPC_TM_NEG); //set trigger on negative edge
			if ( channel_trig_pol [card_index * 4 + 1] == 0) dwError += spcm_dwSetParam_i32 (hDrv[card_index], SPC_TRIG_CH1_MODE, SPC_TM_NONE); //disable trigger on this channel
			dwError += spcm_dwSetParam_i32 (hDrv[card_index], SPC_TRIG_CH1_LEVEL0, channel_thr [ch1] * 127 / channel_adc_range[ch1]); //set trigger level
		} else 
			dwError += spcm_dwSetParam_i32 (hDrv[card_index], SPC_TRIG_CH1_MODE, SPC_TM_NONE); // disable trigger from channel 1
		
		if ( channel_enabled [card_index * 4 + 2] ) {
			if ( channel_trig_pol [card_index * 4 + 2] == 3) dwError += spcm_dwSetParam_i32 (hDrv[card_index], SPC_TRIG_CH2_MODE, SPC_TM_BOTH); //set trigger on both edges
			if ( channel_trig_pol [card_index * 4 + 2] == 2) dwError += spcm_dwSetParam_i32 (hDrv[card_index], SPC_TRIG_CH2_MODE, SPC_TM_POS); //set trigger on positive edge
			if ( channel_trig_pol [card_index * 4 + 2] == 1) dwError += spcm_dwSetParam_i32 (hDrv[card_index], SPC_TRIG_CH2_MODE, SPC_TM_NEG); //set trigger on negative edge
			if ( channel_trig_pol [card_index * 4 + 2] == 0) dwError += spcm_dwSetParam_i32 (hDrv[card_index], SPC_TRIG_CH2_MODE, SPC_TM_NONE); //disable trigger on this channel
			dwError += spcm_dwSetParam_i32 (hDrv[card_index], SPC_TRIG_CH2_LEVEL0, channel_thr [ch2] * 127 / channel_adc_range[ch2]); //set trigger level
		} else 
			dwError += spcm_dwSetParam_i32 (hDrv[card_index], SPC_TRIG_CH2_MODE, SPC_TM_NONE); // disable trigger from channel 2
		
		if ( channel_enabled [card_index * 4 + 3] ) {
			if ( channel_trig_pol [card_index * 4 + 3] == 3) dwError += spcm_dwSetParam_i32 (hDrv[card_index], SPC_TRIG_CH3_MODE, SPC_TM_BOTH); //set trigger on both edges
			if ( channel_trig_pol [card_index * 4 + 3] == 2) dwError += spcm_dwSetParam_i32 (hDrv[card_index], SPC_TRIG_CH3_MODE, SPC_TM_POS); //set trigger on positive edge
			if ( channel_trig_pol [card_index * 4 + 3] == 1) dwError += spcm_dwSetParam_i32 (hDrv[card_index], SPC_TRIG_CH3_MODE, SPC_TM_NEG); //set trigger on negative edge
			if ( channel_trig_pol [card_index * 4 + 3] == 0) dwError += spcm_dwSetParam_i32 (hDrv[card_index], SPC_TRIG_CH3_MODE, SPC_TM_NONE); //disable trigger on this channel
			dwError += spcm_dwSetParam_i32 (hDrv[card_index], SPC_TRIG_CH3_LEVEL0, channel_thr [ch3] * 127 / channel_adc_range[ch3]); //set trigger level	
		} else 
			dwError += spcm_dwSetParam_i32 (hDrv[card_index], SPC_TRIG_CH3_MODE, SPC_TM_NONE); // disable trigger from channel 3
		

		dwError += spcm_dwSetParam_i32 (hDrv[card_index], SPC_TRIG_CH_ORMASK0,  SPC_TMASK0_CH0 | SPC_TMASK0_CH1 | SPC_TMASK0_CH2 | SPC_TMASK0_CH3); // enable hardware trigger

		dwError += spcm_dwSetParam_i32 (hDrv[card_index], SPC_TIMEOUT, 3000); // enable 3 second timeout to allow the threads to exit gracefully

        dwError += spcm_dwSetParam_i32(hDrv[card_index],
                                       SPC_TRIG_CH_ORMASK0,
                                       SPC_TMASK0_CH0 | SPC_TMASK0_CH1 | SPC_TMASK0_CH2 | SPC_TMASK0_CH3);

        dwError += spcm_dwSetParam_i32(hDrv[card_index], SPC_TIMEOUT, 3000);

        if (dwError) {
            spcm_dwGetErrorInfo_i64(hDrv[card_index], NULL, NULL, szErrorBuf);
            std::cout << "Card" << std::to_string(card_index) << " configuration error :" << std::string(szErrorBuf) << std::endl;
            return (0);
        }
    }


/*
    cout << "Enable card synchronization" << endl;
    uint32_t dwError = spcm_dwSetParam_i32 (hDrv[CARD_NUMBER], SPC_SYNC_ENABLEMASK ,  0x000F); // enable synchronization for all cards
    if (dwError) {
        spcm_dwGetErrorInfo_i64 (hDrv[CARD_NUMBER] , NULL , NULL , szErrorBuf);
        cout << "Starhub configuration error :" << string(szErrorBuf) << endl;
        return (0);
    }
*/  
    return (1);
}

// ***********************************************************************

void start_spectrum_acq () { // start data acuistion using starhub
   /* char szErrorBuf[ERRORTEXTLEN];

    uint32_t dwError = spcm_dwSetParam_i32 ( hDrv[CARD_NUMBER] , SPC_M2CMD, M2CMD_CARD_START | M2CMD_CARD_ENABLETRIGGER ); // start acq via starhub
    if (dwError) {
        spcm_dwGetErrorInfo_i64 (hDrv[CARD_NUMBER] , NULL , NULL , szErrorBuf);
        cout << "Starhub error :" << string(szErrorBuf) << endl;
    }
*/
}

void stop_spectrum_acq () {// stop data acuistion using starhub
/*    char szErrorBuf[ERRORTEXTLEN];
    uint32_t dwError = spcm_dwSetParam_i32 ( hDrv[CARD_NUMBER] , SPC_M2CMD, M2CMD_CARD_STOP ); // stop card via starhub
    if (dwError) {
        spcm_dwGetErrorInfo_i64 (hDrv[CARD_NUMBER] , NULL , NULL , szErrorBuf);
        cout << "Starhub error :" << string(szErrorBuf) << endl;
    }
*/
}


void close_spectrum_device() {
    for (int card_index = 0; card_index < CARD_NUMBER; card_index++)
        if (hDrv[card_index]) {
            spcm_vClose(hDrv[card_index]);
            hDrv[card_index] = NULL;
        }
}

// ***********************************************************************
// helper function and save functions: OUTSIDE of acq_loop
// ***********************************************************************
static inline void getEnabledChansForCard(int card_idx, int enabledIdx[4], int& nEnabled) {
    nEnabled = 0;
    for (int i = 0; i < 4; ++i) {
        if (channel_enabled[card_idx * 4 + i]) {
            enabledIdx[nEnabled++] = i;
        }
    }
}


static int64_t now_ns()
{
    using namespace std::chrono;
    auto tp = system_clock::now();
    auto ns = duration_cast<nanoseconds>(tp.time_since_epoch()).count();
    return static_cast<int64_t>(ns);
}


void start_run_visual_timer()
{
    g_run_vis_start_ns.store(now_ns(), std::memory_order_relaxed);
}

double get_run_visual_elapsed_seconds()
{
    int64_t t0 = g_run_vis_start_ns.load(std::memory_order_relaxed);
    if (t0 <= 0) return 0.0;

    int64_t dt = now_ns() - t0;
    if (dt <= 0) return 0.0;

    return double(dt) * 1e-9;
}


static FileHeader make_file_header()
{
    FileHeader h{};
    std::memset(&h, 0, sizeof(h));

    // magic and version
    const char magic_str[8] = { 'W','F','v','1','R','A','W', '\0' };
    std::memcpy(h.magic, magic_str, sizeof(magic_str));
    h.version = 1;

    // channel info
    h.n_channels   = 0;
    h.channel_mask = 0;
    for (int ch = 0; ch < CHANNEL_NUMBER; ++ch) {
        if (channel_enabled[ch]) {
            h.channel_mask |= (1u << ch);
            ++h.n_channels;
        }
    }

    // sampling info
    h.sample_rate_hz = static_cast<double>(SampleRate);
    h.dt_ns          = (SampleRate > 0) ? (1e9 / static_cast<double>(SampleRate)) : 0.0;

    // ADC info
    h.adc_range_mV  = static_cast<double>(ADC_RANGE);  // assuming ADC_RANGE is in mV
    h.adc_bits      = 8;                               // your card
    h.sample_format = 1;                               // 1 = signed 8-bit (likely for bipolar ADC)

    h.adc_event_size = static_cast<uint32_t>(ADC_EVENT_SIZE);
    h.adc_pretrigger = static_cast<uint32_t>(ADC_PRETRIGGER);

    // timestamps
    h.file_start_time_ns = now_ns();
    h.file_end_time_ns   = 0;  // will be filled when file is closed cleanly

    // reserved already zeroed by memset
    return h;
}

bool open_run_file(const std::string& filename)
{
    if (g_run_file_open) {
        g_run_file.close();
        g_run_file_open = false;
    }

    g_run_file.open(filename, std::ios::binary | std::ios::trunc);
    if (!g_run_file) {
        return false;
    }

    g_file_header = make_file_header();
    g_run_file.write(reinterpret_cast<const char*>(&g_file_header),
                     sizeof(g_file_header));
    if (!g_run_file) {
        g_run_file.close();
        g_run_file_open = false;
        return false;
    }

    g_run_file_open = true;
    return true;
}

bool flush_channel_buffer_to_file(int ch)
{
    if (!g_run_file_open)
        return false;

    uint32_t count = waveform_archive_count[ch];
    if (count == 0)
        return false;
    //std::lock_guard<std::mutex> lock(g_file_mutex);

    BufferHeader bh{};
    bh.magic      = BufferHeader::MAGIC_START;
    bh.channel_id = static_cast<uint16_t>(ch);
    bh.reserved0  = 0;
    bh.n_events   = count;

    g_run_file.write(reinterpret_cast<const char*>(&bh), sizeof(bh));

    uint32_t cap    = WF_ARCHIVE_CAPACITY;
    uint32_t write  = waveform_archive_write_index[ch];
    uint32_t oldest = (write + cap - count) % cap;

    for (uint32_t n = 0; n < count; ++n) {
        uint32_t slot = (oldest + n) % cap;

        EventHeader eh{};
        eh.timestamp_ns = waveform_archive_time[ch][slot];
        g_run_file.write(reinterpret_cast<const char*>(&eh), sizeof(eh));

        g_run_file.write(
            reinterpret_cast<const char*>(waveform_archive_raw[ch][slot]),
            ADC_EVENT_SIZE
        );
    }

    BufferTrailer bt{};
    bt.magic = BufferTrailer::MAGIC_END;
    g_run_file.write(reinterpret_cast<const char*>(&bt), sizeof(bt));
    g_run_file.flush();

    // reset buffer for this channel
    waveform_archive_count[ch]       = 0;
    waveform_archive_write_index[ch] = 0;
    return true;
}

void flush_all_buffers_to_file()
{
    if (!g_run_file_open)
        return;

    for (int ch = 0; ch < CHANNEL_NUMBER; ++ch) {
        flush_channel_buffer_to_file(ch);
    }
}

void close_run_file()
{
    if (!g_run_file_open)
        return;

    // final flush in case user forgot
    flush_all_buffers_to_file();

    //write file trailer
    FileTrailer ft{};
    ft.magic = FileTrailer::MAGIC_END_FILE;
    g_run_file.write(reinterpret_cast<const char*>(&ft), sizeof(ft));

    // update end time in header
    std::streampos pos = g_run_file.tellp();

    g_file_header.file_end_time_ns = now_ns();
    g_run_file.seekp(0);
    g_run_file.write(reinterpret_cast<const char*>(&g_file_header),
                     sizeof(g_file_header));

    g_run_file.seekp(pos);
    g_run_file.close();
    g_run_file_open = false;
}



// ***********************************************************************
// Energy Filter
// ***********************************************************************

double energy_filter(const int8_t* in, //Input waveform
                     int           N, // Dimension of the waveform (ADC_EVENT_SIZE)
                     int           pretrigger,//Dimension of pretrigger (ADC_PRETRIGGER)
                     int           window_size, //Size of the moving average window
                     int           n_passes, // Number of moving averages to perfrom
                     float*        buf1, //Preallocated working buffers
                     float*        buf2)
{
    if (!in || !buf1 || !buf2 || N <= 0 || window_size <= 0 || n_passes <= 0)
        return 0.0;

    // Clamp window to [1, N]
    int W = window_size;
    if (W > N) W = N;

    // Compute baseline length once
    int baseline_len = static_cast<int>(0.8 * static_cast<double>(pretrigger));
    if (baseline_len < 1) baseline_len = 1;
    if (baseline_len > N) baseline_len = N;

    // Copy input to first buffer
    for (int i = 0; i < N; ++i)
        buf1[i] = static_cast<float>(in[i]);

    float* src = buf1;
    float* dst = buf2;

    // Apply (n_passes - 1) standard moving-average passes
    for (int pass = 0; pass < n_passes - 1; ++pass) {
        double sum = 0.0;

        // First W samples: growing window
        for (int i = 0; i < W && i < N; ++i) {
            sum += src[i];
            dst[i] = static_cast<float>(sum / static_cast<double>(i + 1));
        }

        // Remaining samples: full window
        for (int i = W; i < N; ++i) {
            sum += src[i];
            sum -= src[i - W];
            dst[i] = static_cast<float>(sum / static_cast<double>(W));
        }

        std::swap(src, dst);
    }

    // Last pass: compute moving average, baseline & max in one go
    double sum = 0.0;
    double baseline_sum = 0.0;
    double max_val;

    // First sample handled separately so we can init max_val
    if (N > 0) {
        // i = 0
        sum += src[0];
        dst[0] = static_cast<float>(sum); // window size = 1 here

        if (baseline_len > 0)
            baseline_sum += dst[0];

        max_val = dst[0];
    } else {
        return 0.0;
    }

    // i from 1 to W-1 (growing window)
    for (int i = 1; i < W && i < N; ++i) {
        sum += src[i];
        double v = sum / static_cast<double>(i + 1);
        dst[i] = static_cast<float>(v);

        if (i < baseline_len)
            baseline_sum += v;

        if (v > max_val)
            max_val = v;
    }

    // i from W to N-1 (full window size W)
    for (int i = W; i < N; ++i) {
        sum += src[i];
        sum -= src[i - W];
        double v = sum / static_cast<double>(W);
        dst[i] = static_cast<float>(v);

        if (i < baseline_len)
            baseline_sum += v;

        if (v > max_val)
            max_val = v;
    }

    double baseline = baseline_sum / static_cast<double>(baseline_len);

    return max_val - baseline;
}


bool software_trigger_pass(int chGlobal,const int8_t* src,int nEnabled,int k)
{
    int pre  = channel_search_pre[chGlobal];
    int post = channel_search_post[chGlobal];

    // Compute search window in sample indices
    int start = ADC_PRETRIGGER - pre;
    if (start < 0) start = 0;

    int end = ADC_PRETRIGGER + post;
    if (end > ADC_EVENT_SIZE) end = ADC_EVENT_SIZE;

    int thr = channel_thr[chGlobal] * 127 / channel_adc_range[chGlobal];       // threshold in ADC counts
    int pol = channel_trig_pol[chGlobal];  // 1 = neg, 2 = pos, 3 = both

    if (thr <= 0 || start >= end)
        return true; // trivial: no threshold or empty window → accept

    bool fired = false;

    for (int j = start; j < end; ++j) {
        std::size_t sampleIndex = static_cast<std::size_t>(j) * nEnabled + k;
        int s = src[sampleIndex];  // int8_t → [-128, 127]

        // positive polarity: pulse above +thr
        if ((pol & 0x2) && s >= thr) {
            fired = true;
            break;
        }
        // negative polarity: pulse below -thr
        if ((pol & 0x1) && s <= -thr) {
            fired = true;
            break;
        }
    }

    return fired;
}

void clear_energy_spectra()
{
    for (int ch = 0; ch < CHANNEL_NUMBER; ++ch) {
        for (int b = 0; b < ENERGY_NBINS; ++b) {
            energy_spectrum[ch][b] = 0;
        }
    }
}




// ***********************************************************************
// acquisition loop
// ***********************************************************************
void* acq_loop(void* arg) {
    char szErrorBuf[ERRORTEXTLEN];
    int card_idx = *(uint32_t*)arg;
    thread_lock_flag[card_idx] = 0;

    int enabledIdx[4];
    int nEnabled = 0;
    



    const int bytesPerSample = 1;
    void* pvBuffer           = nullptr;
    size_t dmaBytes = 0;
    int    nEnabledForBuffer = 0;

    //std::ofstream test_file("test.dat");
    std::cout << "acq loop start" << std::endl;

    static float energy_buf1[ADC_EVENT_SIZE];
    static float energy_buf2[ADC_EVENT_SIZE];
    double E = 0.0;

    while (1) {
        getEnabledChansForCard(card_idx, enabledIdx, nEnabled);

        if ((connection_status != CONN_RUNNING) || nEnabled == 0) {
            thread_lock_flag[card_idx] = 0;

            if (pvBuffer) {
                vFreeMemPageAligned(pvBuffer, dmaBytes);
                pvBuffer           = nullptr;
                dmaBytes  = 0;
                nEnabledForBuffer  = 0;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        thread_lock_flag[card_idx] = 1;

        if (!pvBuffer || nEnabled != nEnabledForBuffer) {
            if (pvBuffer) {
                vFreeMemPageAligned(pvBuffer, dmaBytes);
            }

            nEnabledForBuffer = nEnabled;
            if (nEnabledForBuffer <= 0) nEnabledForBuffer = 1;

            dmaBytes = (size_t)ADC_EVENT_SIZE * nEnabledForBuffer * bytesPerSample;
            pvBuffer = pvAllocMemPageAligned(dmaBytes);

            if (!pvBuffer) {
                std::cout << "card " << card_idx << " DMA buffer alloc failed" << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
        }


        uint32_t dwError = 0;
        dwError += spcm_dwSetParam_i32(hDrv[card_idx],
                                       SPC_M2CMD,
                                       M2CMD_CARD_START | M2CMD_CARD_ENABLETRIGGER | M2CMD_CARD_WAITREADY);

        if (dwError == ERR_TIMEOUT) {
            spcm_dwSetParam_i32(hDrv[card_idx], SPC_M2CMD, M2CMD_CARD_STOP);
            continue;
        }

        dwError += spcm_dwDefTransfer_i64(hDrv[card_idx],
                                          SPCM_BUF_DATA, SPCM_DIR_CARDTOPC,
                                          0, pvBuffer, 0, dmaBytes);
        dwError += spcm_dwSetParam_i32(hDrv[card_idx],
                                       SPC_M2CMD,
                                       M2CMD_DATA_STARTDMA | M2CMD_DATA_WAITDMA);

        if (dwError) {
            spcm_dwGetErrorInfo_i64(hDrv[card_idx], NULL, NULL, szErrorBuf);
            std::cout << "Card" << card_idx << " acq error :" << std::string(szErrorBuf) << std::endl;
            continue;
        }
        
        // de-interleave
        const int8_t* src = reinterpret_cast<const int8_t*>(pvBuffer);
        for (int k = 0; k < nEnabledForBuffer; ++k) {
            int chLocal = enabledIdx[k];
            int chGlobal = card_idx * 4 + chLocal;
            
            // Software trigger to check if there is actually  a trigger in the waveform. Done because the card acquires all 4 channels even though the trigger is just in one
             if (!software_trigger_pass(chGlobal, src, nEnabledForBuffer, k)) {
                //std::cout << "Waveform discarded ch: "<< chGlobal << std::endl;
                continue;
            }
            
            uint32_t idx = waveform_archive_write_index[chGlobal];
            if (idx >= WF_ARCHIVE_CAPACITY) {
                idx = 0;
                waveform_archive_write_index[chGlobal] = 0;
            }
            
            // timestamp for this waveform
            waveform_archive_time[chGlobal][idx] = now_ns();

            // copy all ADC_EVENT_SIZE samples for this channel
            for (int j = 0; j < ADC_EVENT_SIZE; ++j) {
                std::size_t sampleIndex = static_cast<std::size_t>(j) * nEnabledForBuffer + k;
                waveform_archive_raw[chGlobal][idx][j] = src[sampleIndex];
            }

            //apply energy filter
            E = energy_filter(
                waveform_archive_raw[chGlobal][idx],
                ADC_EVENT_SIZE,
                ADC_PRETRIGGER,
                500,   
                3,       
                energy_buf1,
                energy_buf2
            );

            // Energy in ADC counts; ignore negative energies
            if (E > 0.0) {
                double E1 = E / 127 * channel_adc_range[chGlobal];
                // Clamp to histogram range
                double e_clamped = (E1 >= ENERGY_MAX) ? (ENERGY_MAX - 1e-6) : E1;

                int bin = static_cast<int>(
                    (e_clamped - ENERGY_MIN) * (ENERGY_NBINS / (ENERGY_MAX - ENERGY_MIN))
                );

                if (bin >= 0 && bin < ENERGY_NBINS) {
                    energy_spectrum[chGlobal][bin] += 1;
                }
            }

            waveform_archive_write_index[chGlobal] = (idx + 1) % WF_ARCHIVE_CAPACITY;
            if (waveform_archive_count[chGlobal] < WF_ARCHIVE_CAPACITY) {
                ++waveform_archive_count[chGlobal];
            }
            
            //Saving to file if the file is open and a channel buffer is full
            if (g_run_file_open && waveform_archive_count[chGlobal] >= WF_ARCHIVE_CAPACITY) {
                flush_channel_buffer_to_file(chGlobal);
            }

            //Decimation of the waveform for visualization

            for (int j = 0; j < ADC_EVENT_SIZE; j+=ADC_EVENT_SIZE/2048) {
		    display_data [chGlobal][2048*j/ADC_EVENT_SIZE] = *((char*)pvBuffer +  j*nEnabledForBuffer + k);

            }
            display_refresh_flags[chGlobal] = 1;


        }
    }
}

