// #include <memory>         // for std::unique_ptr
// #include <TCanvas.h>
// #include <TMultiGraph.h>
// #include <TGraph.h>
// #include <TLegend.h>
// #include <TROOT.h>
// #include <TStyle.h>

// struct FileHeaderALL {
//     char     magic[8];      // "WFv1ALL\0"
//     uint32_t version;
//     uint32_t n_channels;
//     double   sample_rate_hz;
//     double   dt_ns;
//     uint64_t reserved;
// };
// struct ChannelHeaderALL {
//     uint32_t ch_id;
//     uint32_t reserved;
//     uint64_t num_events;
// };

// static std::string basename_str(const std::string& p) {
//     auto pos = p.find_last_of("/\\");
//     return (pos == std::string::npos) ? p : p.substr(pos+1);
// }

// static int guess_channel_from_name(const std::string& path) {
//     std::smatch m;
//     std::regex re("_ch([0-9]+)\\.bin$");
//     if (std::regex_search(path, m, re)) return std::stoi(m[1]);
//     return 0; // default
// }

// struct ChannelData {
//     uint32_t ch_id = 0;
//     std::vector<std::vector<int16_t>> events; // events[e][sample]
// };

// struct Loaded {
//     std::string fmt;   // "ALL", "BIN", or "RAW"
//     double fs_hz = 0.0;
//     double dt_ns = 1.0;
//     std::vector<ChannelData> channels; // for BIN/RAW, size==1
// };

// static Loaded load_auto_bin(const std::string& path) {
//     std::ifstream in(path, std::ios::binary);
//     if (!in) throw std::runtime_error("Cannot open " + path);

//     // Peek first 8 bytes
//     char magic8[8] = {0};
//     in.read(magic8, 8);
//     if (!in) throw std::runtime_error("File too small");

//     Loaded out;

//     // --- Case 1: WFv1ALL ---
//     if (std::string(magic8, 7) == "WFv1ALL") {
//         out.fmt = "ALL";
//         in.seekg(0);
//         FileHeaderALL fh{};
//         in.read(reinterpret_cast<char*>(&fh), sizeof(fh));
//         if (std::string(fh.magic, 7) != "WFv1ALL")
//             throw std::runtime_error("Bad WFv1ALL header");
//         out.fs_hz = fh.sample_rate_hz;
//         out.dt_ns = fh.dt_ns;

//         out.channels.reserve(fh.n_channels);
//         for (uint32_t i = 0; i < fh.n_channels; ++i) {
//             ChannelHeaderALL ch{};
//             in.read(reinterpret_cast<char*>(&ch), sizeof(ch));
//             ChannelData cd; cd.ch_id = ch.ch_id;
//             cd.events.reserve(static_cast<size_t>(ch.num_events));
//             for (uint64_t e = 0; e < ch.num_events; ++e) {
//                 uint32_t ns=0;
//                 in.read(reinterpret_cast<char*>(&ns), sizeof(ns));
//                 std::vector<int16_t> samples(ns);
//                 if (ns) in.read(reinterpret_cast<char*>(samples.data()), ns * sizeof(int16_t));
//                 cd.events.emplace_back(std::move(samples));
//             }
//             out.channels.emplace_back(std::move(cd));
//         }
//         return out;
//     }

//     // --- Case 2: WFv1BIN (per-channel) ---
//     if (std::string(magic8, 7) == "WFv1BIN") {
//         out.fmt = "BIN";
//         // Skip fixed 16-byte header we wrote in the example: magic[8], version(uint32), reserved(uint32)
//         in.seekg(16, std::ios::beg);
//         ChannelData cd; cd.ch_id = guess_channel_from_name(path);

//         while (true) {
//             uint32_t ns=0;
//             in.read(reinterpret_cast<char*>(&ns), sizeof(ns));
//             if (!in) break;
//             std::vector<int16_t> samples(ns);
//             if (ns) in.read(reinterpret_cast<char*>(samples.data()), ns * sizeof(int16_t));
//             if (!in && ns) throw std::runtime_error("Truncated samples block");
//             cd.events.emplace_back(std::move(samples));
//         }
//         out.channels.push_back(std::move(cd));
//         return out;
//     }

//     // --- Case 3: RAW (no header): treat as [uint32 nsamples][int16 samples...] repeated
//     out.fmt = "RAW";
//     in.seekg(0, std::ios::beg);
//     ChannelData cd; cd.ch_id = guess_channel_from_name(path);
//     while (true) {
//         uint32_t ns=0;
//         in.read(reinterpret_cast<char*>(&ns), sizeof(ns));
//         if (!in) break;
//         std::vector<int16_t> samples(ns);
//         if (ns) in.read(reinterpret_cast<char*>(samples.data()), ns * sizeof(int16_t));
//         if (!in && ns) throw std::runtime_error("Truncated samples block");
//         cd.events.emplace_back(std::move(samples));
//     }
//     out.channels.push_back(std::move(cd));
//     return out;
// }

// // ROOT entrypoint
// void bin_waveforms(const char* filename= "/home/zdfk/ACQ_SOFT/github_try/Alpha_spectrometer/SPECTRUM_ACQ/SPECT_GUI/run_2025-11-10_16-19-50_ch0.bin")
// {
//     auto data = load_auto_bin(filename);
//     std::cout << "[INFO] Detected format: " << data.fmt << "\n";
//     std::cout << "[INFO] Channels loaded: " << data.channels.size() << "\n";
//     for (const auto& ch : data.channels) {
//         std::cout << "  ch " << ch.ch_id << " : " << ch.events.size() << " events\n";
//     }

//     // ---------- PLOT HERE (paste your block below) ----------
//     // ---- Plot ALL channels; each row = one channel; many events overlaid ----
//     TCanvas* c = new TCanvas("c_all", "Waveforms by channel",
//                              1200, 250*std::max(1,(int)data.channels.size()));
//     c->Divide(1, (int)data.channels.size());

//     std::vector<Color_t> chColors = {
//         kBlue, kRed, kGreen+2, kMagenta+1, kOrange+7, kCyan+1, kViolet+1, kTeal+3
//     };
//     const int maxEventsPerChannel = 20;   // avoid drawing thousands at once
//     const double dt_ns = (data.dt_ns > 0 ? data.dt_ns : 1.0);

//     std::vector<std::unique_ptr<TGraph>> keepAlive; // keep graphs alive after function exits

//     for (size_t ich = 0; ich < data.channels.size(); ++ich) {
//         const auto& ch = data.channels[ich];
//         c->cd(ich+1);

//         auto* mg = new TMultiGraph();
//         auto* leg = new TLegend(0.72, 0.70, 0.98, 0.98);
//         leg->SetBorderSize(0);

//         const Color_t baseColor = chColors[ich % chColors.size()];
//         const int nEv = std::min<int>((int)ch.events.size(), maxEventsPerChannel);

//         for (int e = 0; e < nEv; ++e) {
//             const auto& samp = ch.events[e];
//             if (samp.empty()) continue;

//             std::vector<double> x(samp.size()), y(samp.size());
//             for (size_t i = 0; i < samp.size(); ++i) { x[i] = i * dt_ns; y[i] = samp[i]; }

//             auto g = std::make_unique<TGraph>((int)samp.size(), x.data(), y.data());
//             g->SetLineWidth(1);
//             g->SetLineColor(baseColor);
//             // g->SetLineColorAlpha(baseColor, nEv > 1 ? 0.35 : 1.0); // if your ROOT supports alpha

//             mg->Add(g.get(), "L");
//             if (e < 6) leg->AddEntry(g.get(), Form("evt %d", e), "l");
//             keepAlive.emplace_back(std::move(g));
//         }

//         mg->SetTitle(Form("Channel %u;Time [ns];Amplitude [ADC]", ch.ch_id));
//         mg->Draw("A");
//         leg->Draw();
//         gPad->Update();
//     }
//     c->Update();
// }


// // void bin_waveforms(const char* filename = "/home/zdfk/ACQ_SOFT/github_try/Alpha_spectrometer/SPECTRUM_ACQ/SPECT_GUI/run_2025-11-10_16-19-50_ch0.bin")
// // {
// //     auto all = load_all_bin(filename);
// //     std::cout << "Loaded " << all.channels.size() << " channels\n";
// //     for (const auto& ch : all.channels) {
// //         std::cout << "  Channel " << ch.ch_id << " has " 
// //                   << ch.events.size() << " events\n";
// //     }

// //     // Example: plot the first event from the first channel
// //     if (!all.channels.empty() && !all.channels[0].events.empty()) {
// //         const auto& samples = all.channels[0].events[0];
// //         const size_t N = samples.size();
// //         std::vector<double> x(N), y(N);
// //         double dt_ns = all.fh.dt_ns;
// //         for (size_t i = 0; i < N; ++i) {
// //             x[i] = i * dt_ns;
// //             y[i] = samples[i];
// //         }
// //         auto* gr = new TGraph(N, x.data(), y.data());
// //         gr->SetTitle(Form("Channel %u - Event 0;Time [ns];Amplitude [ADC]", all.channels[0].ch_id));
// //         gr->Draw("AL");
// //     }
// // }


#include <fstream>
#include <vector>
#include <cstdint>
#include <string>
#include <stdexcept>
#include <iostream>

#include "TFile.h"
#include "TTree.h"

struct FileHeaderALL {
    char     magic[8];
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
    std::vector<std::vector<int16_t>> events;
};

struct Loaded {
    FileHeaderALL fh{};
    std::vector<ChannelData> channels;
};

static Loaded load_all(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open " + path);

    Loaded L;
    in.read(reinterpret_cast<char*>(&L.fh), sizeof(L.fh));
    if (std::string(L.fh.magic, 7) != "WFv1ALL")
        throw std::runtime_error("Bad magic (not WFv1ALL)");

    L.channels.reserve(L.fh.n_channels);
    for (uint32_t i = 0; i < L.fh.n_channels; ++i) {
        ChannelHeader ch{};
        in.read(reinterpret_cast<char*>(&ch), sizeof(ch));

        ChannelData cd; cd.ch_id = ch.ch_id;
        cd.events.reserve(static_cast<size_t>(ch.num_events));
        for (uint64_t e = 0; e < ch.num_events; ++e) {
            uint32_t ns = 0;
            in.read(reinterpret_cast<char*>(&ns), sizeof(ns));
            std::vector<int16_t> v(ns);
            if (ns) in.read(reinterpret_cast<char*>(v.data()), ns * sizeof(int16_t));
            cd.events.emplace_back(std::move(v));
        }
        L.channels.emplace_back(std::move(cd));
    }
    return L;
}

void bin_waveforms(const char* binPath,
              const char* rootOut = "reco.root")
{
    auto L = load_all(binPath);
    std::cout << "[INFO] Channels: " << L.channels.size()
              << "  Fs=" << L.fh.sample_rate_hz
              << " Hz  dt=" << L.fh.dt_ns << " ns\n";

    TFile* fout = TFile::Open(rootOut, "RECREATE");
    if (!fout || fout->IsZombie()) {
        throw std::runtime_error(std::string("Cannot create ") + rootOut);
    }

    for (const auto& ch : L.channels) {
        // Branch payloads
        int event = 0;
        int channel = (int)ch.ch_id;
        int ch_global = (int)ch.ch_id;
        double baseline = 0;
        double amplitude = 0;
        double charge = 0;
        double t0 = 0;
        std::vector<short> waveform;
        std::vector<double> time_ns;

        TTree* tree = new TTree(Form("ch%d", channel), Form("Waveforms CH %d", channel));
        tree->Branch("channel",   &channel);
        tree->Branch("ch_global", &ch_global);
        tree->Branch("event",     &event);
        tree->Branch("baseline",  &baseline);
        tree->Branch("amplitude", &amplitude);
        tree->Branch("charge",    &charge);
        tree->Branch("t0",        &t0);
        tree->Branch("waveform",  &waveform);
        tree->Branch("time_ns",   &time_ns);

        for (const auto& ev : ch.events) {
            waveform.assign(ev.begin(), ev.end());
            const int N = (int)waveform.size();

            // reconstruct time axis from dt_ns
            time_ns.resize(N);
            for (int i = 0; i < N; ++i) time_ns[i] = i * L.fh.dt_ns;

            // simple derived features (optional; mirror your SaveToRoot)
            if (N > 0) {
                amplitude = *std::max_element(waveform.begin(), waveform.end());
                charge = std::accumulate(waveform.begin(), waveform.end(), 0.0);
            } else {
                amplitude = 0; charge = 0;
            }
            t0 = 0;

            tree->Fill();
            ++event;
        }

        fout->cd();
        tree->Write();
        delete tree;
    }

    fout->Close();
    std::cout << "[OK] Wrote " << rootOut << std::endl;
}
