#include "main.h"
#include <iostream>
#include "device_config.h"
#include "spectrum_acq.h"

MyMainFrame *mFrame;
TThread *acq_thread[4];


using namespace std;


int main(int argc, char** argv)
{
   TApplication theApp("SPECT_GUI", &argc, argv); 

   uint32_t thread_idx_table [CARD_NUMBER];
   for (int acq_thread_idx = 0 ; acq_thread_idx < CARD_NUMBER ; acq_thread_idx ++) {
       thread_idx_table [acq_thread_idx] = acq_thread_idx;
       acq_thread [acq_thread_idx] = new TThread ("acq_t" , acq_loop , (void*) &thread_idx_table[acq_thread_idx]);
       acq_thread[acq_thread_idx]-> Run();
   }

   mFrame = new MyMainFrame(gClient->GetRoot(), 612, 500);
   theApp.Run();
   return 0;
}
