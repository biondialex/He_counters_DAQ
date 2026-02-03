#ifndef MY_OFFLINE_SPECTRUM_FRAME_H
#define MY_OFFLINE_SPECTRUM_FRAME_H

#include <TGFrame.h>
#include <TRootEmbeddedCanvas.h>
#include <TGButton.h>
#include <TGNumberEntry.h>
#include <TGLabel.h>
#include <TLine.h>
#include <TPaveText.h>
#include <TH1F.h>
#include <vector>
#include <string>
#include "device_config.h"
#include "spectrum_acq.h"

enum {
    kOffBtnSelectFile   = 2001,
    kOffBtnFullRange    = 2002,
    kOffBtnZoomRange    = 2003,
    kOffBtnClearSpectra = 2004,
    kOffBtnApplyRoi     = 2005
};

class MyOfflineSpectrumFrame : public TGMainFrame {
public:
    MyOfflineSpectrumFrame(const TGWindow* p, UInt_t w, UInt_t h);
    virtual ~MyOfflineSpectrumFrame();

    virtual Bool_t ProcessMessage(Long_t msg, Long_t parm1, Long_t parm2) override;

private:
    TRootEmbeddedCanvas* fCanvas;
    TCanvas*             fCan;
    TH1F*                fHist[CHANNEL_NUMBER];

    TGHorizontalFrame*   fButtonFrame;
    TGTextButton*        fBtnSelectFile;
    TGTextButton*        fBtnFullRange;
    TGTextButton*        fBtnZoom;
    TGTextButton*        fBtnClear;

    TGLabel*             fFileLabel;
    TGLabel*             fStatusLabel;
    TGLabel*             fProgressLabel;

    double               fZoomMax;
    double               fCurrentXMax;
    int                  fLastActiveChannels;
    bool                 fAxisDirty;

    // ROI values per channel
    double fRoiLow[CHANNEL_NUMBER];
    double fRoiHigh[CHANNEL_NUMBER];

    // ROI UI
    TGVerticalFrame* fRoiPanel;
    TGNumberEntry* fRoiLowEntry[CHANNEL_NUMBER];
    TGNumberEntry* fRoiHighEntry[CHANNEL_NUMBER];
    TGTextButton* fBtnApplyRoi;

    // Drawing helpers (one per channel, reused)
    TLine fRoiLineLow[CHANNEL_NUMBER];
    TLine fRoiLineHigh[CHANNEL_NUMBER];
    TPaveText* fRoiPave[CHANNEL_NUMBER];
    bool fHistDrawn[CHANNEL_NUMBER];
    bool fRoiDirty[CHANNEL_NUMBER];

    std::vector<uint64_t> fCounts[CHANNEL_NUMBER];
    bool   fChannelActive[CHANNEL_NUMBER];
    double fAdcRange[CHANNEL_NUMBER];
    double fElapsedSeconds;
    std::string fCurrentFilePath;

    void UpdateHistograms();
    void CloseWindow() override;

    void OnSelectFile();
    void OnFullRange();
    void OnZoomRange();
    void OnClearSpectra();
    void OnApplyRoi();

    void LoadFile(const std::string& path);
    void ResetSpectra();
    void ApplyCountsToHist();
    void UpdateProgress(int percent);
};

#endif
