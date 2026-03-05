//
//  AttributePainter — Nuke 17 USD vertex colour painter
//  Plugin entry point.
//
//  After building, copy AttributePainter.so to your NUKE_PATH plugins folder.
//  Or add the folder to your init.py:
//    nuke.pluginAddPath('/path/to/AttributePainter')
//

#include <DDImage/Op.h>
#include "AttributePainterOp.h"

// ── Nuke plugin registration ──────────────────────────────────────────────────

static const char* const MENU_PATH = "3D/AttributePainter";

extern "C" void NDKDescribe(unsigned int /*nukeMajor*/,
                             unsigned int /*nukeMinor*/) {
    // Op::Description already registered via static initializer in
    // AttributePainterOp.cpp — nothing more needed here.
    // Additional registration (e.g. menu items) would go here.
}
