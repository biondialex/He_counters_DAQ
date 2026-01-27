#ifndef WAVEFORM_FILE_FORMAT_H
#define WAVEFORM_FILE_FORMAT_H

#include <cstdint>

#pragma pack(push, 1)

struct FileHeader {
    char     magic[8] = "WFv1RAM";
    uint32_t version;

    uint32_t n_channels;
    uint32_t channel_mask;

    double   sample_rate_hz;
    double   dt_ns;

    double   adc_range_mV;

    uint32_t adc_bits;
    uint32_t sample_format;

    uint32_t adc_event_size;   // from ADC_EVENT_SIZE
    uint32_t adc_pretrigger;   // from ADC_PRETRIGGER

    int64_t  file_start_time_ns;
    int64_t  file_end_time_ns;

    uint64_t reserved[2];
};

struct BufferHeader {
    uint32_t magic;            // must be MAGIC_START
    uint16_t channel_id;
    uint16_t reserved0;
    uint32_t n_events;

    static constexpr uint32_t MAGIC_START = 0xCAFEBABE;
};

struct EventHeader {
    int64_t timestamp_ns;
};

struct BufferTrailer {
    uint32_t magic;            // must be MAGIC_END
    static constexpr uint32_t MAGIC_END = 0xDEADBEEF;
};


struct FileTrailer {
    uint32_t magic;                           // must be FILE_MAGIC_END
    static constexpr uint32_t MAGIC_END_FILE = 0xFEEDFACE;
};
#pragma pack(pop)


#endif // WAVEFORM_FILE_FORMAT_H
