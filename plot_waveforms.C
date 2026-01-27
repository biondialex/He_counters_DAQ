#include <TFile.h>
#include <TTree.h>
#include <TChain.h>
#include <TCanvas.h>
#include <TGraph.h>
#include <TLegend.h>
#include <TAxis.h>
#include <TStyle.h>
#include <vector>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <numeric>

// =====================================================
// --- Helper filters
// =====================================================

// --- Gaussian kernel ---
std::vector<double> gaussianKernel(double sigma)
{
    int radius = std::max(1, (int)std::ceil(4.0 * sigma));
    int W = 2 * radius + 1;
    std::vector<double> k(W);
    double s2 = 2.0 * sigma * sigma, norm = 0.0;
    for (int i = -radius; i <= radius; ++i) {
        double v = std::exp(-(i * i) / s2);
        k[i + radius] = v;
        norm += v;
    }
    for (double &v : k) v /= norm;
    return k;
}

// --- Convolution (same length) ---
std::vector<double> convolveSame(const std::vector<double>& x, const std::vector<double>& k)
{
    int n = x.size(), m = k.size(), r = m / 2;
    std::vector<double> y(n, 0.0);
    for (int i = 0; i < n; ++i) {
        double acc = 0.0;
        for (int j = 0; j < m; ++j) {
            int xi = std::clamp(i + j - r, 0, n - 1);
            acc += x[xi] * k[j];
        }
        y[i] = acc;
    }
    return y;
}

// --- CR–(RC)^n shaping filter ---
void rc_lowpass(const std::vector<double>& x, std::vector<double>& y, double tau_s, double dt)
{
    double a = dt / (tau_s + dt);
    y.resize(x.size());
    double s = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
        s += a * (x[i] - s);
        y[i] = s;
    }
}

void cr_highpass(const std::vector<double>& x, std::vector<double>& y, double tau_s, double dt)
{
    double a = tau_s / (tau_s + dt);
    y.resize(x.size());
    double prev_x = 0.0, prev_y = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
        double yi = a * (prev_y + x[i] - prev_x);
        y[i] = yi;
        prev_y = yi;
        prev_x = x[i];
    }
}

std::vector<double> quasiGaussian(const std::vector<double>& x, double tau_s, int n, double dt)
{
    std::vector<double> y, tmp;
    cr_highpass(x, y, tau_s, dt); // CR
    for (int i = 0; i < n; ++i) { // (RC)^n
        rc_lowpass(y, tmp, tau_s, dt);
        y.swap(tmp);
    }
    return y;
}

// =====================================================
// --- Main plotting function
// =====================================================

void selected_channel_manual_events(const char* filename = "run_2025-10-29_13-15-13.root", //.run_2025-10-23_16-29-19.root",//"run_2025-10-22_21-35-50.root",
                    const char* treename = "ch1",
                    int n_events =22000,
                    int filter_mode = 1,   // 0=none, 1=Gaussian, 2=CR-(RC)^n
                    double sigma = 60.0,   // Gaussian width (samples)
                    double tau_ns = 150.0) // shaping time (ns)
{
    gStyle->SetOptStat(0);

    // --- Combine multiple ROOT files into a chain ---
    TChain* t = new TChain(treename);

    // Add the main file passed in the function
    t->Add(filename);

    // Add additional files manually (or use a wildcard pattern)
    t->Add("run_2025-10-30_14-12-19.root");
    // t->Add("run_2025-10-22_08-38-28.root");
    // t->Add("run_2025-10-20_12-52-51.root");
    // t->Add("run_2025-10-27_00-36-55.root");
    // t->Add("run_2025-10-26_09-46-06.root");
    // t->Add("run_2025-10-25_23-16-56.root");
    // t->Add("run_2025-10-24_08-37-31.root");
    // t->Add("run_2025-10-23_19-41-16.root");
    // t->add("run_2025-10-28_12-02-12.root");


    std::cout << "[INFO] Total combined entries: " << t->GetEntries() << std::endl;

    // --- Branch setup ---
    std::vector<short>* wf = nullptr;
    std::vector<double>* time_ns = nullptr;

    t->SetBranchAddress("waveform", &wf);

    TBranch* b_time = t->GetBranch("time_ns");
    bool has_time = (b_time != nullptr);
    if (has_time) {
        t->SetBranchAddress("time_ns", &time_ns);
        std::cout << "[INFO] Using time_ns branch for x-axis" << std::endl;
    } else {
        std::cout << "[WARN] time_ns branch not found — using sample index" << std::endl;
    }

    // --- Canvas setup ---
    auto c = new TCanvas("c", Form("Filtered waveforms (%s)", treename), 1000, 600);
    c->SetGrid();

    int nentries = std::min((int)t->GetEntries(), n_events);
    std::cout << "[INFO] Plotting " << nentries << " entries from chain." << std::endl;

    double SampleRate = 1e9; // fallback 1 GS/s if no time branch
    double dt = 1.0 / SampleRate;
    double tau_s = tau_ns * 1e-9;

    TH1D* hCharge = new TH1D("hCharge", "Integrated Charge Distribution;Charge [ADC·ns];Counts", 1500, 0, 2200);
    TH1D* hAmplitude = new TH1D("hAmplitude", "Amplitude Distribution;Amplitude [ADC counts];Counts", 1000, -10, 100);

    // --- Loop over entries ---
    for (int i = 0; i < nentries; ++i) {
        int fileNum = t->GetTreeNumber(); // tells which file this entry belongs to
        if (i % 100 == 0) // print occasionally
            std::cout << "[DEBUG] Entry " << i << " from file index " << fileNum << std::endl;
        t->GetEntry(i);
        int N = wf->size();
        if (N == 0) continue;

        std::vector<double> x(N), y(N);
        for (int j = 0; j < N; ++j) {
            if (has_time) x[j] = time_ns->at(j);
            else x[j] = 1e9 * j / SampleRate;
            y[j] = wf->at(j);
        }

        // --- Compute baseline ---
        int gate = std::min(100, N);
        double baseline = std::accumulate(y.begin(), y.begin() + gate, 0.0) / gate;
        for (int j = 0; j < N; ++j)
            y[j] -= baseline;

        // --- Apply filter ---
        std::vector<double> y_f = y;
        if (filter_mode == 1) {
            auto k = gaussianKernel(sigma);
            y_f = convolveSame(y, k);
        } else if (filter_mode == 2) {
            y_f = quasiGaussian(y, tau_s, 4, dt);
        }
        // --- Find peak and integrate around it ---
        auto it_max = std::max_element(y_f.begin(), y_f.end());
        int peak_index = std::distance(y_f.begin(), it_max);
        double amplitude = *it_max; 
        int window = 50; // number of samples around the peak to integrate

        int start = std::max(0, peak_index - window);
        int end   = std::min((int)y_f.size() - 1, peak_index + window);

        // Integrate charge (area under waveform)
        double charge = 0.0;
        for (int j = start; j <= end; ++j)
            charge += y_f[j] * dt; // dt = time step (seconds)

        // Convert to arbitrary charge units (optional scaling)
        charge *= 1e9; // if you want to express in "ADC·ns"

        // Fill histogram
        hCharge->Fill(charge);
        hAmplitude->Fill(amplitude);
        // --- Draw total charge histogram ---


        // --- Ensure monotonic x (avoid broken lines) ---
        for (int j = 1; j < N; ++j) {
            if (x[j] <= x[j - 1])
                x[j] = x[j - 1] + 1e-6;
        }

        // --- Plot ---
        // --- Determine color per file ---
        int colors[] = {kBlue, kRed, kGreen+2};
        int nColors = sizeof(colors) / sizeof(int);
        int color = colors[fileNum % nColors];

        auto g_filt = new TGraph(N, x.data(), y_f.data());
        g_filt->SetLineColor(color);
        g_filt->SetLineWidth(1);
        g_filt->SetTitle(Form("Channel %d", i));


        if (i == 0) {
            g_filt->Draw("AL");
            g_filt->GetXaxis()->SetTitle("Time [ns]");
            g_filt->GetYaxis()->SetTitle("ADC counts");
            g_filt->GetYaxis()->SetRangeUser(-10, 20);
        } else {
            g_filt->Draw("L SAME");
        }
    }
    // --- Create legend showing file colors and entries ---
    auto legend = new TLegend(0.40, 0.45, 0.95, 0.80);

    legend->SetBorderSize(0);
    legend->SetFillStyle(0);
    legend->SetTextSize(0.03);

    // Same color array as in the loop
    int colors[] = {kBlue, kRed, kGreen+2};
    int nColors = sizeof(colors) / sizeof(int);

    // Loop through files in the chain
    int nfiles = t->GetListOfFiles()->GetEntries();
    auto offsets = t->GetTreeOffset();  // array of cumulative entry offsets

    for (int f = 0; f < nfiles; ++f) {
        auto fileElem = (TChainElement*)t->GetListOfFiles()->At(f);
        TString fname = fileElem->GetTitle();  // full path of the file

        // Determine how many entries belong to this file
        Long64_t start = offsets[f];
        Long64_t end = (f + 1 < nfiles) ? offsets[f + 1] : t->GetEntries();
        Long64_t count = end - start;

        // Create a dummy graph for color representation
        auto dummy = new TGraph();
        dummy->SetLineColor(colors[f % nColors]);
        dummy->SetLineWidth(2);

        // Add legend entry
        legend->AddEntry(dummy, Form("%s  [%lld entries]", fname.Data(), count), "l");

    }

    // Draw legend
    legend->Draw();

    gPad->SetTitle(Form("%s (filtered);Time [ns];ADC counts", treename));
    // --- Save canvas automatically ---
    TString outname = Form("%s_filtered_mode%d.png", treename, filter_mode);
    c->SaveAs(outname);
    std::cout << "[INFO] Plot saved to " << outname << std::endl;
// --- Draw total charge histogram ---
    auto c2 = new TCanvas("c2", "Charge Distribution", 800, 600);
    c2->SetGrid();
    hCharge->SetLineColor(kBlue+1);
    hCharge->SetFillColorAlpha(kAzure-9, 0.35);
    hCharge->Draw();
    c2->SaveAs("charge_distribution.png");

    std::cout << "[INFO] Saved charge histogram to charge_distribution.png" << std::endl;
    // --- Draw amplitude histogram ---
    auto c3 = new TCanvas("c3", "Amplitude Distribution", 800, 600);
    c3->SetGrid();
    hAmplitude->SetLineColor(kRed+1);
    hAmplitude->SetFillColorAlpha(kPink-9, 0.35);
    hAmplitude->Draw();
    c3->SaveAs("amplitude_distribution.png");
    std::cout << "[INFO] Saved amplitude histogram to amplitude_distribution.png" << std::endl;

   delete c;
}
void all_channel_spectra(const char* filename = "run_2025-10-30_14-12-19.root",
                         int filter_mode = 1,     // 0=none, 1=Gaussian, 2=CR-(RC)^n
                         double sigma = 60.0,     // Gaussian width (samples)
                         double tau_ns = 150.0)   // shaping time (ns)
{
    gStyle->SetOptStat(0);

    // --- Open file ---
    TFile* f = TFile::Open(filename);
    if (!f || f->IsZombie()) {
        std::cerr << "[ERROR] Cannot open file: " << filename << std::endl;
        return;
    }

    const int NCH = 4;
    const char* tnames[NCH] = {"ch1", "ch2", "ch3"};
    const int colors[NCH] = {kBlue, kRed, kGreen + 2, kMagenta};

    std::vector<TTree*> trees(NCH, nullptr);
    std::vector<std::vector<short>*> wf(NCH, nullptr);
    std::vector<std::vector<double>*> tns(NCH, nullptr);
    std::vector<int> nentries(NCH, 0);

    // --- Load trees and branches ---
    for (int ch = 0; ch < NCH; ++ch) {
        trees[ch] = (TTree*)f->Get(tnames[ch]);
        if (!trees[ch]) {
            std::cerr << "[WARN] Missing " << tnames[ch] << "\n";
            continue;
        }

        trees[ch]->SetBranchAddress("waveform", &wf[ch]);
        if (trees[ch]->GetBranch("time_ns"))
            trees[ch]->SetBranchAddress("time_ns", &tns[ch]);
        nentries[ch] = trees[ch]->GetEntries();

        std::cout << "[INFO] " << tnames[ch] << ": " << nentries[ch] << " entries\n";
    }

    // --- Determine number of synchronized events (optional) ---
    int n_sync = INT_MAX;
    for (int ch = 0; ch < NCH; ++ch)
        if (trees[ch]) n_sync = std::min(n_sync, nentries[ch]);
    if (n_sync == INT_MAX) {
        std::cerr << "[ERROR] No valid channels found.\n";
        f->Close();
        return;
    }
    std::cout << "[INFO] Processing up to " << n_sync << " synchronized events.\n";

    // --- Constants ---
    double SampleRate = 1e9;
    double dt = 1.0 / SampleRate;
    double tau_s = tau_ns * 1e-9;

    // --- Histograms per channel ---
    std::vector<TH1D*> hCharge(NCH), hAmp(NCH);
    for (int ch = 0; ch < NCH; ++ch) {
        hCharge[ch] = new TH1D(Form("hCharge_ch%d", ch),
                               Form("Charge Spectrum CH%d;Charge [ADC·ns];Counts", ch),
                               1500, 0, 2200);
        hAmp[ch] = new TH1D(Form("hAmp_ch%d", ch),
                            Form("Amplitude Spectrum CH%d;Amplitude [ADC];Counts", ch),
                            1000, -10, 100);
        hCharge[ch]->SetLineColor(colors[ch]);
        hAmp[ch]->SetLineColor(colors[ch]);
        hCharge[ch]->SetLineWidth(2);
        hAmp[ch]->SetLineWidth(2);
    }

    // --- Loop over events ---
    for (int iEvt = 0; iEvt < n_sync; ++iEvt) {
        for (int ch = 0; ch < NCH; ++ch) {
            if (!trees[ch]) continue;
            trees[ch]->GetEntry(iEvt);
            int N = wf[ch]->size();
            if (N == 0) continue;

            std::vector<double> y(N);
            for (int j = 0; j < N; ++j)
                y[j] = wf[ch]->at(j);

            // --- Baseline subtraction ---
            int gate = std::min(100, N);
            double baseline = std::accumulate(y.begin(), y.begin() + gate, 0.0) / gate;
            for (auto& v : y) v -= baseline;

            // --- Apply filter ---
            std::vector<double> y_f = y;
            if (filter_mode == 1) {
                auto k = gaussianKernel(sigma);
                y_f = convolveSame(y, k);
            } else if (filter_mode == 2) {
                y_f = quasiGaussian(y, tau_s, 4, dt);
            }

            // --- Peak finding ---
            auto it_max = std::max_element(y_f.begin(), y_f.end());
            double amplitude = *it_max;
            int peak_index = std::distance(y_f.begin(), it_max);
            int window = 50;
            int start = std::max(0, peak_index - window);
            int end = std::min((int)y_f.size() - 1, peak_index + window);

            // --- Integrate charge ---
            double charge = 0.0;
            for (int j = start; j <= end; ++j)
                charge += y_f[j] * dt;
            charge *= 1e9; // convert to ADC·ns

            hAmp[ch]->Fill(amplitude);
            hCharge[ch]->Fill(charge);
        }
    }

    // --- Plot charge spectra ---
    auto c1 = new TCanvas("c_charge", "Charge Spectra (All Channels)", 900, 600);
    c1->SetGrid();
    bool first = true;
    for (int ch = 0; ch < NCH; ++ch) {
        if (!trees[ch]) continue;
        if (first) {
            hCharge[ch]->Draw("HIST");
            first = false;
        } else {
            hCharge[ch]->Draw("HIST SAME");
        }
    }
    auto leg1 = new TLegend(0.70, 0.70, 0.93, 0.90);
    leg1->SetBorderSize(0);
    leg1->SetFillStyle(0);
    for (int ch = 0; ch < NCH; ++ch)
        if (trees[ch]) leg1->AddEntry(hCharge[ch], Form("ch%d", ch), "l");
    leg1->Draw();
    c1->SaveAs("charge_spectra_all_channels.png");
    std::cout << "[INFO] Saved charge spectra: charge_spectra_all_channels.png\n";

    // --- Plot amplitude spectra ---
    auto c2 = new TCanvas("c_amp", "Amplitude Spectra (All Channels)", 900, 600);
    c2->SetGrid();
    first = true;
    for (int ch = 0; ch < NCH; ++ch) {
        if (!trees[ch]) continue;
        if (first) {
            hAmp[ch]->Draw("HIST");
            first = false;
        } else {
            hAmp[ch]->Draw("HIST SAME");
        }
    }
    auto leg2 = new TLegend(0.70, 0.70, 0.93, 0.90);
    leg2->SetBorderSize(0);
    leg2->SetFillStyle(0);
    for (int ch = 0; ch < NCH; ++ch)
        if (trees[ch]) leg2->AddEntry(hAmp[ch], Form("ch%d", ch), "l");
    leg2->Draw();
    c2->SaveAs("amplitude_spectra_all_channels.png");
    std::cout << "[INFO] Saved amplitude spectra: amplitude_spectra_all_channels.png\n";

    f->Close();
}

void all_channel_manual_entries_waveforms_only(const char* filename = "run_2025-10-30_14-12-19.root",
                    int n_events = 100,
                    int filter_mode = 1,     // 0=none, 1=Gaussian, 2=CR-(RC)^n
                    double sigma = 60.0,     // Gaussian width (samples)
                    double tau_ns = 150.0)   // shaping time (ns)
{
    gStyle->SetOptStat(0);

    // --- Open file ---
    TFile* f = TFile::Open(filename);
    if (!f || f->IsZombie()) {
        std::cerr << "[ERROR] Cannot open file: " << filename << std::endl;
        return;
    }

    // --- Channel names ---
    const char* tnames[4] = {"ch0", "ch1", "ch2", "ch3"};
    const int colors[4] = {kBlue, kRed, kGreen + 2, kMagenta};
    const int NCH = 4;

    // --- Load all trees ---
    std::vector<TTree*> trees(NCH, nullptr);
    std::vector<std::vector<short>*> wf(NCH, nullptr);
    std::vector<std::vector<double>*> tns(NCH, nullptr);
    std::vector<int> nentries_ch(NCH, 0);

    for (int i = 0; i < NCH; ++i) {
        trees[i] = (TTree*)f->Get(tnames[i]);
        if (!trees[i]) {
            std::cerr << "[WARN] Missing " << tnames[i] << " — skipping.\n";
            continue;
        }
        trees[i]->SetBranchAddress("waveform", &wf[i]);
        if (trees[i]->GetBranch("time_ns"))
            trees[i]->SetBranchAddress("time_ns", &tns[i]);
        nentries_ch[i] = std::min((int)trees[i]->GetEntries(), n_events);
        std::cout << "[INFO] " << tnames[i] << ": " << trees[i]->GetEntries()
                  << " entries (plotting " << nentries_ch[i] << ")\n";
    }

    // --- Find minimum common event count (sync plotting) ---
    int nentries = nentries_ch[0];
    for (int i = 1; i < NCH; ++i)
        nentries = std::min(nentries, nentries_ch[i]);

    std::cout << "[INFO] Plotting up to " << nentries << " synchronized events.\n";

    // --- Constants ---
    double SampleRate = 1e9;
    double dt = 1.0 / SampleRate;
    double tau_s = tau_ns * 1e-9;

    // --- Canvas for waveforms ---
    auto c = new TCanvas("c_all", "All Channels", 1000, 600);
    // c->SetGrid();

    // --- Loop through events ---
    for (int iEvt = 0; iEvt < nentries; ++iEvt) {
        for (int ch = 0; ch < NCH; ++ch) {
            if (!trees[ch]) continue;
            trees[ch]->GetEntry(iEvt);

            int N = wf[ch]->size();
            if (N == 0) continue;

            std::vector<double> x(N), y(N);
            for (int j = 0; j < N; ++j) {
                x[j] = tns[ch] ? tns[ch]->at(j) : j;
                y[j] = wf[ch]->at(j);
            }

            // --- Baseline subtraction ---
            int gate = std::min(100, N);
            double baseline = std::accumulate(y.begin(), y.begin() + gate, 0.0) / gate;
            for (auto& v : y) v -= baseline;

            // --- Filtering ---
            std::vector<double> yf = y;
            if (filter_mode == 1) {
                auto k = gaussianKernel(sigma);
                yf = convolveSame(y, k);
            } else if (filter_mode == 2) {
                yf = quasiGaussian(y, tau_s, 4, dt);
            }

            // --- Create graph ---
            auto g = new TGraph(N, x.data(), yf.data());
            g->SetLineColor(colors[ch]);
            g->SetLineWidth(1);

            if (ch == 0 && iEvt == 0) {
                g->Draw("AL");
                g->SetTitle("Waveforms (4 channels)");
                g->GetXaxis()->SetTitle("Time [ns]");
                g->GetYaxis()->SetTitle("ADC counts");
                g->GetYaxis()->SetRangeUser(-10, 130);
            } else {
                g->Draw("L SAME");
            }
        }
    }

    // --- Legend ---
    auto leg = new TLegend(0.70, 0.70, 0.95, 0.90);
    leg->SetBorderSize(0);
    leg->SetFillStyle(0);
    leg->SetTextSize(0.03);
    leg->SetHeader("Channels", "C");

    for (int i = 0; i < NCH; ++i) {
        if (!trees[i]) continue;
        auto dummy = new TGraph();
        dummy->SetLineColor(colors[i]);
        dummy->SetLineWidth(2);
        leg->AddEntry(dummy, Form("%s (%d events)", tnames[i], nentries_ch[i]), "l");
    }

    leg->Draw();

    // --- Save waveform plot ---
    c->SaveAs("waveforms_ch0_ch1_ch2_ch3.png");
    std::cout << "[INFO] Saved overlay plot: waveforms_ch0_ch1_ch2_ch3.png\n";

    f->Close();
}

void all_channel_all_events(const char* filename = "run_2025-10-30_14-12-19.root",
                    int filter_mode = 1,     // 0=none, 1=Gaussian, 2=CR-(RC)^n
                    double sigma = 60.0,     // Gaussian width (samples)
                    double tau_ns = 150.0)   // shaping time (ns)
{
    gStyle->SetOptStat(0);

    // --- Open file ---
    TFile* f = TFile::Open(filename);
    if (!f || f->IsZombie()) {
        std::cerr << "[ERROR] Cannot open file: " << filename << std::endl;
        return;
    }

    // --- Channel setup ---
    const int NCH = 4;
    const char* tnames[NCH] = {"ch0", "ch1", "ch2", "ch3"};
    const int colors[NCH] = {kBlue, kRed, kGreen + 2, kMagenta};

    std::vector<TTree*> trees(NCH, nullptr);
    std::vector<std::vector<short>*> wf(NCH, nullptr);
    std::vector<std::vector<double>*> tns(NCH, nullptr);
    std::vector<int> nentries_ch(NCH, 0);

    // --- Load all trees and set branches ---
    for (int i = 0; i < NCH; ++i) {
        trees[i] = (TTree*)f->Get(tnames[i]);
        if (!trees[i]) {
            std::cerr << "[WARN] Missing " << tnames[i] << " — skipping.\n";
            continue;
        }
        trees[i]->SetBranchAddress("waveform", &wf[i]);
        if (trees[i]->GetBranch("time_ns"))
            trees[i]->SetBranchAddress("time_ns", &tns[i]);
        nentries_ch[i] = trees[i]->GetEntries();
        std::cout << "[INFO] " << tnames[i] << ": " << nentries_ch[i] << " entries\n";
    }

    // --- Find minimum common number of entries (synchronized plotting) ---
    int nentries = INT_MAX;
    for (int i = 0; i < NCH; ++i)
        if (trees[i]) nentries = std::min(nentries, nentries_ch[i]);
    if (nentries == INT_MAX) {
        std::cerr << "[ERROR] No valid channels found.\n";
        f->Close();
        return;
    }

    std::cout << "[INFO] Plotting up to " << nentries << " synchronized events.\n";

    // --- Constants ---
    double SampleRate = 1e9;
    double dt = 1.0 / SampleRate;
    double tau_s = tau_ns * 1e-9;

    // --- Canvas for all channels ---
    auto c = new TCanvas("c_all", "All Channels", 1000, 600);
    // c->SetGrid();

    bool firstDraw = true;

    // --- Loop through events ---
    for (int iEvt = 0; iEvt < nentries; ++iEvt) {
        for (int ch = 0; ch < NCH; ++ch) {
            if (!trees[ch]) continue;
            trees[ch]->GetEntry(iEvt);

            int N = wf[ch]->size();
            if (N == 0) continue;

            std::vector<double> x(N), y(N);
            for (int j = 0; j < N; ++j) {
                x[j] = tns[ch] ? tns[ch]->at(j) : j;
                y[j] = wf[ch]->at(j);
            }

            // --- Baseline subtraction ---
            int gate = std::min(100, N);
            double baseline = std::accumulate(y.begin(), y.begin() + gate, 0.0) / gate;
            for (auto& v : y) v -= baseline;

            // --- Filtering ---
            std::vector<double> yf = y;
            if (filter_mode == 1) {
                auto k = gaussianKernel(sigma);
                yf = convolveSame(y, k);
            } else if (filter_mode == 2) {
                yf = quasiGaussian(y, tau_s, 4, dt);
            }

            // --- Draw graphs ---
            auto g = new TGraph(N, x.data(), yf.data());
            g->SetLineColor(colors[ch]);
            g->SetLineWidth(1);

            if (firstDraw) {
                g->Draw("AL");
                g->SetTitle("Waveforms (ch0–ch3)");
                g->GetXaxis()->SetTitle("Time [ns]");
                g->GetYaxis()->SetTitle("ADC counts");
                g->GetYaxis()->SetRangeUser(-10, 130);
                firstDraw = false;
            } else {
                g->Draw("L SAME");
            }
        }
    }

    // --- Legend ---
    auto leg = new TLegend(0.70, 0.70, 0.95, 0.90);
    leg->SetBorderSize(0);
    leg->SetFillStyle(0);
    leg->SetTextSize(0.03);
    leg->SetHeader("Channels", "C");

    for (int i = 0; i < NCH; ++i) {
        if (!trees[i]) continue;
        auto dummy = new TGraph();
        dummy->SetLineColor(colors[i]);
        dummy->SetLineWidth(2);
        leg->AddEntry(dummy, Form("%s (%d events)", tnames[i], nentries_ch[i]), "l");
    }

    leg->Draw();

    // --- Save waveform plot ---
    c->SaveAs("waveforms_ch0_ch1_ch2_ch3.png");
    std::cout << "[INFO] Saved overlay plot: waveforms_ch0_ch1_ch2_ch3.png\n";

    f->Close();
}

void single_channel(const char* filename = "run_2025-10-30_14-12-19.root",
                    const char* channel = "ch2",  // << choose which channel
                    int filter_mode = 1,
                    double sigma = 60.0,
                    double tau_ns = 150.0)
{
    gStyle->SetOptStat(0);

    // --- Open file ---
    TFile* f = TFile::Open(filename);
    if (!f || f->IsZombie()) {
        std::cerr << "[ERROR] Cannot open file: " << filename << std::endl;
        return;
    }

    TTree* t = (TTree*)f->Get(channel);
    if (!t) {
        std::cerr << "[ERROR] Missing tree " << channel << std::endl;
        f->Close();
        return;
    }

    std::vector<short>* wf = nullptr;
    std::vector<double>* tns = nullptr;
    t->SetBranchAddress("waveform", &wf);
    if (t->GetBranch("time_ns"))
        t->SetBranchAddress("time_ns", &tns);

    int nentries = t->GetEntries();
    std::cout << "[INFO] " << channel << ": " << nentries << " entries\n";

    double SampleRate = 1e9;
    double dt = 1.0 / SampleRate;
    double tau_s = tau_ns * 1e-9;

    auto c = new TCanvas("c_single", Form("Waveforms (%s)", channel), 1000, 600);
    // c->SetGrid();

    for (int iEvt = 0; iEvt < nentries; ++iEvt) {
        t->GetEntry(iEvt);
        int N = wf->size();
        if (N == 0) continue;

        std::vector<double> x(N), y(N);
        for (int j = 0; j < N; ++j) {
            x[j] = tns ? tns->at(j) : j;
            y[j] = wf->at(j);
        }

        // Baseline subtraction
        int gate = std::min(100, N);
        double baseline = std::accumulate(y.begin(), y.begin() + gate, 0.0) / gate;
        for (auto& v : y) v -= baseline;

        // Filtering
        std::vector<double> yf = y;
        if (filter_mode == 1) {
            auto k = gaussianKernel(sigma);
            yf = convolveSame(y, k);
        } else if (filter_mode == 2) {
            yf = quasiGaussian(y, tau_s, 4, dt);
        }

        auto g = new TGraph(N, x.data(), yf.data());
        g->SetLineColor(kBlue);
        g->SetLineWidth(1);

        if (iEvt == 0) {
            g->Draw("AL");
            g->SetTitle(Form("Waveforms (%s)", channel));
            g->GetXaxis()->SetTitle("Time [ns]");
            g->GetYaxis()->SetTitle("ADC counts");
            g->GetYaxis()->SetRangeUser(-10, 130);
        } else {
            g->Draw("L SAME");
        }
    }

    auto leg = new TLegend(0.70, 0.80, 0.95, 0.90);
    leg->AddEntry((TObject*)nullptr, Form("%s (%d events)", channel, nentries), "");
    leg->Draw();

    c->SaveAs(Form("waveforms_%s.png", channel));
    std::cout << "[INFO] Saved plot: waveforms_" << channel << ".png" << std::endl;

    f->Close();
}




void test(const char* treename = "ch2",
                                  int filter_mode = 1,   // 0=none, 1=Gaussian, 2=CR-(RC)^n
                                  double sigma = 60.0,   // Gaussian width (samples)
                                  double tau_ns = 150.0) // shaping time (ns)
{
    gStyle->SetOptStat(0);

    // --- Combine ROOT files automatically ---
    TChain* t = new TChain(treename);

    // You can add multiple files manually or via wildcard pattern:
    //t->Add("run_2025-10-29_13-15-13.root");
    t->Add("run_2025-10-30_14-12-19.root");
    // or simply:  t->Add("run_2025-10-*.root"); // wildcard works too!

    Long64_t total_entries = t->GetEntries();
    if (total_entries == 0) {
        std::cerr << "[ERROR] No entries found in chain for " << treename << std::endl;
        return;
    }
    std::cout << "[INFO] Total combined entries for " << treename << ": " << total_entries << std::endl;

    // --- Branch setup ---
    std::vector<short>* wf = nullptr;
    std::vector<double>* time_ns = nullptr;

    t->SetBranchAddress("waveform", &wf);

    bool has_time = (t->GetBranch("time_ns") != nullptr);
    if (has_time) {
        t->SetBranchAddress("time_ns", &time_ns);
        std::cout << "[INFO] Using time_ns branch for x-axis.\n";
    } else {
        std::cout << "[WARN] time_ns not found — using sample index.\n";
    }

    // --- Canvas setup ---
    auto c = new TCanvas("c", Form("Filtered waveforms (%s)", treename), 1000, 600);
    c->SetGrid();

    double SampleRate = 1e9; // 1 GS/s default
    double dt = 1.0 / SampleRate;
    double tau_s = tau_ns * 1e-9;

    TH1D* hCharge = new TH1D("hCharge", "Integrated Charge Distribution;Charge [ADC·ns];Counts", 1000, 0, 2000);
    TH1D* hAmplitude = new TH1D("hAmplitude", "Amplitude Distribution;Amplitude [ADC counts];Counts", 1000, 0, 100);

    // --- Loop over all entries automatically ---
    for (Long64_t i = 0; i < total_entries; ++i) {
        t->GetEntry(i);
        int N = wf->size();
        if (N == 0) continue;

        std::vector<double> x(N), y(N);
        for (int j = 0; j < N; ++j) {
            x[j] = has_time ? time_ns->at(j) : j * 1e9 * dt;
            y[j] = wf->at(j);
        }

        // --- Baseline subtraction ---
        int gate = std::min(100, N);
        double baseline = std::accumulate(y.begin(), y.begin() + gate, 0.0) / gate;
        for (auto& v : y) v -= baseline;

        // --- Apply filter ---
        std::vector<double> y_f = y;
        if (filter_mode == 1) {
            auto k = gaussianKernel(sigma);
            y_f = convolveSame(y, k);
        } else if (filter_mode == 2) {
            y_f = quasiGaussian(y, tau_s, 4, dt);
        }

        // --- Polarity detection ---
        double ymax = *std::max_element(y_f.begin(), y_f.end());
        double ymin = *std::min_element(y_f.begin(), y_f.end());
        bool negative_pulse = (std::abs(ymin) > std::abs(ymax));

        int peak_index;
        double amplitude;
        if (negative_pulse) {
            auto it_min = std::min_element(y_f.begin(), y_f.end());
            peak_index = std::distance(y_f.begin(), it_min);
            amplitude = -(*it_min);
        } else {
            auto it_max = std::max_element(y_f.begin(), y_f.end());
            peak_index = std::distance(y_f.begin(), it_max);
            amplitude = *it_max;
        }

        // --- Integrate charge around the pulse ---
        int window = std::min(200, N / 4);
        int start = std::max(0, peak_index - window);
        int end = std::min(N - 1, peak_index + window);

        double charge = 0.0;
        for (int j = start; j <= end; ++j)
            charge += (negative_pulse ? -y_f[j] : y_f[j]) * dt;
        charge *= 1e9; // ADC·ns

        // --- Fill histograms ---
        hAmplitude->Fill(amplitude);
        hCharge->Fill(charge);

        // --- Plot a few waveforms for illustration ---
        if (i < 500) { // only draw first 10
            auto g_filt = new TGraph(N, x.data(), y_f.data());
            g_filt->SetLineColor(kBlue);
            g_filt->SetLineWidth(1);
            if (i == 0) {
                g_filt->Draw("AL");
                g_filt->SetTitle(Form("Filtered waveforms (%s)", treename));
                g_filt->GetXaxis()->SetTitle("Time [ns]");
                g_filt->GetYaxis()->SetTitle("ADC counts");
            } else {
                g_filt->Draw("L SAME");
            }
        }
    }

    // --- Save waveform canvas ---
    TString outname = Form("%s_waveforms.png", treename);
    c->SaveAs(outname);
    std::cout << "[INFO] Saved overlay plot: " << outname << std::endl;

    // --- Plot charge ---
    auto c2 = new TCanvas("c2", "Charge Distribution", 800, 600);
    c2->SetGrid();
    hCharge->SetLineColor(kBlue + 1);
    hCharge->SetFillColorAlpha(kAzure - 9, 0.35);
    hCharge->Draw();
    c2->SaveAs(Form("%s_charge.png", treename));

    // --- Plot amplitude ---
    auto c3 = new TCanvas("c3", "Amplitude Distribution", 800, 600);
    c3->SetGrid();
    hAmplitude->SetLineColor(kRed + 1);
    hAmplitude->SetFillColorAlpha(kPink - 9, 0.35);
    hAmplitude->Draw();
    c3->SaveAs(Form("%s_amplitude.png", treename));

    std::cout << "[INFO] Finished: " << treename << std::endl;
}

void plot_waveforms(){
      //single_channel();
      all_channel_all_events();
     // selected_channel_manual_events();
     //all_channel_manual_entries_waveforms_only();
     // all_channel_spectra();
     //test();

}