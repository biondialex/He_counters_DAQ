#ifndef MYMAINFRAME_H
#define MYMAINFRAME_H

#include <TGClient.h>
#include <TGButton.h>
#include <TGFrame.h>
#include <TGLabel.h>
#include <TTimer.h>
#include <TRootEmbeddedCanvas.h>
#include <TGNumberEntry.h>
#include <TGraph.h>
#include "device_config.h"

extern TGraph* gr[CHANNEL_NUMBER];

class MySpectrumFrame;
class MyOfflineSpectrumFrame;

class MyMainFrame : public TGMainFrame {
private:
   TGCompositeFrame *fCframe;
   TGTextButton     *fConnect, *fRunStop, *fConfig, *fExit, *showSpectraButton, *showOfflineSpectraButton;
   TGGroupFrame     *fGStatusFrame;
   TGLabel          *fGStatusLabel;
   TGLabel          *fWaveformCounterLabel;
   TRootEmbeddedCanvas *fEcanvas1;
   TTimer           *refresh_timer;

   TGNumberEntry    *fWaveformCountEntry;
   TGTextButton     *fAutoSaveButton;
   MySpectrumFrame *fSpectrumFrame;
   MyOfflineSpectrumFrame *fOfflineFrame;
   TGLabel* fRunFileLabel;

   int waveform_save_limit;
   int waveform_counter;
   bool auto_save_enabled;
   std::string current_run_name;  // unique run file name with timestamp
   


public:
   MyMainFrame(const TGWindow *p, UInt_t w, UInt_t h);
   virtual ~MyMainFrame();

   void OpenConfig();
   void CloseMainFrame();
   void ConnectDevice();
   void UpdateStatusLabel();
   void StartStopAcq();
   void RefreshGraph();
   void OnShowSpectra();
   void OnShowOfflineSpectra();
   void OnSelectFile();


   void SetWaveformSaveLimit();
   void ToggleAutoSave();   // toggles the state of auto-save

   ClassDef(MyMainFrame, 0)
};

#endif
