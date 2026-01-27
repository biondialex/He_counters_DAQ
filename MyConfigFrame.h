#include <TGClient.h>
#include <TGButton.h>
#include <TGLabel.h>
#include <TGFrame.h>
#include <TGTextEntry.h>
#include <TGComboBox.h>
#include "device_config.h"

// configuration window
class MyConfigFrame : public TGMainFrame {

private:
   TGGroupFrame     *fGframes [CHANNEL_NUMBER+1] ;
   TGTextButton     *fExit;
   TGLabel 	        *trg_label [CHANNEL_NUMBER] , *ofs_label [CHANNEL_NUMBER];
   TGTextEntry      *ip_entry , *trg_entry [CHANNEL_NUMBER] , *ofs_entry [CHANNEL_NUMBER]; 
   TGCheckButton    *en_cButton [CHANNEL_NUMBER] , *polN_cButton [CHANNEL_NUMBER] , *polP_cButton [CHANNEL_NUMBER];
   
   TGLabel          *prewin_label[CHANNEL_NUMBER], *postwin_label[CHANNEL_NUMBER];
   TGTextEntry      *prewin_entry[CHANNEL_NUMBER], *postwin_entry[CHANNEL_NUMBER];

   TGLabel    *range_label[CHANNEL_NUMBER];
   TGComboBox *range_combo[CHANNEL_NUMBER];




public:
   MyConfigFrame(const TGWindow *p, UInt_t w, UInt_t h);
   virtual ~MyConfigFrame();
   // slots
   void CloseConfigFrame();

   void OnAdcRangeSelected(Int_t id);


   ClassDef(MyConfigFrame, 0)
};
