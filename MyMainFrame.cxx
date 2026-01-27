#include <TGClient.h>
#include <TCanvas.h>
#include <TGButton.h>
#include <TGFrame.h>
#include <TGraph.h>
#include <TGLabel.h>
#include <TGFileDialog.h>
#include <TRootEmbeddedCanvas.h>
#include <TApplication.h>
#include <TFile.h>
#include <TGNumberEntry.h>
#include <TAxis.h>
#include <TTree.h>
#include <numeric>
#include "MyMainFrame.h"
#include "MyConfigFrame.h"
#include "MySpectrumFrame.h"
#include "device_config.h"
#include "spectrum_acq.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <fstream>
#include <cstdint>
#include <limits>



TGraph* gr[CHANNEL_NUMBER] = {nullptr};

//======================================================================
//  Utility: generate timestamp string
//======================================================================

static std::string ExpandTemplate(const std::string& pattern)
{
    std::string out = pattern;

    // Build timestamp pieces
    std::time_t t = std::time(nullptr);
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    char buf[64];

    // Replace %DATE%
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    while (out.find("%DATE%") != std::string::npos)
        out.replace(out.find("%DATE%"), 6, buf);

    // Replace %TIME%
    std::strftime(buf, sizeof(buf), "%H-%M-%S", &tm);
    while (out.find("%TIME%") != std::string::npos)
        out.replace(out.find("%TIME%"), 6, buf);

    return out;
}


//======================================================================
//  MAIN WINDOW ACTIONS
//======================================================================

void MyMainFrame::OpenConfig() {
    new MyConfigFrame(nullptr, 1200, 1000);
}

void MyMainFrame::CloseMainFrame() {
    if (connection_status == CONN_RUNNING) {
        connection_status = CONN_CONNECTED;
        UpdateStatusLabel();
        int wait_flag = 1;
        while (wait_flag == 1) {
            wait_flag = 0;
            for (int i = 0; i < CARD_NUMBER; ++i) wait_flag |= thread_lock_flag[i];
        }
    }
    if (auto_save_enabled) {
        close_run_file();
        std::cout << "[INFO] Closed run: " << current_run_name << ".bin\n";
    }
    close_spectrum_device();
    gApplication->Terminate(0);
}

void MyMainFrame::UpdateStatusLabel() {
    if (connection_status == CONN_CONNECTED) fGStatusLabel->SetText("CONNECTED");
    if (connection_status == CONN_ERROR) fGStatusLabel->SetText("ERROR");
    if (connection_status == CONN_DISCONNECTED) fGStatusLabel->SetText("DISCONNECTED");
    if (connection_status == CONN_RUNNING) fGStatusLabel->SetText("RUNNING");
}

void MyMainFrame::StartStopAcq() {
    if (connection_status == CONN_CONNECTED) {
        waveform_counter = 0;

        if (auto_save_enabled) {
            //current_run_name = "run_" + GetTimestampString();
            fWaveformCounterLabel->SetText(Form("%s.bin - Waveforms: 0",
                                                current_run_name.c_str()));
            std::cout << "[INFO] Starting new run: " << current_run_name << ".bin\n";

            // NEW: open raw run file
            std::string filename = current_run_name + ".bin";
            if (!open_run_file(filename)) {
                std::cerr << "[ERROR] Could not open run file " << filename << std::endl;
                // optional: disable auto-save if you want
                auto_save_enabled = false;
                fAutoSaveButton->SetText("Save: OFF");
            }
        } else {
            
            fWaveformCounterLabel->SetText(Form("Waveforms: 0"));
        }

        start_run_visual_timer();
        
        connection_status = CONN_RUNNING;
        UpdateStatusLabel();
        start_spectrum_acq();
        return;
    }

    if (connection_status == CONN_RUNNING) {
        stop_spectrum_acq();                // stop HW
        connection_status = CONN_CONNECTED;

        if (auto_save_enabled) {
            close_run_file();
            std::cout << "[INFO] Closed run: " << current_run_name << ".bin\n";
            auto_save_enabled = false;
            fAutoSaveButton->SetText("Save: OFF");
            current_run_name = "";
            fRunFileLabel->SetText("Current file: run_%DATE%_%TIME%.bin");
        }

        // wait for your worker threads to be idle
        int wait_flag = 1;
        while (wait_flag == 1) {
            wait_flag = 0;
            for (int i = 0; i < CARD_NUMBER; ++i) wait_flag |= thread_lock_flag[i];
        }
        UpdateStatusLabel();

    }
}

void MyMainFrame::ConnectDevice() {
    if (open_spectrum_device()) {
        connection_status = CONN_CONNECTED;
        UpdateStatusLabel();
    } else {
        connection_status = CONN_ERROR;
        UpdateStatusLabel();
        close_spectrum_device();
        return;
    }

    if (configure_spectrum_device()) {
        connection_status = CONN_CONNECTED;
        UpdateStatusLabel();
    } else {
        connection_status = CONN_ERROR;
        UpdateStatusLabel();
        close_spectrum_device();
    }
}

void MyMainFrame::OnSelectFile()
{
    static const char* filetypes[] = {
        "Binary files", "*.bin",
        "All files",    "*",
        nullptr,        nullptr
    };

    TGFileInfo fi;
    fi.fFileTypes = filetypes;
    fi.fIniDir    = nullptr;  // or StrDup(".")

    fi.fFilename  = nullptr;  // IMPORTANT: do NOT point to std::string memory

    new TGFileDialog(gClient->GetRoot(), this, kFDSave, &fi);

    if (!fi.fFilename) {
        // user cancelled
        return;
    }

    std::string chosen = fi.fFilename;  // ROOT owns the allocation, you just read it

    // Allow templates typed by user, like run_$DATE$_$TIME$.bin
    chosen = ExpandTemplate(chosen);

    // Strip .bin to keep current_run_name as "base"
    if (chosen.size() > 4 &&
        chosen.substr(chosen.size() - 4) == ".bin") {
        chosen = chosen.substr(0, chosen.size() - 4);
    }

    current_run_name = chosen;

    std::cout << "[INFO] Selected run file base name: "
              << current_run_name << ".bin" << std::endl;

    // Update labels (see next section)
    if (fRunFileLabel) {
        fRunFileLabel->SetText(
            Form("File: %s.bin", current_run_name.c_str())
        );
    }

    if (auto_save_enabled) {
        fWaveformCounterLabel->SetText(
            Form("Waveforms: %d", waveform_counter)
        );
    }
}


//======================================================================
//  REFRESH GRAPH
//======================================================================

void MyMainFrame::RefreshGraph() {
    if (SampleRate == 0)
        return;

    const int outN = 2048;
    const int step = std::max(1, ADC_EVENT_SIZE / outN);

    for (int ch = 0; ch < CHANNEL_NUMBER; ++ch) {
        if (!display_refresh_flags[ch]) continue;

        if (!gr[ch]) gr[ch] = new TGraph(outN);

        for (int i = 0; i < outN; ++i) {
            int j = i * step;
            if (j >= ADC_EVENT_SIZE) j = ADC_EVENT_SIZE - 1;
            double x = 1e6 * j / (double)SampleRate;
            double y = display_data[ch][i];
            gr[ch]->SetPoint(i, x, y);
        }

        //gr[ch]->SetLineColor(kBlue);
        gr[ch]->SetTitle(Form("Channel %d", ch));
        display_refresh_flags[ch] = 0;
    
        TCanvas* c = fEcanvas1->GetCanvas();
        c->cd();
        gr[ch]->Draw("AL SAME");  // "SAME" overlays both channels
        gr[ch]->SetLineColor(kRed);
        gr[ch]->GetXaxis()->SetTitle("Time [#mus]");
        gr[ch]->GetYaxis()->SetTitle("Amplitude [ADC counts]");
        c->Modified();
        c->Update();

        if (gr[ch] && gr[ch]->GetN() > 0) {
            waveform_counter++;
            if (auto_save_enabled){
                fWaveformCounterLabel->SetText(Form("Waveforms: %d",
                                                    waveform_counter));
                if(waveform_save_limit > 0 && waveform_counter >= waveform_save_limit){
                    if (connection_status == CONN_RUNNING) {
                        std::cout << "[INFO] Max waveforms reached (" << waveform_counter << "), stopping acquisition\n";
                        StartStopAcq();
                    }
                }
            }
            else{
                fWaveformCounterLabel->SetText(Form("Waveforms: %d",
                                                    waveform_counter));}

        }
    }

}

//======================================================================
//  SLOT: CHANGE SAVE LIMIT
//======================================================================

void MyMainFrame::SetWaveformSaveLimit() {
    waveform_save_limit = (int)fWaveformCountEntry->GetNumber();
    if (auto_save_enabled && !current_run_name.empty())
        fWaveformCounterLabel->SetText(Form("Waveforms: %d",
                                            waveform_counter));
    else
        fWaveformCounterLabel->SetText(Form("Waveforms: %d",
                                            waveform_counter));
    std::cout << "[INFO] Waveform save limit set to " << waveform_save_limit << std::endl;
}

//======================================================================
//  SLOT: TOGGLE AUTO SAVE
//======================================================================

void MyMainFrame::ToggleAutoSave() {
    auto_save_enabled = !auto_save_enabled;
    if (auto_save_enabled) {
        //current_run_name = "run_" + GetTimestampString();
        if (current_run_name.empty()) {
        current_run_name = ExpandTemplate("run_%DATE%_%TIME%");
        }
        waveform_counter = 0;
        fAutoSaveButton->SetText("Save: ON");
        fWaveformCounterLabel->SetText(Form("Waveforms: 0"));
        if (fRunFileLabel) {
            fRunFileLabel->SetText(
                Form("File: %s.bin", current_run_name.c_str())
            );
        }
        std::cout << "[INFO] Save enabled - new run: "
                  << current_run_name << std::endl;
    } else {
        fAutoSaveButton->SetText("Save: OFF");
        fWaveformCounterLabel->SetText(Form("Waveforms: %d",
                                            waveform_counter));
        std::cout << "[INFO] Save disabled\n";
    }
}



void MyMainFrame::OnShowSpectra()
{
    if (!fSpectrumFrame) {
        fSpectrumFrame = new MySpectrumFrame(gClient->GetRoot(), 1000, 600);
    } else {
        fSpectrumFrame->MapRaised();
    }
}



//======================================================================
//  CONSTRUCTOR
//======================================================================

MyMainFrame::MyMainFrame(const TGWindow *p, UInt_t w, UInt_t h)
    : TGMainFrame(p, w, h) {
    Resize(w, h);
    SetWindowName("SPECTRUM ACQ");
    fSpectrumFrame = nullptr;

    fGStatusFrame = new TGGroupFrame(this, new TGString("STATUS"), kHorizontalFrame);
    fGStatusLabel = new TGLabel(fGStatusFrame, new TGString("DISCONNECTED"));
    fGStatusFrame->AddFrame(fGStatusLabel, new TGLayoutHints(kLHintsTop | kLHintsExpandX,5,5,2,2));
    AddFrame(fGStatusFrame, new TGLayoutHints(kLHintsTop | kLHintsExpandX,5,5,2,2));

    TGHorizontalFrame *canvasFrame = new TGHorizontalFrame(this);
    fEcanvas1 = new TRootEmbeddedCanvas("Ecanvas1", canvasFrame, 400, 400);
    canvasFrame->AddFrame(fEcanvas1, new TGLayoutHints(kLHintsExpandX | kLHintsExpandY,5,5,5,5));
    AddFrame(canvasFrame, new TGLayoutHints(kLHintsExpandX | kLHintsExpandY,10,10,10,10));


    fConnect = new TGTextButton(this, "Co&nnect");
    fConnect->Connect("Clicked()", "MyMainFrame", this, "ConnectDevice()");
    AddFrame(fConnect, new TGLayoutHints(kLHintsTop | kLHintsExpandX,5,5,2,2));

    fRunStop = new TGTextButton(this, "&Start Acq/Stop Acq");
    fRunStop->Connect("Clicked()", "MyMainFrame", this, "StartStopAcq()");
    AddFrame(fRunStop, new TGLayoutHints(kLHintsTop | kLHintsExpandX,5,5,2,2));
    

    fConfig = new TGTextButton(this, "&Config");
    fConfig->Connect("Clicked()", "MyMainFrame", this, "OpenConfig()");
    AddFrame(fConfig, new TGLayoutHints(kLHintsTop | kLHintsExpandX,5,5,2,2));

    showSpectraButton= new TGTextButton(this, "&Show Spectra");
    showSpectraButton->Connect("Clicked()", "MyMainFrame", this, "OnShowSpectra()");
    AddFrame(showSpectraButton, new TGLayoutHints(kLHintsTop | kLHintsExpandX,5,5,2,2));

    TGHorizontalFrame *fSaveCountFrame = new TGHorizontalFrame(this);
    auto *label = new TGLabel(fSaveCountFrame, "Waveforms to keep:");
    fSaveCountFrame->AddFrame(label, new TGLayoutHints(kLHintsLeft,5,5,2,2));
    waveform_save_limit = 0;
    fWaveformCountEntry = new TGNumberEntry(fSaveCountFrame, waveform_save_limit, 12, -1,
                                            TGNumberFormat::kNESInteger,
                                            TGNumberFormat::kNEANonNegative,
                                            TGNumberFormat::kNELLimitMinMax, 1, std::numeric_limits<long long>::max());
    fWaveformCountEntry->Connect("ValueSet(Long_t)", "MyMainFrame", this, "SetWaveformSaveLimit()");
    fSaveCountFrame->AddFrame(fWaveformCountEntry, new TGLayoutHints(kLHintsLeft,5,5,2,2));
    AddFrame(fSaveCountFrame, new TGLayoutHints(kLHintsTop | kLHintsExpandX,5,5,2,2));


    waveform_counter = 0;
    //current_run_name.clear();
    fWaveformCounterLabel = new TGLabel(this, "Waveforms: 0");
    AddFrame(fWaveformCounterLabel, new TGLayoutHints(kLHintsTop | kLHintsExpandX,5,5,2,2));

    TGTextButton* fSelectFileBtn = new TGTextButton(this, "&Select File");
    fSelectFileBtn->Connect("Clicked()", "MyMainFrame", this, "OnSelectFile()");
    AddFrame(fSelectFileBtn, new TGLayoutHints(kLHintsTop | kLHintsExpandX,5,5,2,2));

    fRunFileLabel = new TGLabel(this, "Current file: run_%DATE%_%TIME%.bin");
    AddFrame(fRunFileLabel,
    new TGLayoutHints(kLHintsTop | kLHintsExpandX, 5, 5, 2, 2));


    fAutoSaveButton = new TGTextButton(this, "Save: OFF");
    fAutoSaveButton->Connect("Clicked()", "MyMainFrame", this, "ToggleAutoSave()");
    AddFrame(fAutoSaveButton, new TGLayoutHints(kLHintsTop | kLHintsExpandX,5,5,2,2));
    auto_save_enabled = false;


    fExit = new TGTextButton(this, "&Exit");
    fExit->Connect("Clicked()", "MyMainFrame", this, "CloseMainFrame()");
    AddFrame(fExit, new TGLayoutHints(kLHintsTop | kLHintsExpandX,5,5,2,2));

    Layout();
    MapSubwindows();
    MapWindow();

    Connect("CloseWindow()", "MyMainFrame", this, "CloseMainFrame()");
    DontCallClose();

    if (!read_config_file()) OpenConfig();

    refresh_timer = new TTimer(10);
    refresh_timer->Connect("Timeout()", "MyMainFrame", this, "RefreshGraph()");
    refresh_timer->TurnOn();
}

//======================================================================
//  DESTRUCTOR
//======================================================================

MyMainFrame::~MyMainFrame() {
    Cleanup();
}
