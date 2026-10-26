/* Reading and writing graphics.cfg; the options and their keys are listed
   once, in settings_panel.cpp. */
#include "internal.h"
#include "../settings_panel.h"
extern "C" { extern int screenblocks,detailLevel; }

namespace doom3d {
const char *graphicsConfig="graphics.cfg";
void loadSettings() {
    FILE *file=fopen(graphicsConfig,"r"); if(!file) return;
    char key[64];float value;
    while(fscanf(file,"%63s %f",key,&value)==2) readOption(settings,key,value);
    finishReading(settings);
    fclose(file);
}
static void saveSettings() {
    FILE *file=fopen(graphicsConfig,"w"); if(!file)return;
    writeOptions(file,settings);
    fclose(file);
}
void settingsChanged() {applySettings();R_SetViewSize(screenblocks,detailLevel);saveSettings();tickTime=0;++settingsSerial;}
} // namespace doom3d
