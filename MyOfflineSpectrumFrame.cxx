#include "MyOfflineSpectrumFrame.h"
#include "WaveformFileFormat.h"
#include <TCanvas.h>
#include <TSystem.h>
#include <TGClient.h>
#include <TGFileDialog.h>
#include <TROOT.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>

MyOfflineSpectrumFrame::MyOfflineSpectrumFrame(const TGWindow* p, UInt_t w, UInt_t h)
: TGMainFrame(p, w, h)
{
    fZoomMax = 100.0;
    fCurrentXMax = fZoomMax;
    fLastActiveChannels = -1;
    fAxisDirty = true;
    fElapsedSeconds = 0.0;

    for (int ch = 0; ch < CHANNEL_NUMBER; ++ch) {
        fCounts[ch].assign(ENERGY_NBINS, 0);
        fChannelActive[ch] = false;
        fAdcRange[ch] = ENERGY_MAX;
    }

    // --- Buttons row ---
    fButtonFrame = new TGHorizontalFrame(this);
    fBtnSelectFile = new TGTextButton(fButtonFrame, "Select file", kOffBtnSelectFile);
    fBtnFullRange  = new TGTextButton(fButtonFrame, "Full range", kOffBtnFullRange);
    fBtnZoom       = new TGTextButton(fButtonFrame, Form("0 - %.0f", fZoomMax), kOffBtnZoomRange);
    fBtnClear      = new TGTextButton(fButtonFrame, "Clear spectra", kOffBtnClearSpectra);

    fBtnSelectFile->Associate(this);
    fBtnFullRange->Associate(this);
    fBtnZoom->Associate(this);
    fBtnClear->Associate(this);

    fButtonFrame->AddFrame(fBtnSelectFile,
        new TGLayoutHints(kLHintsLeft | kLHintsCenterY, 4, 4, 2, 2));
    fButtonFrame->AddFrame(fBtnFullRange,
        new TGLayoutHints(kLHintsLeft | kLHintsCenterY, 4, 4, 2, 2));
    fButtonFrame->AddFrame(fBtnZoom,
        new TGLayoutHints(kLHintsLeft | kLHintsCenterY, 4, 4, 2, 2));
    fButtonFrame->AddFrame(fBtnClear,
        new TGLayoutHints(kLHintsLeft | kLHintsCenterY, 4, 4, 2, 2));

    AddFrame(fButtonFrame, new TGLayoutHints(kLHintsTop | kLHintsExpandX));

    // --- Info row ---
    TGHorizontalFrame* infoFrame = new TGHorizontalFrame(this);
    fFileLabel = new TGLabel(infoFrame, "File: (none)");
    fStatusLabel = new TGLabel(infoFrame, "Status: idle");
    fProgressLabel = new TGLabel(infoFrame, "Progress: 0%");

    infoFrame->AddFrame(fFileLabel,
        new TGLayoutHints(kLHintsLeft | kLHintsCenterY, 4, 12, 2, 2));
    infoFrame->AddFrame(fStatusLabel,
        new TGLayoutHints(kLHintsLeft | kLHintsCenterY, 4, 12, 2, 2));
    infoFrame->AddFrame(fProgressLabel,
        new TGLayoutHints(kLHintsLeft | kLHintsCenterY, 4, 4, 2, 2));
    AddFrame(infoFrame, new TGLayoutHints(kLHintsTop | kLHintsExpandX));

    // ---- Main horizontal area: ROI panel (left) + canvas (right) ----
    TGHorizontalFrame* mainFrame = new TGHorizontalFrame(this);
    AddFrame(mainFrame, new TGLayoutHints(kLHintsExpandX | kLHintsExpandY, 5, 5, 5, 5));

    // Left: ROI panel
    fRoiPanel = new TGVerticalFrame(mainFrame);
    mainFrame->AddFrame(fRoiPanel, new TGLayoutHints(kLHintsLeft | kLHintsExpandY, 5, 5, 5, 5));

    auto* roiTitle = new TGLabel(fRoiPanel, "ROI (mV): Low / High");
    fRoiPanel->AddFrame(roiTitle, new TGLayoutHints(kLHintsCenterX, 2, 2, 4, 6));

    for (int ch = 0; ch < CHANNEL_NUMBER; ++ch) {
        TGHorizontalFrame* row = new TGHorizontalFrame(fRoiPanel);

        auto* lab = new TGLabel(row, Form("Ch %02d", ch));
        row->AddFrame(lab, new TGLayoutHints(kLHintsLeft, 2, 2, 2, 2));

        fRoiLowEntry[ch] = new TGNumberEntry(
            row, ENERGY_MIN, 7, -1,
            TGNumberFormat::kNESReal,
            TGNumberFormat::kNEAAnyNumber,
            TGNumberFormat::kNELLimitMinMax,
            ENERGY_MIN, ENERGY_MAX);

        fRoiHighEntry[ch] = new TGNumberEntry(
            row, ENERGY_MAX, 7, -1,
            TGNumberFormat::kNESReal,
            TGNumberFormat::kNEAAnyNumber,
            TGNumberFormat::kNELLimitMinMax,
            ENERGY_MIN, ENERGY_MAX);

        fRoiLowEntry[ch]->SetState(kFALSE);
        fRoiHighEntry[ch]->SetState(kFALSE);

        row->AddFrame(fRoiLowEntry[ch],  new TGLayoutHints(kLHintsLeft, 2, 2, 2, 2));
        row->AddFrame(fRoiHighEntry[ch], new TGLayoutHints(kLHintsLeft, 2, 2, 2, 2));

        fRoiPanel->AddFrame(row, new TGLayoutHints(kLHintsTop));
    }

    fBtnApplyRoi = new TGTextButton(fRoiPanel, "Apply ROI", kOffBtnApplyRoi);
    fBtnApplyRoi->Associate(this);
    fRoiPanel->AddFrame(fBtnApplyRoi, new TGLayoutHints(kLHintsCenterX, 5, 5, 10, 2));

    // Right: embedded canvas
    fCanvas = new TRootEmbeddedCanvas("offlineSpecCanvas", mainFrame, w, h);
    mainFrame->AddFrame(fCanvas, new TGLayoutHints(kLHintsExpandX | kLHintsExpandY, 5, 5, 5, 5));
    fCan = fCanvas->GetCanvas();

    // Create one histogram per channel
    for (int ch = 0; ch < CHANNEL_NUMBER; ++ch) {
        fHist[ch] = new TH1F(Form("OffCh%d", ch),
                             Form("Offline energy spectrum ch %d;Amplitude [mV];Counts", ch),
                             ENERGY_NBINS, ENERGY_MIN, ENERGY_MAX);
        fHist[ch]->GetXaxis()->SetRangeUser(ENERGY_MIN, ENERGY_MAX);
        fHist[ch]->SetLineWidth(1);
        fHist[ch]->SetStats(kFALSE);
        fHist[ch]->SetTitleSize(0.15, "T");
        fHist[ch]->SetLabelSize(0.1, "X");
        fHist[ch]->SetLabelSize(0.1, "Y");
        fHist[ch]->SetTitleSize(0.1, "X");
        fHist[ch]->SetTitleSize(0.1, "Y");
        fHist[ch]->SetTitleOffset(1.0, "X");
        fHist[ch]->SetTitleOffset(1.1, "Y");

        fRoiLow[ch]  = ENERGY_MIN;
        fRoiHigh[ch] = ENERGY_MAX;

        fRoiPave[ch] = nullptr;
        fHistDrawn[ch] = false;
        fRoiDirty[ch] = true;

        fRoiLineLow[ch].SetLineColor(kRed);
        fRoiLineHigh[ch].SetLineColor(kRed);
        fRoiLineLow[ch].SetLineWidth(2);
        fRoiLineHigh[ch].SetLineWidth(2);
    }

    SetWindowName("Offline Energy Spectra");
    MapSubwindows();
    Resize(GetDefaultSize());
    MapWindow();
}

MyOfflineSpectrumFrame::~MyOfflineSpectrumFrame()
{
}

void MyOfflineSpectrumFrame::CloseWindow()
{
    UnmapWindow();
}

void MyOfflineSpectrumFrame::OnSelectFile()
{
    static const char* filetypes[] = {
        "Binary files", "*.bin",
        "All files",    "*",
        nullptr,        nullptr
    };

    TGFileInfo fi;
    fi.fFileTypes = filetypes;
    fi.fIniDir    = nullptr;
    fi.fFilename  = nullptr;

    new TGFileDialog(gClient->GetRoot(), this, kFDOpen, &fi);
    if (!fi.fFilename)
        return;

    std::string chosen = fi.fFilename;
    LoadFile(chosen);
}

void MyOfflineSpectrumFrame::OnFullRange()
{
    fCurrentXMax = ENERGY_MAX;
    fAxisDirty = true;
    UpdateHistograms();
}

void MyOfflineSpectrumFrame::OnZoomRange()
{
    double xmax = fZoomMax;
    if (xmax > ENERGY_MAX) xmax = ENERGY_MAX;
    fCurrentXMax = xmax;
    fAxisDirty = true;
    UpdateHistograms();
}

void MyOfflineSpectrumFrame::OnClearSpectra()
{
    ResetSpectra();
    UpdateHistograms();
}

void MyOfflineSpectrumFrame::OnApplyRoi()
{
    for (int ch = 0; ch < CHANNEL_NUMBER; ++ch) {
        double lo = fRoiLowEntry[ch]->GetNumber();
        double hi = fRoiHighEntry[ch]->GetNumber();
        if (hi < lo) std::swap(lo, hi);

        double chMax = fAdcRange[ch];
        if (chMax <= 0) chMax = ENERGY_MAX;

        fRoiLow[ch]  = std::max(lo, ENERGY_MIN);
        fRoiHigh[ch] = std::min(hi, chMax);
        fRoiDirty[ch] = true;
    }

    UpdateHistograms();
}

void MyOfflineSpectrumFrame::ResetSpectra()
{
    for (int ch = 0; ch < CHANNEL_NUMBER; ++ch) {
        std::fill(fCounts[ch].begin(), fCounts[ch].end(), 0);
        if (fHist[ch]) fHist[ch]->Reset();
        fHistDrawn[ch] = false;
        fRoiDirty[ch] = true;
    }
    fElapsedSeconds = 0.0;
}

void MyOfflineSpectrumFrame::ApplyCountsToHist()
{
    for (int ch = 0; ch < CHANNEL_NUMBER; ++ch) {
        if (!fHist[ch]) continue;
        for (int b = 0; b < ENERGY_NBINS; ++b) {
            fHist[ch]->SetBinContent(b + 1, static_cast<double>(fCounts[ch][b]));
        }
        fHistDrawn[ch] = false;
        fRoiDirty[ch] = true;
    }
    fAxisDirty = true;
}

void MyOfflineSpectrumFrame::UpdateProgress(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    fProgressLabel->SetText(Form("Progress: %d%%", percent));
    fProgressLabel->Layout();
}

void MyOfflineSpectrumFrame::LoadFile(const std::string& path)
{
    fCurrentFilePath = path;
    fFileLabel->SetText(Form("File: %s", fCurrentFilePath.c_str()));
    fStatusLabel->SetText("Status: loading...");
    UpdateProgress(0);
    gSystem->ProcessEvents();

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        fStatusLabel->SetText("Status: cannot open file");
        return;
    }

    in.seekg(0, std::ios::end);
    const std::streamoff totalSize = in.tellg();
    in.seekg(0, std::ios::beg);

    FileHeader header{};
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!in) {
        fStatusLabel->SetText("Status: failed to read header");
        return;
    }

    const char expected_magic[] = "WFv2RAW";
    if (std::strncmp(header.magic, expected_magic, 7) != 0 || header.version != 2) {
        fStatusLabel->SetText("Status: unsupported file version");
        return;
    }

    ResetSpectra();

    // Determine enabled channels and ADC ranges
    const uint32_t mask = header.channel_mask;
    for (int ch = 0; ch < CHANNEL_NUMBER; ++ch) {
        bool active = false;
        if (mask != 0) {
            active = (mask & (1u << ch)) != 0;
        } else {
            active = (header.adc_range_mV_by_ch[ch] > 0);
        }
        fChannelActive[ch] = active;

        double range = static_cast<double>(header.adc_range_mV_by_ch[ch]);
        if (!active || range <= 0) range = ENERGY_MAX;
        if (range > ENERGY_MAX) range = ENERGY_MAX;
        fAdcRange[ch] = range;

        // Update ROI input limits and defaults
        fRoiLow[ch]  = ENERGY_MIN;
        fRoiHigh[ch] = range;
        fRoiLowEntry[ch]->SetLimits(TGNumberFormat::kNELLimitMinMax,
                                    ENERGY_MIN, range);
        fRoiHighEntry[ch]->SetLimits(TGNumberFormat::kNELLimitMinMax,
                                     ENERGY_MIN, range);
        fRoiLowEntry[ch]->SetNumber(fRoiLow[ch], kFALSE);
        fRoiHighEntry[ch]->SetNumber(fRoiHigh[ch], kFALSE);

        fRoiLowEntry[ch]->SetState(active);
        fRoiHighEntry[ch]->SetState(active);
        fRoiDirty[ch] = true;
    }

    const int pretrigger = (header.adc_pretrigger > 0 && header.adc_pretrigger < ADC_EVENT_SIZE)
                               ? static_cast<int>(header.adc_pretrigger)
                               : ADC_PRETRIGGER;

    std::vector<int8_t> waveform(ADC_EVENT_SIZE);
    int64_t lastTimestamp = 0;
    uint64_t totalEvents = 0;
    int lastPercent = -1;
    std::streamoff bytesRead = static_cast<std::streamoff>(sizeof(header));

    while (in) {
        BufferHeader bh{};
        in.read(reinterpret_cast<char*>(&bh), sizeof(bh));
        if (!in) break;
        bytesRead += static_cast<std::streamoff>(sizeof(bh));

        if (bh.magic != BufferHeader::MAGIC_START) {
            // Stop if we hit file trailer or corrupted data
            break;
        }

        const int ch = static_cast<int>(bh.channel_id);
        const bool active = (ch >= 0 && ch < CHANNEL_NUMBER) ? fChannelActive[ch] : false;

        for (uint32_t i = 0; i < bh.n_events; ++i) {
            EventHeader eh{};
            in.read(reinterpret_cast<char*>(&eh), sizeof(eh));
            if (!in) break;
            bytesRead += static_cast<std::streamoff>(sizeof(eh));

            in.read(reinterpret_cast<char*>(waveform.data()), ADC_EVENT_SIZE);
            if (!in) break;
            bytesRead += static_cast<std::streamoff>(ADC_EVENT_SIZE);

            lastTimestamp = eh.timestamp_ns;
            ++totalEvents;

            if (!active)
                continue;

            double E = energy_filter(waveform.data(),
                                     ADC_EVENT_SIZE,
                                     pretrigger,
                                     500,
                                     3);
            if (E > 0.0) {
                double E1 = E / 255.0 * fAdcRange[ch];
                double e_clamped = (E1 >= ENERGY_MAX) ? (ENERGY_MAX - 1e-6) : E1;

                int bin = static_cast<int>(
                    (e_clamped - ENERGY_MIN) * (ENERGY_NBINS / (ENERGY_MAX - ENERGY_MIN))
                );
                if (bin >= 0 && bin < ENERGY_NBINS) {
                    fCounts[ch][bin] += 1;
                }
            }
        }

        BufferTrailer bt{};
        in.read(reinterpret_cast<char*>(&bt), sizeof(bt));
        if (!in) break;
        bytesRead += static_cast<std::streamoff>(sizeof(bt));

        if (bt.magic != BufferTrailer::MAGIC_END) {
            break;
        }

        if (totalSize > 0) {
            int percent = static_cast<int>(100.0 * (double)bytesRead / (double)totalSize);
            if (percent != lastPercent) {
                lastPercent = percent;
                UpdateProgress(percent);
                gSystem->ProcessEvents();
            }
        }
    }

    int64_t end_ns = (header.file_end_time_ns != 0) ? header.file_end_time_ns : lastTimestamp;
    if (end_ns < header.file_start_time_ns) end_ns = header.file_start_time_ns;
    fElapsedSeconds = (end_ns > 0) ? (double)(end_ns - header.file_start_time_ns) * 1e-9 : 0.0;

    ApplyCountsToHist();
    UpdateHistograms();

    fStatusLabel->SetText(Form("Status: loaded (%llu events)", static_cast<unsigned long long>(totalEvents)));
    UpdateProgress(100);
    gSystem->ProcessEvents();
}

void MyOfflineSpectrumFrame::UpdateHistograms()
{
    if (!IsMapped()) {
        fAxisDirty = true;
        return;
    }

    int nActive = 0;
    for (int ch = 0; ch < CHANNEL_NUMBER; ++ch) {
        if (fChannelActive[ch]) ++nActive;
    }
    if (nActive <= 0) nActive = 1;

    if (nActive != fLastActiveChannels) {
        fLastActiveChannels = nActive;
        fCan->Clear();
        fCan->Divide(1, nActive);
        fAxisDirty = true;
        for (int ch = 0; ch < CHANNEL_NUMBER; ++ch) {
            fHistDrawn[ch] = false;
            fRoiDirty[ch] = true;
        }
    }

    fCan->cd(0);

    int padIndex = 1;
    bool anyPadChanged = false;
    for (int ch = 0; ch < CHANNEL_NUMBER; ++ch) {
        if (!fChannelActive[ch]) continue;

        fCan->cd(padIndex);
        gPad->SetLogy(true);

        double chMax = fAdcRange[ch];
        if (chMax <= 0) chMax = ENERGY_MAX;

        double xMax = fCurrentXMax;
        if (xMax > chMax) xMax = chMax;

        fHist[ch]->GetXaxis()->SetRangeUser(ENERGY_MIN, xMax);
        fHist[ch]->Draw();
        fHistDrawn[ch] = true;
        anyPadChanged = true;

        double lo = fRoiLow[ch];
        double hi = fRoiHigh[ch];

        int b1 = fHist[ch]->GetXaxis()->FindBin(lo);
        int b2 = fHist[ch]->GetXaxis()->FindBin(hi);
        if (b2 < b1) std::swap(b1, b2);

        double counts = fHist[ch]->Integral(b1, b2);
        double rate = (fElapsedSeconds > 0.0) ? counts / fElapsedSeconds : 0.0;

        double ymin = 0.0;
        double ymax = 1e10;

        fRoiLineLow[ch].SetX1(lo); fRoiLineLow[ch].SetY1(ymin);
        fRoiLineLow[ch].SetX2(lo); fRoiLineLow[ch].SetY2(ymax);
        fRoiLineHigh[ch].SetX1(hi); fRoiLineHigh[ch].SetY1(ymin);
        fRoiLineHigh[ch].SetX2(hi); fRoiLineHigh[ch].SetY2(ymax);

        fRoiLineLow[ch].Draw("same");
        fRoiLineHigh[ch].Draw("same");

        if (!fRoiPave[ch]) {
            fRoiPave[ch] = new TPaveText(0.62, 0.72, 0.98, 0.98, "NDC");
            fRoiPave[ch]->SetFillStyle(0);
            fRoiPave[ch]->SetBorderSize(0);
            fRoiPave[ch]->SetTextSize(0.1);
        }

        fRoiPave[ch]->Clear();
        fRoiPave[ch]->AddText(Form("ROI: %.1f - %.1f mV", lo, hi));
        fRoiPave[ch]->AddText(Form("Counts: %.0f", counts));
        fRoiPave[ch]->AddText(Form("Rate: %.3f Hz", rate));
        fRoiPave[ch]->Draw("same");
        fRoiDirty[ch] = false;

        padIndex++;
    }

    fAxisDirty = false;
    if (anyPadChanged) {
        fCan->Modified();
        fCan->Update();
    }
}

Bool_t MyOfflineSpectrumFrame::ProcessMessage(Long_t msg, Long_t parm1, Long_t parm2)
{
    switch (GET_MSG(msg)) {
    case kC_COMMAND:
        switch (GET_SUBMSG(msg)) {
        case kCM_BUTTON:
            if (parm1 == kOffBtnSelectFile) {
                OnSelectFile();
                return kTRUE;
            }
            if (parm1 == kOffBtnFullRange) {
                OnFullRange();
                return kTRUE;
            }
            if (parm1 == kOffBtnZoomRange) {
                OnZoomRange();
                return kTRUE;
            }
            if (parm1 == kOffBtnClearSpectra) {
                OnClearSpectra();
                return kTRUE;
            }
            if (parm1 == kOffBtnApplyRoi) {
                OnApplyRoi();
                return kTRUE;
            }
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }

    return TGMainFrame::ProcessMessage(msg, parm1, parm2);
}
