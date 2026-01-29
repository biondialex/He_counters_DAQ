#include "MySpectrumFrame.h"
#include "spectrum_acq.h"
#include <TCanvas.h>
#include <TROOT.h>
#include <TSystem.h>
#include <limits>

// extern energy_spectrum from spectrum_acq.cxx
extern uint64_t energy_spectrum[CHANNEL_NUMBER][ENERGY_NBINS];



MySpectrumFrame::MySpectrumFrame(const TGWindow* p, UInt_t w, UInt_t h)
: TGMainFrame(p, w, h)
{
    fZoomMax = 100.0;  // your zoom max
    fCurrentXMax = fZoomMax;
    fLastActiveChannels = -1;
    fAxisDirty = true;

    // --- Buttons row ---
    fButtonFrame = new TGHorizontalFrame(this);
    fBtnFullRange = new TGTextButton(fButtonFrame, "Full range", kBtnFullRange);
    fBtnZoom      = new TGTextButton(fButtonFrame, Form("0 - %.0f", fZoomMax), kBtnZoomRange);
    fBtnClear     = new TGTextButton(fButtonFrame, "Clear spectra", kBtnClearSpectra);

    // Associate buttons with this frame (so ProcessMessage gets events)
    fBtnFullRange->Associate(this);
    fBtnZoom->Associate(this);
    fBtnClear->Associate(this);

    fButtonFrame->AddFrame(fBtnFullRange,
        new TGLayoutHints(kLHintsLeft | kLHintsCenterY, 4, 4, 2, 2));
    fButtonFrame->AddFrame(fBtnZoom,
        new TGLayoutHints(kLHintsLeft | kLHintsCenterY, 4, 4, 2, 2));
    fButtonFrame->AddFrame(fBtnClear,
        new TGLayoutHints(kLHintsLeft | kLHintsCenterY, 4, 4, 2, 2));

    AddFrame(fButtonFrame,
        new TGLayoutHints(kLHintsTop | kLHintsExpandX));

    //Canvas for plots    

    // ---- Main horizontal area: ROI panel (left) + canvas (right) ----
    TGHorizontalFrame* mainFrame = new TGHorizontalFrame(this);
    AddFrame(mainFrame, new TGLayoutHints(kLHintsExpandX | kLHintsExpandY, 5,5,5,5));

    // Left: ROI panel
    fRoiPanel = new TGVerticalFrame(mainFrame);
    mainFrame->AddFrame(fRoiPanel, new TGLayoutHints(kLHintsLeft | kLHintsExpandY, 5,5,5,5));

    auto* roiTitle = new TGLabel(fRoiPanel, "ROI (mV): Low / High");
    fRoiPanel->AddFrame(roiTitle, new TGLayoutHints(kLHintsCenterX, 2,2,4,6));

    // One row per channel: label + low entry + high entry
    for (int ch = 0; ch < CHANNEL_NUMBER; ++ch) {
        double chMax = channel_adc_range[ch];

        TGHorizontalFrame* row = new TGHorizontalFrame(fRoiPanel);

        auto* lab = new TGLabel(row, Form("Ch %02d", ch));
        row->AddFrame(lab, new TGLayoutHints(kLHintsLeft, 2,2,2,2));

        fRoiLowEntry[ch] = new TGNumberEntry(
            row, ENERGY_MIN, 7, -1,
            TGNumberFormat::kNESReal,
            TGNumberFormat::kNEAAnyNumber,
            TGNumberFormat::kNELLimitMinMax,
            ENERGY_MIN, chMax);

        fRoiHighEntry[ch] = new TGNumberEntry(
            row, chMax, 7, -1,
            TGNumberFormat::kNESReal,
            TGNumberFormat::kNEAAnyNumber,
            TGNumberFormat::kNELLimitMinMax,
            ENERGY_MIN, chMax);

        row->AddFrame(fRoiLowEntry[ch],  new TGLayoutHints(kLHintsLeft, 2,2,2,2));
        row->AddFrame(fRoiHighEntry[ch], new TGLayoutHints(kLHintsLeft, 2,2,2,2));

        fRoiPanel->AddFrame(row, new TGLayoutHints(kLHintsTop));
    }

    // Apply ROI button (applies all per-channel fields)
    fBtnApplyRoi = new TGTextButton(fRoiPanel, "Apply ROI", kBtnApplyRoi);
    fBtnApplyRoi->Associate(this);
    fRoiPanel->AddFrame(fBtnApplyRoi, new TGLayoutHints(kLHintsCenterX, 5,5,10,2));

    // Right: embedded canvas
    fCanvas = new TRootEmbeddedCanvas("specCanvas", mainFrame, w, h);
    mainFrame->AddFrame(fCanvas, new TGLayoutHints(kLHintsExpandX | kLHintsExpandY, 5,5,5,5));
    fCan = fCanvas->GetCanvas();


    // Create one histogram per channel
    for (int ch = 0; ch < CHANNEL_NUMBER; ++ch) {
        double chMax = channel_adc_range[ch];
        fHist[ch] = new TH1F(Form("Ch%d", ch),
                             Form("Energy spectrum ch %d;Amplitude [mV];Counts", ch),
                             ENERGY_NBINS, ENERGY_MIN, ENERGY_MAX);
        fHist[ch]->GetXaxis()->SetRangeUser(ENERGY_MIN, chMax);
        fHist[ch]->SetLineWidth(1);
        fHist[ch]->SetStats(kFALSE);
        fHist[ch]->SetTitleSize(0.15, "T");   // static cosmetics
        fHist[ch]->SetLabelSize(0.1, "X");
        fHist[ch]->SetLabelSize(0.1, "Y");
        fHist[ch]->SetTitleSize(0.1, "X");
        fHist[ch]->SetTitleSize(0.1, "Y");
        fHist[ch]->SetTitleOffset(1.0, "X");
        fHist[ch]->SetTitleOffset(1.1, "Y");

        fRoiLow[ch]  = ENERGY_MIN;
        fRoiHigh[ch] = chMax;

        fRoiPave[ch]      = nullptr;
        fLastSpectrum[ch].assign(ENERGY_NBINS, std::numeric_limits<uint64_t>::max());
        fHistDrawn[ch] = false;
        fRoiDirty[ch] = true;

        fRoiLineLow[ch].SetLineColor(kRed);
        fRoiLineHigh[ch].SetLineColor(kRed);
        fRoiLineLow[ch].SetLineWidth(2);
        fRoiLineHigh[ch].SetLineWidth(2);
    }

    // Simple layout: one pad per active channel in a grid
    /*
    int nActive = 0;
    for (int ch = 0; ch < CHANNEL_NUMBER; ++ch)
        if (channel_enabled[ch]) ++nActive;

    if (nActive <= 0) nActive = 1;
    //int nCols = std::min(nActive, 4);
    //int nRows = (nActive + nCols - 1) / nCols;
    fCan->Divide(1,nActive);
    */
    // Timer to refresh, e.g. every 2000 ms
    fTimer = new TTimer(this, 2000, kTRUE);
    fTimer->TurnOn();

    SetWindowName("Energy Spectra");
    MapSubwindows();
    Resize(GetDefaultSize());
    MapWindow();
}

MySpectrumFrame::~MySpectrumFrame()
{
    if (fTimer) {
        fTimer->TurnOff();
        delete fTimer;
    }
}


void MySpectrumFrame::UpdateHistograms()
{
    if (!IsMapped()) {
        fAxisDirty = true; // ensure refresh next time it's shown
        return;
    }

    int nActive = 0;
    for (int ch = 0; ch < CHANNEL_NUMBER; ++ch) {
        if (channel_enabled[ch]) ++nActive;
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
            fLastSpectrum[ch].assign(ENERGY_NBINS, std::numeric_limits<uint64_t>::max());
        }
    }

    fCan->cd(0);

    bool anyPadChanged = false;
    int padIndex = 1;
    for (int ch = 0; ch < CHANNEL_NUMBER; ++ch) {
        if (!channel_enabled[ch]) continue;

        fCan->cd(padIndex);
        gPad->SetLogy(true);

        bool histChanged = false;
        // copy counts (only update bins that changed)
        for (int b = 0; b < ENERGY_NBINS; ++b) {
            uint64_t value = energy_spectrum[ch][b];
            if (!fHistDrawn[ch] || value != fLastSpectrum[ch][b]) {
                fHist[ch]->SetBinContent(b + 1, static_cast<double>(value));
                fLastSpectrum[ch][b] = value;
                histChanged = true;
            }
        }

        if (!histChanged && !fRoiDirty[ch] && !fAxisDirty && fHistDrawn[ch]) {
            padIndex++;
            continue;
        }

        // enforce current x-range here
        double chMax = channel_adc_range[ch];
        if (chMax <= 0) chMax = ENERGY_MAX;

        double xMax = fCurrentXMax;
        if (xMax > chMax) xMax = chMax;

        fHist[ch]->GetXaxis()->SetRangeUser(ENERGY_MIN, xMax);

        fHist[ch]->Draw();
        fHistDrawn[ch] = true;
        anyPadChanged = true;
        
        // ROI bounds
        double lo = fRoiLow[ch];
        double hi = fRoiHigh[ch];

        // Compute bins
        int b1 = fHist[ch]->GetXaxis()->FindBin(lo);
        int b2 = fHist[ch]->GetXaxis()->FindBin(hi);
        if (b2 < b1) std::swap(b1, b2);

        // Counts
        double counts = fHist[ch]->Integral(b1, b2);

        // Rate (Hz)
        double elapsed = get_run_visual_elapsed_seconds();
        double rate = (elapsed > 0.0) ? counts / elapsed : 0.0;

        // Draw ROI lines
        double ymin = 0.0;
        double ymax = 1e10;

        fRoiLineLow[ch].SetX1(lo); fRoiLineLow[ch].SetY1(ymin);
        fRoiLineLow[ch].SetX2(lo); fRoiLineLow[ch].SetY2(ymax);
        fRoiLineHigh[ch].SetX1(hi); fRoiLineHigh[ch].SetY1(ymin);
        fRoiLineHigh[ch].SetX2(hi); fRoiLineHigh[ch].SetY2(ymax);

        fRoiLineLow[ch].Draw("same");
        fRoiLineHigh[ch].Draw("same");

        // Text box
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




Bool_t MySpectrumFrame::HandleTimer(TTimer* t)
{
    if (t == fTimer) {
        UpdateHistograms();
        fTimer->Start(); // restart
        return kTRUE;
    }
    return kFALSE;
}

void MySpectrumFrame::CloseWindow()
{
    //if (fTimer) fTimer->TurnOff();  // optional
    UnmapWindow();                  // hide, don't delete
}

void MySpectrumFrame::OnFullRange()
{
    fCurrentXMax = ENERGY_MAX;
    fAxisDirty = true;

    UpdateHistograms();
}

void MySpectrumFrame::OnZoomRange()
{
    double xmax = fZoomMax;
    if (xmax > ENERGY_MAX) xmax = ENERGY_MAX;

    fCurrentXMax = xmax;
    fAxisDirty = true;
    UpdateHistograms();
}


void MySpectrumFrame::OnClearSpectra()
{
    // Clear backend counts
    clear_energy_spectra();

    // Also clear local histograms immediately
    for (int ch = 0; ch < CHANNEL_NUMBER; ++ch) {
        fHist[ch]->Reset();
        fHistDrawn[ch] = false;
        fLastSpectrum[ch].assign(ENERGY_NBINS, std::numeric_limits<uint64_t>::max());
        fRoiDirty[ch] = true;
    }

    fCan->Modified();
    fCan->Update();
}

void MySpectrumFrame::OnApplyRoi()
{
    for (int ch = 0; ch < CHANNEL_NUMBER; ++ch) {
        double lo = fRoiLowEntry[ch]->GetNumber();
        double hi = fRoiHighEntry[ch]->GetNumber();
        if (hi < lo) std::swap(lo, hi);

        fRoiLow[ch]  = std::max(lo, ENERGY_MIN);
        double chMax = channel_adc_range[ch];
        if (chMax <= 0) chMax = ENERGY_MAX;

        fRoiLow[ch]  = std::max(lo, ENERGY_MIN);
        fRoiHigh[ch] = std::min(hi, chMax);
        fRoiDirty[ch] = true;

    }

    UpdateHistograms();
}




Bool_t MySpectrumFrame::ProcessMessage(Long_t msg, Long_t parm1, Long_t parm2)
{
    switch (GET_MSG(msg)) {
    case kC_COMMAND:
        switch (GET_SUBMSG(msg)) {
        case kCM_BUTTON:
            if (parm1 == kBtnFullRange) {
                OnFullRange();
                return kTRUE;
            }
            if (parm1 == kBtnZoomRange) {
                OnZoomRange();
                return kTRUE;
            }
            if (parm1 == kBtnClearSpectra) { 
                OnClearSpectra();
                return kTRUE;
            }
            if (parm1 == kBtnApplyRoi) {
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

    // Let base class handle other messages
    return TGMainFrame::ProcessMessage(msg, parm1, parm2);
} 
