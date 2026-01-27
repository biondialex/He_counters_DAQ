#include <fstream>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <iostream>

struct FileHeaderALL {
    char     magic[8];      // "WFv1ALL\0"
    uint32_t version;
    uint32_t n_channels;
    double   sample_rate_hz;
    double   dt_ns;
    uint64_t reserved;
};

struct ChannelHeader {
    uint32_t ch_id;
    uint32_t reserved;
    uint64_t num_events;
};

struct ChannelData {
    uint32_t ch_id = 0;
    std::vector<std::vector<int16_t>> events; // events[e][sample]
};

struct BinAll {
    double fs_hz = 0.0;
    double dt_ns = 1.0;
    std::vector<ChannelData> channels; // size = number of present channels
};

static BinAll load_bin_all(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open " + path);

    FileHeaderALL fh{};
    in.read(reinterpret_cast<char*>(&fh), sizeof(fh));
    if (!in) throw std::runtime_error("File too small for header");
    if (std::string(fh.magic, 7) != "WFv1ALL")
        throw std::runtime_error("Bad magic (not WFv1ALL)");
    if (fh.version != 1)
        throw std::runtime_error("Unsupported version");

    BinAll out;
    out.fs_hz = fh.sample_rate_hz;
    out.dt_ns = fh.dt_ns;

    out.channels.reserve(fh.n_channels);

    for (uint32_t k = 0; k < fh.n_channels; ++k) {
        ChannelHeader ch{};
        in.read(reinterpret_cast<char*>(&ch), sizeof(ch));
        if (!in) throw std::runtime_error("Truncated channel header");

        ChannelData cd;
        cd.ch_id = ch.ch_id;
        cd.events.reserve(static_cast<size_t>(ch.num_events));

        for (uint64_t e = 0; e < ch.num_events; ++e) {
            uint32_t ns = 0;
            in.read(reinterpret_cast<char*>(&ns), sizeof(ns));
            if (!in) throw std::runtime_error("Truncated nsamples");
            std::vector<int16_t> samples(ns);
            if (ns) {
                in.read(reinterpret_cast<char*>(samples.data()), ns * sizeof(int16_t));
                if (!in) throw std::runtime_error("Truncated samples block");
            }
            cd.events.emplace_back(std::move(samples));
        }

        out.channels.emplace_back(std::move(cd));
    }

    return out;
}

// ROOT entrypoint: loads and draws up to first N events per channel
// void waveform_from_bin_file(const char* filename="run_test.bin", int maxEventsPerChannel=5) {
//     auto data = load_bin_all(filename);

//     std::cout << "[INFO] Loaded " << filename
//               << "  fs=" << data.fs_hz << " Hz  dt=" << data.dt_ns << " ns\n";
//     for (const auto& ch : data.channels) {
//         std::cout << "  ch " << ch.ch_id << " : " << ch.events.size() << " events\n";
//     }

//     if (data.channels.empty()) { std::cout << "No channels.\n"; return; }

//     // Plot: one pad per channel, overlay up to maxEventsPerChannel in one color
//     auto* c = new TCanvas("c_all", "WF ALL", 1200, 300 * (int)data.channels.size());
//     c->Divide(2,2);

//     std::vector<int> cols = { kBlue, kRed, kGreen+2, kMagenta+1, kOrange+7, kCyan+1 };
//     for (size_t ich = 0; ich < data.channels.size(); ++ich) {
//         const auto& ch = data.channels[ich];
//         c->cd((int)ich+1);

//         auto* mg = new TMultiGraph();
//         auto* leg = new TLegend(0.75, 0.70, 0.99, 0.99);
//         leg->SetBorderSize(0);

//         const int baseColor = cols[ich % cols.size()];
//         const int nEv = std::min<int>((int)ch.events.size(), maxEventsPerChannel);

//         for (int e = 0; e < nEv; ++e) {
//             const auto& s = ch.events[e];
//             if (s.empty()) continue;

//             std::vector<double> x(s.size()), y(s.size());
//             for (size_t i = 0; i < s.size(); ++i) {
//                 x[i] = i * data.dt_ns;
//                 y[i] = (double)s[i];
//             }
//             auto* g = new TGraph((int)s.size(), x.data(), y.data());
//             g->SetLineColor(baseColor);
//             g->SetLineWidth(1);
//             mg->Add(g, "L");
//             if (e < 8) leg->AddEntry(g, Form("evt %d", e), "l");
//         }

//         mg->SetTitle(Form("Channel %u;Time [ns];Amplitude [ADC]", ch.ch_id));
//         mg->Draw("A");
//         leg->Draw();
//         gPad->Update();
//     }
//     c->Update();
// }
void waveform_from_bin_file(const char* filename,
                              int samplePerChannel=100, // draw up to this many
                              int seed=12345)
{
    auto data = load_bin_all(filename);
    gStyle->SetOptStat(0);

    std::vector<int> cols = { kBlue, kRed, kGreen+2, kMagenta+1, kOrange+7, kCyan+1 };
#if ROOT_VERSION_CODE >= ROOT_VERSION(6,14,00)
    bool haveAlpha = true;
#else
    bool haveAlpha = false;
#endif

    TCanvas* c = new TCanvas("c_ov","Sampled overlay",1200,300*(int)data.channels.size());
    c->Divide(1,(int)data.channels.size());

    std::mt19937 rng(seed);

    for (size_t ich=0; ich<data.channels.size(); ++ich) {
        const auto& ch = data.channels[ich];
        c->cd((int)ich+1);

        auto* mg = new TMultiGraph();
        auto* leg = new TLegend(0.75,0.70,0.99,0.99);
        leg->SetBorderSize(0);

        // choose which events to draw (random if too many)
        std::vector<int> idx(ch.events.size());
        std::iota(idx.begin(), idx.end(), 0);
        if ((int)idx.size() > samplePerChannel) {
            std::shuffle(idx.begin(), idx.end(), rng);
            idx.resize(samplePerChannel);
        }

        double dt = (data.dt_ns > 0 ? data.dt_ns : 1.0);
        int baseColor = cols[ich % cols.size()];

        for (int e : idx) {
            const auto& s = ch.events[e];
            if (s.empty()) continue;

            std::vector<double> x(s.size()), y(s.size());
            for (size_t i=0; i<s.size(); ++i) { x[i]=i*dt; y[i]=(double)s[i]; }

            auto* g = new TGraph((int)s.size(), x.data(), y.data());
            g->SetLineWidth(1);
            if (haveAlpha) g->SetLineColorAlpha(baseColor, 0.25);
            else           g->SetLineColor(baseColor);
            mg->Add(g, "L");
        }

        mg->SetTitle(Form("Channel %u;Time [ns];Amplitude [ADC]", ch.ch_id));
        mg->Draw("A");
        leg->AddEntry((TObject*)0, Form("drawn %zu/%zu",
                        std::min((size_t)samplePerChannel, ch.events.size()),
                        ch.events.size()), "");
        leg->Draw();
        gPad->Update();
    }
    c->Update();
}
