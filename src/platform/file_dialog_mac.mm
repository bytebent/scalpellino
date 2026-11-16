// macOS native Open / Save / folder panels (AppKit).
#import <AppKit/AppKit.h>
#include "platform/file_dialog.hpp"

// allowedFileTypes is deprecated since macOS 12 but still works and keeps us compatible
// with macOS 11 (the replacement needs UniformTypeIdentifiers types for ".map").
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace scal {

namespace {
NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()]; }

void setup(NSSavePanel* p, const std::string& startDir, const std::vector<std::string>& exts) {
    if (!startDir.empty()) p.directoryURL = [NSURL fileURLWithPath:ns(startDir) isDirectory:YES];
    if (!exts.empty()) {
        NSMutableArray* a = [NSMutableArray array];
        for (const std::string& e : exts) [a addObject:ns(e)];
        p.allowedFileTypes = a;
    }
}

// Runs the panel modally and gives keyboard focus back to the editor window after.
bool run(NSSavePanel* p, std::string& out) {
    NSWindow* editor = [NSApp keyWindow];
    const NSModalResponse r = [p runModal];
    if (editor) [editor makeKeyAndOrderFront:nil];
    if (r != NSModalResponseOK || !p.URL) return false;
    out = p.URL.path.UTF8String;
    return true;
}
} // namespace

bool nativeDialogsAvailable() { return true; }

bool openFileDialog(const std::string& startDir, const std::vector<std::string>& exts, std::string& out) {
    @autoreleasepool {
        NSOpenPanel* p = [NSOpenPanel openPanel];
        p.canChooseFiles = YES;
        p.canChooseDirectories = NO;
        p.allowsMultipleSelection = NO;
        setup(p, startDir, exts);
        return run(p, out);
    }
}

bool saveFileDialog(const std::string& startDir, const std::string& defaultName,
                    const std::vector<std::string>& exts, std::string& out) {
    @autoreleasepool {
        NSSavePanel* p = [NSSavePanel savePanel];
        p.canCreateDirectories = YES;
        if (!defaultName.empty()) p.nameFieldStringValue = ns(defaultName);
        setup(p, startDir, exts);
        return run(p, out);
    }
}

bool chooseFolderDialog(const std::string& startDir, std::string& out) {
    @autoreleasepool {
        NSOpenPanel* p = [NSOpenPanel openPanel];
        p.canChooseFiles = NO;
        p.canChooseDirectories = YES;
        p.allowsMultipleSelection = NO;
        p.prompt = @"Add Folder";
        setup(p, startDir, {});
        return run(p, out);
    }
}

} // namespace scal
