#import <Cocoa/Cocoa.h>
#include <string.h>
#include "wad_probe.h"

static void explain(NSString *message)
{
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @"Choose a different WAD folder";
    alert.informativeText = message;
    [alert addButtonWithTitle:@"Choose Folder"];
    [alert runModal];
}

static NSString *choose(NSArray<NSString *> *paths, NSString *title, BOOL optional)
{
    if (paths.count == 1 && !optional) return paths.firstObject;
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = title;
    alert.informativeText = @"The folder contains several WADs. Choose the one you want to play.";
    NSPopUpButton *menu = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 360, 28)];
    if (optional) [menu addItemWithTitle:@"Base game only"];
    for (NSString *path in paths) [menu addItemWithTitle:path.lastPathComponent];
    alert.accessoryView = menu;
    [alert addButtonWithTitle:@"Continue"];
    [alert addButtonWithTitle:@"Cancel"];
    if ([alert runModal] != NSAlertFirstButtonReturn) return nil;
    NSInteger index = menu.indexOfSelectedItem - (optional ? 1 : 0);
    return index < 0 ? @"" : paths[(NSUInteger)index];
}

extern "C" int DOOM_ChooseWads(const char *bundled, char **base, char **addon)
{
    @autoreleasepool {
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp activateIgnoringOtherApps:YES];
        NSAlert *welcome = [[NSAlert alloc] init];
        welcome.messageText = @"What would you like to play?";
        welcome.informativeText = @"Choose a folder containing your WAD files, or play the included Freedoom game.";
        [welcome addButtonWithTitle:@"Choose WAD Folder…"];
        BOOL hasBundled = [[NSFileManager defaultManager] fileExistsAtPath:@(bundled)];
        if (hasBundled) [welcome addButtonWithTitle:@"Play Freedoom"];
        [welcome addButtonWithTitle:@"Quit"];
        NSModalResponse response = [welcome runModal];
        if (hasBundled && response == NSAlertSecondButtonReturn) {
            *base = strdup(bundled);
            return *base != NULL;
        }
        if (response != NSAlertFirstButtonReturn) return 0;
        for (;;) {
            NSOpenPanel *panel = [NSOpenPanel openPanel];
            panel.title = @"Choose your WAD folder";
            panel.message = @"Put the base game and custom map WAD in this folder. Files in subfolders are not included.";
            panel.prompt = @"Use This Folder";
            panel.canChooseFiles = NO;
            panel.canChooseDirectories = YES;
            panel.allowsMultipleSelection = NO;
            NSString *last = [[NSUserDefaults standardUserDefaults] stringForKey:@"WADFolder"];
            if (last) panel.directoryURL = [NSURL fileURLWithPath:last isDirectory:YES];
            if ([panel runModal] != NSModalResponseOK) return 0;
            NSString *folder = panel.URL.path;
            [[NSUserDefaults standardUserDefaults] setObject:folder forKey:@"WADFolder"];
            NSError *error = nil;
            NSArray *names = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:folder error:&error];
            if (!names) { explain(error.localizedDescription); continue; }
            NSMutableArray<NSString *> *bases = [NSMutableArray array];
            NSMutableArray<NSString *> *addons = [NSMutableArray array];
            BOOL invalid = NO, otherGame = NO;
            for (NSString *name in [names sortedArrayUsingSelector:@selector(localizedStandardCompare:)]) {
                if ([name.pathExtension caseInsensitiveCompare:@"wad"] != NSOrderedSame) continue;
                NSString *path = [folder stringByAppendingPathComponent:name];
                doom_wad_info info;
                if (!DOOM_ProbeWad(path.fileSystemRepresentation, &info)) { invalid = YES; continue; }
                if (info.is_iwad && (!info.has_palette || info.game == DOOM_WAD_HEXEN)) otherGame = YES;
                else if (info.is_iwad && (info.episode_maps || info.numbered_maps)) [bases addObject:path];
                else if (!info.is_iwad && info.game != DOOM_WAD_HEXEN) [addons addObject:path];
            }
            if (!bases.count && !addons.count) {
                explain(otherGame ? @"This folder holds a game WAD that cannot be played yet, such as Hexen or DOOM 64. DOOM, DOOM II, Final DOOM, Heretic and Freedoom game WADs can be played."
                        : invalid ? @"The WAD files in this folder are damaged or unsupported. Choose a folder with valid DOOM WAD files."
                        : @"No WAD files were found. Choose the folder that directly contains your .wad files.");
                continue;
            }
            NSString *game = bases.count ? choose(bases, @"Choose the base game", NO) : (hasBundled ? @(bundled) : nil);
            if (!game) {
                if (bases.count) return 0;
                explain(@"Custom maps need a base game. Put your DOOM or DOOM II game WAD in the folder too.");
                continue;
            }
            NSString *mod = addons.count ? choose(addons, @"Choose a custom WAD", addons.count > 1) : @"";
            if (!mod) return 0;
            doom_wad_info gameInfo, modInfo;
            DOOM_ProbeWad(game.fileSystemRepresentation, &gameInfo);
            if (mod.length) {
                DOOM_ProbeWad(mod.fileSystemRepresentation, &modInfo);
                if ((modInfo.numbered_maps && !gameInfo.numbered_maps) ||
                    (modInfo.episode_maps && !gameInfo.episode_maps)) {
                    explain(modInfo.numbered_maps ? @"This map uses DOOM II levels. Put your DOOM II WAD (or Freedoom Phase 2) in the folder and select it as the base game."
                                                 : @"This map uses DOOM episodes. Put your DOOM WAD (or Freedoom Phase 1) in the folder and select it as the base game.");
                    continue;
                }
                if (!gameInfo.numbered_maps && !gameInfo.full_game) {
                    explain(@"The shareware base game cannot load custom WADs. Choose the full game or Freedoom instead.");
                    continue;
                }
            }
            *base = strdup(game.fileSystemRepresentation);
            if (mod.length) *addon = strdup(mod.fileSystemRepresentation);
            return *base && (!mod.length || *addon);
        }
    }
}
