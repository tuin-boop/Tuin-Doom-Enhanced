/* The in-game F4 graphics panel. It draws into its own 640x400 image on a
 * grid of 8x16 cells, like a text-mode setup program, in the WAD's own small
 * font (STCFN), uppercase as in DOOM's messages; the renderer scales it by
 * whole pixels over the dimmed view. Changes apply when the panel closes. */
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
extern "C" {
#include "doomtype.h"
#include "doomdef.h"
#include "s_sound.h"
#include "w_wad.h"
#include "z_zone.h"
#include "doomstat.h"
#include "g_game.h"
extern boolean paused;
}
#include <SDL3/SDL.h>
#include "i_render3d.h"
#include "scene3d.h"
#include "settings_panel.h"
#include "game_profile.h"

using namespace doom3d;

namespace {
// One row of the current tab: a section heading (option null) or an option.
struct Line { const OptionSection *section; Option *option; };

struct Panel {
    bool open=false,wasPaused=false;
    int aiWarning=0;
    Settings draft,defaults; // defaults: as R sets them, for the per-option reset and marks
    std::vector<OptionTab> tabs;
    std::vector<Line> lines;
    int tab=0,selected=0,scroll=0;
    
} panel;
static int selectedMap=0;
static std::vector<std::string> mapNames;
static std::vector<std::pair<int,int>> mapNumbers;
void closePanel();
void startSelectedMap(){
    if(selectedMap<0||selectedMap>=(int)mapNumbers.size())return;
    auto map=mapNumbers[selectedMap];closePanel();
    G_DeferedInitNew(gameskill,map.first,map.second);
}
void addMapTab(){
    mapNames.clear();mapNumbers.clear();selectedMap=0;
    for(int episode=1;episode<=(gamemode==commercial?1:9);++episode)
        for(int map=1;map<=(gamemode==commercial?32:9);++map){
            char name[9];if(gamemode==commercial)snprintf(name,sizeof(name),"MAP%02d",map);else snprintf(name,sizeof(name),"E%dM%d",episode,map);
            if(W_CheckNumForName(name)<0)continue;
            if(episode==gameepisode&&map==gamemap)selectedMap=(int)mapNames.size();
            mapNames.emplace_back(name);mapNumbers.emplace_back(episode,map);
        }
    if(mapNames.empty())return;
    Option map{};map.kind=OptionKind::Choice;map.key="";map.title="Map";map.tip="Choose a loaded map, then select Start map. This starts a new game on that map.";map.flag=&selectedMap;
    for(size_t n=0;n<mapNames.size();++n){map.items.push_back(mapNames[n].c_str());map.values.push_back((int)n);}
    Option start{};start.kind=OptionKind::Action;start.key="";start.title="Start map";start.tip="Start the selected map at the current difficulty. Current level progress will be replaced.";start.action=startSelectedMap;
    panel.tabs.insert(panel.tabs.begin(),{"Game",{{"Change map",{map,start}}}});
}

// The image is a grid of 8x16 character cells, 80 columns by 25 rows.
const int width=DOOM_SETTINGS_WIDTH,height=DOOM_SETTINGS_HEIGHT,cellWidth=8,cellHeight=16;
const int listRow=2,visibleLines=17,descriptionRow=20,descriptionLines=4,helpRow=24;
const int leftColumn=20,rightColumn=78,sliderColumns=14,valueColumns=6;

// Colors as 0xAARRGGBB, the byte order of the B8G8R8A8 texture.
const unsigned backdrop=0xc8080808,separator=0xff3a3a3a,text=0xffe6e6e6,heading=0xffe8b040,dim=0xff686868,
               description=0xffb4b4b4,selectionBar=0xff7a1414,bright=0xffffffff,onColor=0xff8cd48c,
               tabBar=0xffa01818,track=0xff505050,trackFill=0xffd03030;

unsigned *image;

void fill(int x,int y,int w,int h,unsigned color) {
    for(int row=std::max(0,y);row<std::min(height,y+h);++row)
        std::fill(image+row*width+std::max(0,x),image+row*width+std::min(width,x+w),color);
}

// The game's small font ('!' to '_', Game_FontLump: DOOM's STCFN, Heretic's
// FONTA) as brightness masks: 0 is transparent, otherwise the palette color's
// brightness relative to the font's brightest pixel, so the artwork's
// shading survives any text color. glyphHeight is the tallest letter's.
struct Glyph { int width=0,height=0,left=0,top=0;std::vector<unsigned char> shade; };
const int firstGlyph='!',lastGlyph='_',spaceWidth=4;
int glyphHeight=7;
std::vector<Glyph> glyphs;

void loadFont() {
    if(!glyphs.empty())return;
    glyphs.resize(lastGlyph-firstGlyph+1);
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    std::vector<int> brightness(256);
    for(int i=0;i<256;++i)brightness[i]=palette[3*i]*299+palette[3*i+1]*587+palette[3*i+2]*114;
    std::vector<std::vector<int>> raw(glyphs.size());
    int brightest=1,letterHeight=0;
    for(int c=firstGlyph;c<=lastGlyph;++c) {
        int lump=Game_FontLump(c);
        if(lump<0)continue;
        const byte *data=(const byte*)W_CacheLumpNum(lump,PU_CACHE);
        int size=W_LumpLength(lump);
        if(size<8)continue;
        Glyph &glyph=glyphs[(size_t)(c-firstGlyph)];
        int w=(short)(data[0]|data[1]<<8),h=(short)(data[2]|data[3]<<8);
        if(w<=0||h<=0||w>32||h>32||8+4*w>size)continue;
        std::vector<int> &pixels=raw[(size_t)(c-firstGlyph)];
        pixels.assign((size_t)(w*h),-1);
        // Patch columns: posts of (top, length, pad, pixels, pad) up to 0xff.
        for(int x=0;x<w;++x) {
            int at=data[8+4*x]|data[9+4*x]<<8|data[10+4*x]<<16|data[11+4*x]<<24;
            while(at>=0&&at<size&&data[at]!=0xff) {
                if(at+2>=size)break;
                int top=data[at],length=data[at+1];
                if(at+3+length>size)break;
                for(int y=0;y<length;++y)if(top+y<h) {
                    int index=data[at+3+y];
                    pixels[(size_t)((top+y)*w+x)]=index;
                    brightest=std::max(brightest,brightness[(size_t)index]);
                }
                at+=length+4;
            }
        }
        glyph.width=w;glyph.height=h;
        if(c>='A'&&c<='Z')letterHeight=std::max(letterHeight,h);
        glyph.left=(short)(data[4]|data[5]<<8);glyph.top=(short)(data[6]|data[7]<<8);
    }
    if(letterHeight)glyphHeight=letterHeight;
    for(size_t i=0;i<glyphs.size();++i) {
        Glyph &glyph=glyphs[i];
        glyph.shade.resize(raw[i].size());
        for(size_t p=0;p<raw[i].size();++p)
            glyph.shade[p]=raw[i][p]<0?0:(unsigned char)std::max(1,brightness[(size_t)raw[i][p]]*255/brightest);
    }
}

const Glyph *glyphFor(char c) {
    int upper=std::toupper((unsigned char)c);
    if(upper<firstGlyph||upper>lastGlyph)return nullptr;
    const Glyph &glyph=glyphs[(size_t)(upper-firstGlyph)];
    return glyph.width?&glyph:nullptr;
}

int textWidth(const std::string &string) {
    int w=0;
    for(char c:string) {const Glyph *glyph=glyphFor(c);w+=glyph?glyph->width+Game_FontSpacing():spaceWidth;}
    return w;
}

std::string clipped(std::string value,int pixels) {
    if(textWidth(value)<=pixels)return value;
    while(!value.empty()&&textWidth(value+"...")>pixels)value.pop_back();
    return value+"...";
}

// Probe the same predicates used by controls, without changing the draft.
std::string dependencyHint(const Option &option) {
    std::string key=option.key?option.key:"";
    if(key.find("fluid_")==0&&!panel.draft.fluidWeapons)return "Enable FluidWeapons in Bundled mods first.";
    for(const auto &tab:panel.tabs)for(const auto &section:tab.sections)for(const auto &parent:section.options) {
        if(!parent.key||!*parent.key||!parent.flag||&parent==&option)continue;
        Settings probe=panel.draft;
        auto offset=(const char*)parent.flag-(const char*)&panel.draft;
        int *flag=(int*)((char*)&probe+offset);
        if(parent.kind==OptionKind::Check) {
            *flag=!*parent.flag;
            if(option.isEnabled(probe))return std::string(*flag?"Enable ":"Disable ")+parent.title+" in "+tab.title+".";
        } else if(parent.kind==OptionKind::Choice) {
            for(size_t i=0;i<parent.values.size();++i) {
                *flag=parent.values[i];
                if(option.isEnabled(probe))return std::string("Set ")+parent.title+" to "+parent.items[i]+" in "+tab.title+".";
            }
        }
    }
    return "Unavailable with the current settings or renderer. Check the requirements below.";
}

unsigned shaded(unsigned color,int shade) {
    unsigned r=(color>>16&255)*shade/255,g=(color>>8&255)*shade/255,b=(color&255)*shade/255;
    return (color&0xff000000)|r<<16|g<<8|b;
}

// Draws from pixel x, vertically centered in the row's cell; returns the end.
int drawTextAt(const std::string &string,int x,int row,unsigned color) {
    int baseline=row*cellHeight+(cellHeight-glyphHeight)/2;
    for(char c:string) {
        const Glyph *glyph=glyphFor(c);
        if(!glyph) {x+=spaceWidth;continue;}
        int left=x-glyph->left,top=baseline-glyph->top;
        for(int y=0;y<glyph->height;++y)
            for(int i=0;i<glyph->width;++i) {
                int shade=glyph->shade[(size_t)(y*glyph->width+i)],px=left+i,py=top+y;
                if(shade&&px>=0&&px<width&&py>=0&&py<height)image[py*width+px]=shaded(color,shade);
            }
        x+=glyph->width+Game_FontSpacing();
    }
    return x;
}

void drawText(const std::string &string,int column,int row,unsigned color) { drawTextAt(string,column*cellWidth,row,color); }

// Ends the text at the left edge of column.
void drawTextRight(const std::string &string,int column,int row,unsigned color) {
    drawTextAt(string,column*cellWidth-textWidth(string),row,color);
}

int rowY(int row) { return row*cellHeight; }

void drawSeparator(int row) { fill(leftColumn*cellWidth,rowY(row)+cellHeight/2,(rightColumn-leftColumn)*cellWidth,1,separator); }

// Splits text into lines at most lineWidth pixels wide; the last line ends
// in "..." when the text does not fit.
std::vector<std::string> wrap(const std::string &string,int lineWidth,size_t maxLines) {
    std::vector<std::string> lines;
    std::string line,word;
    auto flushWord=[&] {
        if(word.empty())return;
        if(line.empty())line=word;
        else if(textWidth(line+" "+word)<=lineWidth)line+=" "+word;
        else {lines.push_back(line);line=word;}
        word.clear();
    };
    for(char c:string) {if(c==' ')flushWord();else word+=c;}
    flushWord();
    if(!line.empty())lines.push_back(line);
    if(lines.size()>maxLines) {
        lines.resize(maxLines);
        std::string &last=lines.back();
        while(!last.empty()&&textWidth(last+"...")>lineWidth)last.pop_back();
        last+="...";
    }
    return lines;
}

std::string valueText(const Option &option) {
    if(option.kind==OptionKind::Action)return "Start";
    if(option.kind==OptionKind::Check)return *option.flag?"On":"Off";
    if(option.kind==OptionKind::Choice) {
        for(size_t i=0;i<option.values.size();++i)if(option.values[i]==*option.flag)return option.items[i];
        return option.items.front();
    }
    size_t step=(size_t)std::lround((*option.number-option.low)/option.step);
    if(step<option.items.size())return option.items[step];
    char value[16];
    snprintf(value,sizeof(value),option.step>=1?"%.0f":"%.2f",*option.number);
    return value;
}

void selectTab(int tab) {
    int count=(int)panel.tabs.size();
    panel.tab=(tab%count+count)%count;
    panel.lines.clear();
    for(OptionSection &section:panel.tabs[(size_t)panel.tab].sections) {
        panel.lines.push_back({&section,nullptr});
        for(Option &option:section.options)panel.lines.push_back({&section,&option});
    }
    panel.selected=1;panel.scroll=0;
}

void keepSelectionVisible() {
    // The first option brings its section heading into view too.
    if(panel.selected==1)panel.scroll=0;
    if(panel.selected<panel.scroll)panel.scroll=panel.selected;
    if(panel.selected>=panel.scroll+visibleLines)panel.scroll=panel.selected-visibleLines+1;
}

void moveSelection(int direction) {
    int count=(int)panel.lines.size();
    do panel.selected=((panel.selected+direction)%count+count)%count;
    while(!panel.lines[(size_t)panel.selected].option);
    keepSelectionVisible();
    S_StartSound(NULL,Game_MenuSound(GAME_MENU_MOVE));
}

Option &selectedOption() { return *panel.lines[(size_t)panel.selected].option; }

void setSlider(Option &option,double value) { *option.number=option.snap(value); }

// The options point into panel.draft; this is the same member of panel.defaults.
template<class T> T *defaultOf(T *member) {
    return (T*)((char*)&panel.defaults+((char*)member-(char*)&panel.draft));
}

bool isDefault(const Option &option) {
    // Map selectors and actions do not point into Settings.
    if(!option.key||!*option.key||option.kind==OptionKind::Action)return true;
    if(option.kind==OptionKind::Slider)return option.snap(*option.number)==option.snap(*defaultOf(option.number));
    return *option.flag==*defaultOf(option.flag);
}

void resetOption() {
    Option &option=selectedOption();
    if(!option.isEnabled(panel.draft)||isDefault(option)) {S_StartSound(NULL,Game_MenuSound(GAME_MENU_DENY));return;}
    if(option.kind==OptionKind::Slider)*option.number=*defaultOf(option.number);
    else *option.flag=*defaultOf(option.flag);
    S_StartSound(NULL,Game_MenuSound(GAME_MENU_CHANGE));
}

void changeOption(int direction) {
    Option &option=selectedOption();
    if(!option.isEnabled(panel.draft)) {S_StartSound(NULL,Game_MenuSound(GAME_MENU_DENY));return;}
    if(option.kind==OptionKind::Action){if(direction>0&&option.action)option.action();return;}
    if(option.kind==OptionKind::Check){
        *option.flag=!*option.flag;
        if(*option.flag){
            if(option.flag==&panel.draft.advancedTactics)panel.aiWarning=1;
            else if(option.flag==&panel.draft.aggressiveAI)panel.aiWarning=2;
        }
    }
    else if(option.kind==OptionKind::Choice) {
        int count=(int)option.values.size(),index=0;
        for(int i=0;i<count;++i)if(option.values[(size_t)i]==*option.flag)index=i;
        *option.flag=option.values[(size_t)(((index+direction)%count+count)%count)];
    } else setSlider(option,*option.number+direction*option.step);
    S_StartSound(NULL,Game_MenuSound(GAME_MENU_CHANGE));
}

void closePanel() {
    settings=panel.draft;
    paused=panel.wasPaused;
    panel.open=false;
    settingsChanged();
    S_StartSound(NULL,Game_MenuSound(GAME_MENU_CLOSE));
}

int sliderLeft() { return (rightColumn-valueColumns-sliderColumns)*cellWidth; }
int sliderWidth() { return (sliderColumns-1)*cellWidth; }

// A small triangle in the column right of the list, pointing where more options are.
void drawArrow(int row,bool up) {
    int x=(rightColumn+1)*cellWidth+cellWidth/2,y=rowY(row)+(cellHeight-4)/2;
    for(int i=0;i<4;++i)fill(x-(up?i:3-i),y+i,2*(up?i:3-i)+1,1,dim);
}

void drawSlider(const Option &option,int row,bool enabled) {
    int x=sliderLeft(),w=sliderWidth(),y=rowY(row)+cellHeight/2;
    int position=(int)std::lround((*option.number-option.low)/(option.high-option.low)*(w-1));
    fill(x,y-1,w,2,track);
    fill(x,y-1,position,2,enabled?trackFill:dim);
    fill(x+position-2,y-5,5,10,enabled?bright:dim);
}

// A small diamond in the margin left of an option changed from its default.
void drawChangedMark(int row,bool enabled) {
    int x=(leftColumn+1)*cellWidth+cellWidth/2,y=rowY(row)+cellHeight/2-2;
    for(int i=0;i<5;++i) {int half=2-std::abs(i-2);fill(x-half,y+i,2*half+1,1,enabled?heading:dim);}
}
}

void I_Render3DSettingsMenu(void) {
    if(panel.open)return;
    panel.aiWarning=0;
    panel.draft=settings;
    if(auto *window=SDL_GetKeyboardFocus())panel.draft.fullscreen=(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN)!=0;
    panel.defaults=Settings{};fitGame(panel.defaults);
    panel.tabs=optionTabs(panel.draft);
    addMapTab();
    selectTab(panel.tab);
    panel.wasPaused=paused;paused=true;
    panel.open=true;
    S_StartSound(NULL,Game_MenuSound(GAME_MENU_OPEN));
}

int I_Render3DSettingsOpen(void) { return panel.open; }

void I_Render3DSettingsKey(int scancode,int shift) {
    if(!panel.open)return;
    if(panel.aiWarning){panel.aiWarning=0;return;}
    switch(scancode) {
    case SDL_SCANCODE_UP: moveSelection(-1); break;
    case SDL_SCANCODE_DOWN: moveSelection(1); break;
    case SDL_SCANCODE_LEFT: changeOption(-1); break;
    case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE:
        changeOption(1); break;
    case SDL_SCANCODE_TAB: selectTab(panel.tab+(shift?-1:1)); S_StartSound(NULL,Game_MenuSound(GAME_MENU_MOVE)); break;
    case SDL_SCANCODE_PAGEUP: selectTab(panel.tab-1); S_StartSound(NULL,Game_MenuSound(GAME_MENU_MOVE)); break;
    case SDL_SCANCODE_PAGEDOWN: selectTab(panel.tab+1); S_StartSound(NULL,Game_MenuSound(GAME_MENU_MOVE)); break;
    case SDL_SCANCODE_BACKSPACE: case SDL_SCANCODE_DELETE: resetOption(); break;
    case SDL_SCANCODE_R: resetOption(); break;
    case SDL_SCANCODE_HOME: panel.selected=1;panel.scroll=0;break;
    case SDL_SCANCODE_END: panel.selected=(int)panel.lines.size()-1;keepSelectionVisible();break;
    case SDL_SCANCODE_F10: paused=panel.wasPaused;panel.open=false;S_StartSound(NULL,Game_MenuSound(GAME_MENU_CLOSE));break;
    case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_F4: closePanel(); break;
    default:
        if(scancode>=SDL_SCANCODE_1&&scancode<SDL_SCANCODE_1+std::min(9,(int)panel.tabs.size())) {
            selectTab(scancode-SDL_SCANCODE_1);S_StartSound(NULL,Game_MenuSound(GAME_MENU_MOVE));
        }
        break;
    }
}

void I_Render3DSettingsClick(int x,int y,int button) {
    if(!panel.open||x<0||y<0||x>=width||y>=height)return;
    if(panel.aiWarning){if(button)panel.aiWarning=0;return;}
    int row=y/cellHeight;
    if(row==helpRow&&button==1) {
        if(x>=width-128){closePanel();return;}
        if(x>=width-232){I_Render3DSettingsKey(SDL_SCANCODE_F10,0);return;}
    }
    if(x<(leftColumn-1)*cellWidth) {
        if(button==1&&row>=2&&row<2+(int)panel.tabs.size()) {
            selectTab(row-2);S_StartSound(NULL,Game_MenuSound(GAME_MENU_MOVE));
        }
        return;
    }
    int index=panel.scroll+row-listRow;
    if(row<listRow||row>=listRow+visibleLines||index>=(int)panel.lines.size()||!panel.lines[(size_t)index].option)return;
    panel.selected=index;
    if(button==0)return; // Hovering highlights the option.
    Option &option=selectedOption();
    if(option.kind==OptionKind::Slider&&button==1&&x>=sliderLeft()-cellWidth) {
        if(!option.isEnabled(panel.draft)) {S_StartSound(NULL,Game_MenuSound(GAME_MENU_DENY));return;}
        double fraction=(double)(x-sliderLeft())/(sliderWidth()-1);
        setSlider(option,option.low+std::clamp(fraction,0.0,1.0)*(option.high-option.low));
        S_StartSound(NULL,Game_MenuSound(GAME_MENU_CHANGE));
    } else changeOption(button==1?1:-1);
}

void I_Render3DSettingsDraw(unsigned *pixels) {
    if(!panel.open)return;
    image=pixels;
    loadFont();
    fill(0,0,width,height,backdrop);

    fill(8,24,(leftColumn-2)*cellWidth,280,0xe0101010);
    drawText("SETTINGS",2,0,heading);
    for(size_t i=0;i<panel.tabs.size();++i) {
        bool current=(int)i==panel.tab;
        if(current)fill(8,rowY(2+(int)i),(leftColumn-2)*cellWidth,cellHeight,tabBar);
        drawText(clipped(panel.tabs[i].title,(leftColumn-4)*cellWidth),2,2+(int)i,current?bright:description);
    }
    drawText(panel.tabs[panel.tab].title,leftColumn,0,heading);
    int item=0,total=0;
    for(size_t i=0;i<panel.lines.size();++i)if(panel.lines[i].option){++total;if((int)i<=panel.selected)++item;}
    drawTextRight(std::to_string(item)+" / "+std::to_string(total),rightColumn,0,description);
    drawSeparator(1);
    for(int i=0;i<visibleLines&&panel.scroll+i<(int)panel.lines.size();++i) {
        const Line &line=panel.lines[(size_t)(panel.scroll+i)];
        int row=listRow+i;
        if(!line.option) {drawText(line.section->title,leftColumn,row,heading);continue;}
        const Option &option=*line.option;
        bool selected=panel.scroll+i==panel.selected,enabled=option.isEnabled(panel.draft);
        if(selected)fill((leftColumn+1)*cellWidth,rowY(row),(rightColumn-leftColumn)*cellWidth,cellHeight,selectionBar);
        unsigned color=!enabled?dim:selected?bright:text;
        int valueLeft=option.kind==OptionKind::Slider?sliderLeft()-12:rightColumn*cellWidth-textWidth(valueText(option))-16;
        drawText(clipped(option.title,valueLeft-(leftColumn+2)*cellWidth),leftColumn+2,row,color);
        if(!isDefault(option))drawChangedMark(row,enabled);
        unsigned valueColor=enabled&&option.kind==OptionKind::Check&&*option.flag?onColor:color;
        drawTextRight(valueText(option),rightColumn,row,valueColor);
        if(option.kind==OptionKind::Slider)drawSlider(option,row,enabled);
    }
    if(panel.scroll>0)drawArrow(listRow,true);
    if(panel.scroll+visibleLines<(int)panel.lines.size())drawArrow(listRow+visibleLines-1,false);
    drawSeparator(descriptionRow-1);

    const Option &option=selectedOption();
    int tipRow=descriptionRow;
    std::string status=option.isEnabled(panel.draft)?std::string(option.title):dependencyHint(option);
    drawText(clipped(status,608),2,tipRow++,option.isEnabled(panel.draft)?heading:0xffffbc72);
    std::string detail=option.tip?option.tip:"";
    if(option.cost)detail+=" "+std::string(option.cost);
    auto lines=wrap(detail,608,3);
    for(size_t i=0;i<lines.size();++i)drawText(lines[i],2,tipRow+(int)i,description);
    drawText("Tab: category  Arrows: adjust  R: reset item",2,helpRow,description);
    fill(width-232,rowY(helpRow),100,cellHeight,0xff303030);
    drawTextAt("F10 Discard",width-228,helpRow,text);
    fill(width-128,rowY(helpRow),120,cellHeight,selectionBar);
    drawTextAt("Esc Apply / close",width-124,helpRow,bright);
    if(panel.aiWarning){
        fill(24,128,width-48,144,0xff180c08);
        drawText(panel.aiWarning==2?"AGGRESSIVE AI ENABLED":"ADVANCED TACTICS ENABLED",6,9,heading);
        drawText("Warning: this can make the game hard,",6,11,bright);
        drawText("and VERY HARD with pistol starts.",6,12,heading);
        drawText("Press any key or click to continue.",6,15,description);
    }
    // The renderer repeats the border texels beyond the image, so they stay plain backdrop.
    fill(0,0,width,1,backdrop);fill(0,height-1,width,1,backdrop);
    fill(0,0,1,height,backdrop);fill(width-1,0,1,height,backdrop);
}

void I_Render3DLoadingDraw(unsigned *pixels,const char *title,const char *stage,float fraction) {
    image=pixels;
    loadFont();
    const unsigned black=0xff000000;
    fill(0,0,width,height,black);
    const int titleRow=11,barY=rowY(titleRow+1)+4,barWidth=320,barX=(width-barWidth)/2,barHeight=6;
    drawTextAt(title,(width-textWidth(title))/2,titleRow,heading);
    fill(barX-2,barY-2,barWidth+4,barHeight+4,track);
    fill(barX-1,barY-1,barWidth+2,barHeight+2,black);
    fill(barX,barY,(int)std::lround(barWidth*std::clamp(fraction,0.0f,1.0f)),barHeight,trackFill);
    drawTextAt(stage,(width-textWidth(stage))/2,titleRow+2,description);
}

void I_Render3DSettings(void) { I_Render3DSettingsMenu(); }

// Runs within the native smoke harness, with a level loaded and the panel closed.
extern "C" int I_TestSettingsControls(void) {
    I_Render3DSettingsMenu();
    Settings saved=panel.draft;
    auto choose=[](const char *key){
        for(size_t t=0;t<panel.tabs.size();++t) {
            selectTab((int)t);
            for(size_t i=0;i<panel.lines.size();++i) {
                auto *o=panel.lines[i].option;
                if(o&&std::string(o->key)==key){panel.selected=(int)i;return true;}
            }
        }
        return false;
    };
    if(!choose("fluid_rocket_fire"))return 0;
    panel.draft.fluidWeapons=0;
    int value=panel.draft.fluidRocketFire;
    changeOption(1);resetOption();
    if(panel.draft.fluidRocketFire!=value||selectedOption().isEnabled(panel.draft))return 0;
    if(dependencyHint(selectedOption()).find("FluidWeapons")==std::string::npos)return 0;
    panel.draft.fluidWeapons=1;
    changeOption(1);
    if(panel.draft.fluidRocketFire==value)return 0;
    if(!choose("fluid_glove_red"))return 0;
    panel.draft.fluidGloves=0;
    if(selectedOption().isEnabled(panel.draft))return 0;
    panel.draft.fluidGloves=7;
    if(!selectedOption().isEnabled(panel.draft))return 0;
    if(!choose("progressive_glass"))return 0;
    panel.draft.windowGlass=0;
    if(selectedOption().isEnabled(panel.draft))return 0;
    panel.draft.windowGlass=1;
    if(!selectedOption().isEnabled(panel.draft))return 0;
    I_Render3DSettingsKey(SDL_SCANCODE_F10,0);
    if(panel.open)return 0;
    I_Render3DSettingsMenu();
    if(panel.draft.fluidRocketFire!=saved.fluidRocketFire||panel.draft.fluidGloves!=saved.fluidGloves)return 0;
    I_Render3DSettingsClick(24,2*cellHeight+4,1);
    if(panel.tab!=0)return 0;
    I_Render3DSettingsKey(SDL_SCANCODE_END,0);
    if(selectedOption().kind!=OptionKind::Action)return 0;
    I_Render3DSettingsKey(SDL_SCANCODE_F10,0);
    return 1;
}
