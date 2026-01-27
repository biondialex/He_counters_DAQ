#ifndef MY_SPECTRUM_FRAME_H
#define MY_SPECTRUM_FRAME_H

#include <TGFrame.h>
#include <TRootEmbeddedCanvas.h>
#include <TTimer.h>
#include <TH1F.h>
#include <TGButton.h>
#include <TPaveText.h>
#include <TGNumberEntry.h>
#include <TLine.h>
#include <TGLabel.h>
#include <TFrame.h>
#include "device_config.h"
#include "spectrum_acq.h"  // for CHANNEL_NUMBER, ENERGY_NBINS, ENERGY_MIN, ENERGY_MAX


enum {
    kBtnFullRange = 1001,
    kBtnZoomRange = 1002,
    kBtnClearSpectra = 1003,
    kBtnApplyRoi      = 1004
};

class MySpectrumFrame : public TGMainFrame {
public:
    MySpectrumFrame(const TGWindow* p, UInt_t w, UInt_t h);
    virtual ~MySpectrumFrame();

    Bool_t HandleTimer(TTimer* t);
    virtual Bool_t ProcessMessage(Long_t msg, Long_t parm1, Long_t parm2) override;

private:
    TRootEmbeddedCanvas* fCanvas;
    TCanvas*             fCan;
    TH1F*                fHist[CHANNEL_NUMBER];
    TTimer*              fTimer;
    TGHorizontalFrame*   fButtonFrame;
    TGTextButton*        fBtnFullRange;
    TGTextButton*        fBtnZoom;
    TGTextButton*        fBtnClear;
    double               fZoomMax;
    double               fCurrentXMax;
    int                  fLastActiveChannels;

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


    void UpdateHistograms();
    void CloseWindow();

    void OnFullRange();
    void OnZoomRange();
    void OnClearSpectra();
    void OnApplyRoi();



};

#endif
