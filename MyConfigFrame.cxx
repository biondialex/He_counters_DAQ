#include <TGClient.h>
#include <TGString.h>
#include <TGButton.h>
#include <TGLabel.h>
#include <TGFrame.h>
#include <TGTextEntry.h>
#include <TGTextBuffer.h>
#include <TQObject.h>
#include <TGComboBox.h>
#include "MyConfigFrame.h"
#include <string>
#include <iostream>
#include <fstream>
#include "device_config.h"
#include "spectrum_acq.h"
#include "main.h"

// configuration frame


void MyConfigFrame::CloseConfigFrame() {
    DeleteWindow();
}

MyConfigFrame::MyConfigFrame(const TGWindow *p, UInt_t w, UInt_t h) :
  TGMainFrame(p, w, h)
{

   Resize(w,h);
   SetWindowName("Configuration"); 

   fGframes[CHANNEL_NUMBER] = new TGGroupFrame(this , new TGString ("Device IP") , kHorizontalFrame );
   ip_entry = new TGTextEntry ( fGframes[CHANNEL_NUMBER] , new TGTextBuffer (64) );
   ip_entry -> AppendText (device_ip_addr.c_str());
   fGframes[CHANNEL_NUMBER]-> AddFrame(ip_entry, new TGLayoutHints(kLHintsTop | kLHintsExpandX, 3, 2, 2, 2));
   AddFrame(fGframes[CHANNEL_NUMBER], new TGLayoutHints(kLHintsTop | kLHintsExpandX ,5,5,2,2));


   for (int channel = 0 ; channel < CHANNEL_NUMBER ; channel ++ ) {
	std::string channel_name = "CH" + std::to_string(channel) + ":";

        fGframes[channel] = new TGGroupFrame(this , new TGString(channel_name.c_str()) , kHorizontalFrame);

        en_cButton [channel] = new TGCheckButton (fGframes[channel] , "Enabled" , 0);
	if ( channel_enabled [channel] ) en_cButton [channel] -> SetState (kButtonDown); else en_cButton [channel] -> SetState (kButtonUp); 
        fGframes[channel] -> AddFrame(en_cButton [channel] , new TGLayoutHints(kLHintsTop ,5,5,2,2));

        polN_cButton [channel] = new TGCheckButton (fGframes[channel] , "TR neg" , 0);
	if ( channel_trig_pol [channel] == 1) polN_cButton [channel] -> SetState (kButtonDown); else polN_cButton [channel] -> SetState (kButtonUp); 
        fGframes[channel] -> AddFrame(polN_cButton [channel] , new TGLayoutHints(kLHintsTop | kLHintsExpandX,5,5,2,2));

        polP_cButton [channel] = new TGCheckButton (fGframes[channel] , "TR pos" , 0);
	if ( channel_trig_pol [channel] == 2) polP_cButton [channel] -> SetState (kButtonDown); else polP_cButton [channel] -> SetState (kButtonUp); 
        fGframes[channel] -> AddFrame(polP_cButton [channel] , new TGLayoutHints(kLHintsTop | kLHintsExpandX,5,5,2,2));

	trg_label[channel] =  new TGLabel (fGframes[channel] , new TGString ("TR Level [ADC]:"));
        fGframes[channel] -> AddFrame(trg_label [channel] , new TGLayoutHints(kLHintsTop | kLHintsExpandX,5,5,2,2));
	trg_entry[channel] = new TGTextEntry ( fGframes[channel] , new TGTextBuffer (6) );
	trg_entry[channel] -> AppendText (std::to_string(channel_thr[channel]).c_str());
        fGframes[channel] -> AddFrame(trg_entry [channel] , new TGLayoutHints(kLHintsTop | kLHintsExpandX,5,5,2,2));

	ofs_label[channel] =  new TGLabel (fGframes[channel] , new TGString ("Offset [%]:"));
        fGframes[channel] -> AddFrame(ofs_label [channel] , new TGLayoutHints(kLHintsTop | kLHintsExpandX,5,5,2,2));
	ofs_entry[channel] = new TGTextEntry ( fGframes[channel] , new TGTextBuffer (6) );
	ofs_entry[channel] -> AppendText (std::to_string(channel_ofs[channel]).c_str());
        fGframes[channel] -> AddFrame(ofs_entry [channel] , new TGLayoutHints(kLHintsTop | kLHintsExpandX,5,5,2,2));


        range_label[channel] = new TGLabel(fGframes[channel], new TGString("ADC Range [mV]:"));
        fGframes[channel]->AddFrame(range_label[channel],
        new TGLayoutHints(kLHintsTop | kLHintsExpandX, 5,5,2,2));

        range_combo[channel] = new TGComboBox(fGframes[channel]);
        range_combo[channel]->AddEntry("200", 200);
        range_combo[channel]->AddEntry("500", 500);
        range_combo[channel]->AddEntry("1000", 1000);
        range_combo[channel]->AddEntry("2500", 2500);

        // select current channel value
        int r = channel_adc_range[channel];
        if (r != 200 && r != 500 && r != 1000 && r != 2500) r = ADC_RANGE;
        range_combo[channel]->Select(r);

        range_combo[channel]->Connect("Selected(Int_t)", "MyConfigFrame", this, "OnAdcRangeSelected(Int_t)");

        fGframes[channel]->AddFrame(range_combo[channel],
        new TGLayoutHints(kLHintsLeft | kLHintsExpandX, 5,5,2,2));


        prewin_label[channel] = new TGLabel(fGframes[channel],new TGString("Samples bef PTR:"));
        fGframes[channel]->AddFrame(prewin_label[channel],new TGLayoutHints(kLHintsTop | kLHintsExpandX,5,5,2,2));
        prewin_entry[channel] = new TGTextEntry(fGframes[channel], new TGTextBuffer(10));
        prewin_entry[channel]->SetText(Form("%d", channel_search_pre[channel]));
        fGframes[channel]->AddFrame(prewin_entry[channel],new TGLayoutHints(kLHintsTop | kLHintsExpandX,5,5,2,2));

        postwin_label[channel] = new TGLabel(fGframes[channel],new TGString("Samples aft PTR:"));
        fGframes[channel]->AddFrame(postwin_label[channel],new TGLayoutHints(kLHintsTop | kLHintsExpandX,5,5,2,2));
        postwin_entry[channel] = new TGTextEntry(fGframes[channel], new TGTextBuffer(10));
        postwin_entry[channel]->SetText(Form("%d", channel_search_post[channel]));
        fGframes[channel]->AddFrame(postwin_entry[channel],new TGLayoutHints(kLHintsTop | kLHintsExpandX,5,5,2,2));


        AddFrame(fGframes[channel], new TGLayoutHints(kLHintsTop | kLHintsExpandX,5,5,2,2));

    }

   fExit = new TGTextButton(this, "&Exit ");
   fExit->Connect ("Clicked()", "MyConfigFrame" , this , "CloseConfigFrame()");
   AddFrame(fExit, new TGLayoutHints(kLHintsTop | kLHintsExpandX,5,5,2,2));

   Layout ();
   MapSubwindows();
   MapWindow();
   
   Connect("CloseWindow()", "MyConfigFrame", this, "CloseConfigFrame()");
   DontCallClose();
}


MyConfigFrame::~MyConfigFrame()
{
    // Save configuration to file
    std::ofstream config_file("spectr_acq.cfg");
    config_file << "IP_ADDR=" << ip_entry->GetBuffer()->GetString() << std::endl;
    device_ip_addr = ip_entry->GetBuffer()->GetString();

    for (int channel = 0; channel < CHANNEL_NUMBER; ++channel) {
        std::string channel_name = "CH" + std::to_string(channel) + "_";

        config_file << channel_name << "ENABLED="
                    << en_cButton[channel]->IsOn() << std::endl;
        channel_enabled[channel] = en_cButton[channel]->IsOn();

        channel_trig_pol[channel] =
            polN_cButton[channel]->IsOn() +
            2 * polP_cButton[channel]->IsOn();
        config_file << channel_name << "TR_POL="
                    << channel_trig_pol[channel] << std::endl;

        int r = range_combo[channel]->GetSelected();
        if (r != 200 && r != 500 && r != 1000 && r != 2500) r = ADC_RANGE;
        channel_adc_range[channel] = r;
        config_file << channel_name << "ADC_RANGE=" << r << std::endl;

        int t_trLevel = std::atoi(trg_entry[channel]->GetBuffer()->GetString());
        if (t_trLevel >  r) t_trLevel =  r;
        if (t_trLevel < -r) t_trLevel = -r;
        channel_thr[channel] = t_trLevel;
        config_file << channel_name << "TR_LEVEL="
                    << t_trLevel << std::endl;

        int t_ofs = std::atoi(ofs_entry[channel]->GetBuffer()->GetString());
        if (t_ofs >  r) t_ofs =  r;
        if (t_ofs < -r) t_ofs = -r;
        channel_ofs[channel] = t_ofs;
        config_file << channel_name << "OFS=" << t_ofs << std::endl;

        int t_pre  = std::atoi(prewin_entry[channel]->GetBuffer()->GetString());
        channel_search_pre[channel] = t_pre;
        config_file << channel_name << "SRCH_PRE=" << t_pre << std::endl;

        int t_post = std::atoi(postwin_entry[channel]->GetBuffer()->GetString());
        channel_search_post[channel] = t_post;
        config_file << channel_name << "SRCH_POST="
                    << t_post << std::endl;

    }

    config_file.close();

    if (connection_status == CONN_CONNECTED) {
        if (configure_spectrum_device()) {
            connection_status = CONN_CONNECTED;
            mFrame->UpdateStatusLabel();
        } else {
            connection_status = CONN_ERROR;
            mFrame->UpdateStatusLabel();
            close_spectrum_device();
        }
    }

}


void MyConfigFrame::OnAdcRangeSelected(Int_t id)
{
    // identify which combo emitted the signal
    TGComboBox* sender = static_cast<TGComboBox*>(gTQSender);
    if (!sender) return;

    int ch = -1;
    for (int i = 0; i < CHANNEL_NUMBER; ++i) {
        if (range_combo[i] == sender) { ch = i; break; }
    }
    if (ch < 0) return;

    int r = id;
    if (r != 200 && r != 500 && r != 1000 && r != 2000) r = ADC_RANGE;

    channel_adc_range[ch] = r;

    // Clamp the current trigger/offset text to ±r (safe + matches your request)
    int thr = std::atoi(trg_entry[ch]->GetBuffer()->GetString());
    int ofs = std::atoi(ofs_entry[ch]->GetBuffer()->GetString());

    if (thr >  r) thr =  r;
    if (thr < -r) thr = -r;
    if (ofs >  r) ofs =  r;
    if (ofs < -r) ofs = -r;

    trg_entry[ch]->SetText(Form("%d", thr));
    ofs_entry[ch]->SetText(Form("%d", ofs));

    Layout();
}


