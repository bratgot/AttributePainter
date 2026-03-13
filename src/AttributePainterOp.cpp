#include "AttributePainterOp.h"
#include "ViewportBrushKnob.h"
#include "BrushSystem.h"

// Nuke NDK
#include <DDImage/Knobs.h>
#include <DDImage/ViewerContext.h>
#include <DDImage/gl.h>

// USG
#include <usg/geom/Stage.h>
#include <usg/geom/MeshPrim.h>
#include <usg/geom/XformCache.h>
#include <usg/api.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace AP {

// ─────────────────────────────────────────────────────────────────────────────
//  Static data
// ─────────────────────────────────────────────────────────────────────────────

const char* const AttributePainterOp::kFalloffNames[] = {
    "Smooth", "Linear", "Constant", "Gaussian", nullptr
};
const char* const AttributePainterOp::kSaveFormatNames[] = {
    "USD ASCII (.usda)", "USD Binary (.usdc)", "JSON (.json)", nullptr
};
const char* const AttributePainterOp::kBlendNames[] = {
    "Replace", "Add", "Subtract", "Multiply", "Smooth", "Erase", nullptr
};

const DD::Image::GeomOp::Description AttributePainterOp::description(
    "AttributePainter",
    AttributePainterOp::Build
);

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────
AttributePainterOp::AttributePainterOp(Node* node)
    : DD::Image::GeomOp(node, BuildEngine<Engine>())
    , sampler_(std::make_unique<MeshSampler>())
    , writer_ (std::make_unique<USDColorWriter>())
{
}

AttributePainterOp::~AttributePainterOp() = default;

// ─────────────────────────────────────────────────────────────────────────────
//  Help
// ─────────────────────────────────────────────────────────────────────────────

const char* AttributePainterOp::node_help() const {
    return
        "AttributePainter\n\n"
        "Interactive vertex colour painter for USD geometry in Nuke 17.\n"
        "Connect a GeoImport/USD node, set prim path, paint in 3D viewer.\n\n"
        "  • LMB drag to paint\n"
        "  • Ctrl+Scroll to resize brush\n"
        "  • Ctrl+Z / Ctrl+Shift+Z to undo/redo\n";
}

// ─────────────────────────────────────────────────────────────────────────────
//  Knobs
// ─────────────────────────────────────────────────────────────────────────────

void AttributePainterOp::knobs(DD::Image::Knob_Callback f) {
    DD::Image::GeomOp::knobs(f);

    // -- Title -------------------------------------------------------------
    DD::Image::Text_knob(f, "<b><font size=5>Attribute Painter</font></b><br><font color=#aaaaaa>USD Vertex Colour Painter for Nuke 17</font>");
    DD::Image::Divider(f, "");

    // -- USD Target --------------------------------------------------------
    DD::Image::Divider(f, "USD Target");
    static const char* primPathBuf = k_primPath_.c_str();
    DD::Image::String_knob(f, &primPathBuf, "prim_path", "Prim Path");
    DD::Image::Tooltip(f, "USD prim path of the mesh to paint.");
    DD::Image::Button(f, "refresh_mesh", "Refresh");
    DD::Image::SetFlags(f, DD::Image::Knob::STARTLINE);
    static const char* primvarBuf = k_primvarName_.c_str();
    DD::Image::String_knob(f, &primvarBuf, "primvar_name", "Primvar Name");
    DD::Image::Tooltip(f, "Name of the USD primvar to write colours into. Default: displayColor");
    DD::Image::Button(f, "repaint_all", "Repaint");
    DD::Image::Button(f, "clear_paint", "Clear");
    // -- Brush -------------------------------------------------------------
    DD::Image::Divider(f, "Brush");
    DD::Image::Bool_knob (f, &k_paintEnabled_, "paint_enabled", "Enable Paint");
    DD::Image::Tooltip(f, "Enable or disable painting. Disabling lets you navigate without accidentally painting.");
    DD::Image::Bool_knob (f, &k_showBrush_,   "show_brush",    "Show Brush");
    DD::Image::Bool_knob (f, &k_showVertices_, "show_vertices", "Show Painted Vertices");
    DD::Image::Tooltip(f, "Show painted vertex colour dots in the viewport.");
    DD::Image::Tooltip(f, "Show or hide the brush circle overlay in the viewport.");
    DD::Image::Float_knob(f, &k_radius_,       "radius",        "Radius");
    DD::Image::Tooltip(f, "Brush radius in world units.\\nShift+LMB drag horizontally to resize interactively.");
    DD::Image::SetRange(f, 0.001, 10.0);
    DD::Image::Float_knob(f, &k_strength_,     "strength",      "Strength");
    DD::Image::Tooltip(f, "Paint strength (opacity) per tick. 1.0 = full replacement per stroke.");
    DD::Image::SetRange(f, 0.0, 1.0);
    DD::Image::Float_knob(f, &k_hardness_,     "hardness",      "Hardness");
    DD::Image::Tooltip(f, "Inner falloff edge. 1.0 = hard edge, 0.0 = full feather.");
    DD::Image::SetRange(f, 0.0, 1.0);
    DD::Image::Enumeration_knob(f, &k_falloff_, kFalloffNames, "falloff", "Falloff");
    DD::Image::Tooltip(f, "Falloff curve shape within the feathered region.");
    DD::Image::Enumeration_knob(f, &k_blend_,   kBlendNames,   "blend",   "Blend Mode");
    DD::Image::Tooltip(f, "How the brush colour blends with existing vertex colours.");
    DD::Image::Color_knob(f, k_color_, "paint_color", "Color");
    DD::Image::Tooltip(f, "Paint colour. Used as the target colour for Replace, Add, Subtract etc.");

    DD::Image::Divider(f, "");
    DD::Image::Bool_knob(f, &k_flipNormals_, "flip_normals", "Flip Normals");
    DD::Image::Tooltip(f, "Flip mesh normals. Use if the brush only hits the back-face of your mesh.");

    // -- Save / Load -------------------------------------------------------
    DD::Image::Divider(f, "Save / Load");
    DD::Image::Enumeration_knob(f, &k_saveFormat_, kSaveFormatNames, "save_format", "Format");
    DD::Image::Tooltip(f, "File format to save paint data. USD writes a .usda layer. JSON writes a simple array.");
    static std::string defaultPath = std::string(getenv("TEMP") ? getenv("TEMP") : "/tmp") + "/attribute_painter_paint.usdc";
    static const char* savePathBuf = defaultPath.c_str();
    DD::Image::File_knob(f, &savePathBuf, "save_path", "File Path", DD::Image::Write_File_Normal);
    DD::Image::Tooltip(f, "Path to save/load paint data. Use .usda for USD or .json for JSON.");
    DD::Image::Bool_knob(f, &k_autoSave_, "auto_save", "Auto Save");
    DD::Image::Tooltip(f, "Automatically save after each stroke.");
    DD::Image::Button(f, "save_paint", "Save");
    DD::Image::Button(f, "load_paint", "Load");
    // -- Usage Notes -------------------------------------------------------
    DD::Image::Divider(f, "How To Use");
    DD::Image::Text_knob(f, "<font color=#cccccc><b>1.</b> Connect a USD geometry node as input<br><b>2.</b> Click <i>Refresh Mesh</i> to auto-detect the prim path<br><b>3.</b> Open the Properties panel to activate the brush<br><b>4.</b> LMB drag in the 3D viewer to paint<br><b>5.</b> Shift+LMB drag horizontally to resize brush<br><b>6.</b> Hold Alt before clicking to rotate viewport</font>");

    // -- Credit ------------------------------------------------------------
    DD::Image::Divider(f, "");
    DD::Image::Text_knob(f, "<font color=#666666>Created by Marten Blumen&nbsp;&nbsp;|&nbsp;&nbsp;Nuke 17 NDK + USG&nbsp;&nbsp;|&nbsp;&nbsp;v1.0.18</font>");
    CustomKnob1(ViewportBrushKnob, f, this, "brush_handle");
}

int AttributePainterOp::knob_changed(DD::Image::Knob* k) {
    if (k->is("prim_path") || k->is("primvar_name")) {
        geometryDirty_.store(true);
        return 1;
    }
    if (k->is("refresh_mesh")) {
        autoDetectPrimPath();
        geometryDirty_.store(true);
        rebuildGeometry();
        return 1;
    }
    if (k->is("repaint_all")) {
        invalidate();
        return 1;
    }
    if (k->is("browse_path")) {
        // Use TCL to open file browser and set knob
        std::string cmd = std::string("set _f [tk_getSaveFile")
            + " -title {Save Paint Data}"
            + " -filetypes {{\"USD ASCII\" {.usda}} {\"USD Binary\" {.usdc}} {\"JSON\" {.json}}}"
            + " -defaultextension .usda];"
            + " if {$_f ne {}} { [python nuke.toNode(nuke.thisNode().name())\[\"save_path\"\].setValue($_f)] }";
        script_command(cmd.c_str(), true, false);
        const char* result = script_result(false);
        script_unlock();
        return 1;
    }
    if (k->is("save_paint")) {
        saveColors();
        return 1;
    }
    if (k->is("load_paint")) {
        loadColors();
        return 1;
    }
    if (k->is("save_format")) {
        DD::Image::Knob* pk = knob("save_path");
        if (pk) {
            const char* txt = pk->get_text(&outputContext());
            std::string path = (txt && txt[0]) ? std::string(txt) : "";
            // Swap extension
            auto dot = path.rfind('.');
            if (dot != std::string::npos) path = path.substr(0, dot);
            else if (path.empty()) path = std::string(getenv("TEMP") ? getenv("TEMP") : "/tmp") + "/attribute_painter_paint";
            if (k_saveFormat_ == 0) path += ".usda";
            else if (k_saveFormat_ == 1) path += ".usdc";
            else path += ".json";
            pk->set_text(path.c_str());
        }
        return 1;
    }
    if (k->is("show_vertices")) {
        if (brushKnob_) brushKnob_->setShowVertices(k_showVertices_);
        return 1;
    }
    if (k->is("clear_paint")) {
        if (sampler_ && sampler_->isValid()) {
            std::vector<AP::Color3f> zeros(sampler_->colors().size(), {0,0,0});
            sampler_->initColors(zeros);
            writer_->clearStaged();
            strokeBefore_.clear();
            undoStack_.clear();
        }
        pushColorsToHydra();
        ++paintVersion_;
        invalidate();
        asapUpdate();
        return 1;
    }
    syncBrushStateToKnobs();
    return DD::Image::GeomOp::knob_changed(k);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Brush state sync
// ─────────────────────────────────────────────────────────────────────────────

void AttributePainterOp::syncBrushStateToKnobs() {
    if (!brushKnob_) return;
    BrushState bs;
    bs.radius   = k_radius_;
    bs.strength = k_strength_;
    bs.hardness = k_hardness_;
    bs.color    = { k_color_[0], k_color_[1], k_color_[2] };
    bs.falloff  = static_cast<FalloffMode>(k_falloff_);
    bs.blend    = static_cast<BlendMode>(k_blend_);
    bs.active   = k_paintEnabled_;
    brushKnob_->setBrushState(bs);
    brushKnob_->setEnabled(k_paintEnabled_ && k_showBrush_);
}

// ─────────────────────────────────────────────────────────────────────────────
//  rebuildFromStage — fully usg-native, no pxr
// ─────────────────────────────────────────────────────────────────────────────

void AttributePainterOp::autoDetectPrimPath() {
    DD::Image::Op* gIn = input(0);
    if (!gIn) return;
    DD::Image::Knob* inPrimPath = gIn->knob("prim_path");
    if (!inPrimPath) return;
    std::string path = inPrimPath->get_text(nullptr);
    std::string nodeName = gIn->node_name();
    size_t pos = path.find("{nodename}");
    if (pos != std::string::npos) path.replace(pos, 10, nodeName);
    if (path.empty()) return;
    if (path[0] != (char)0x2F) path = "/" + path;
    DD::Image::Knob* pk = knob("prim_path");
    if (pk) {
        pk->set_text(path.c_str());
        k_primPath_ = pk->get_text(nullptr);
    }
}
void AttributePainterOp::rebuildGeometry() {
    DD::Image::GeomOp* gIn = input0();
    if (gIn) {
        usg::StageRef stageRef;
        usg::ArgSet   args;
        gIn->buildGeometryStage(stageRef, args);
        usgStage_ = stageRef;
    }
    rebuildFromStage();
    syncBrushStateToKnobs();
}

bool AttributePainterOp::rebuildFromStage() {
    if (!usgStage_) return false;

    usg::MeshPrim mesh = usg::MeshPrim::getInStage(usgStage_, usg::Path(k_primPath_));
    if (!mesh.isValid()) return false;
    usg::Vec3fArray pts = mesh.getPoints();
    if (pts.empty()) return false;

    // World transform
    usg::XformCache xc(fdk::defaultTimeValue());
    fdk::Mat4d xform = xc.getLocalToWorldTransform(static_cast<usg::Prim>(mesh));

    std::vector<Vec3f> worldPts;
    worldPts.reserve(pts.size());
    for (auto& p : pts) {
        fdk::Vec3d pw = xform.transform(fdk::Vec3d(p[0], p[1], p[2]));
        worldPts.push_back({ (float)pw.x, (float)pw.y, (float)pw.z });
    }

    // Topology
    usg::IntArray counts  = mesh.getFaceVertexCounts();
    usg::IntArray indices = mesh.getFaceVertexIndices();

    sampler_->rebuild(worldPts,
                      std::vector<int>(counts.begin(),  counts.end()),
                      std::vector<int>(indices.begin(), indices.end()));
    if (brushKnob_) brushKnob_->setMeshSampler(sampler_.get());

    // On first build (no original colors stored yet), read from USD.
    // On subsequent rebuilds (transform change), preserve painted colors —
    // sampler_->rebuild already keeps colors if vertex count is unchanged.
    if (!hadOriginalColors_) {
        writer_ = std::make_unique<USDColorWriter>(k_primvarName_);
        auto existing = writer_->read(mesh, worldPts.size());
        if (!existing.empty()) {
            sampler_->initColors(existing);
            originalColors_ = existing;
            hadOriginalColors_ = true;
        } else {
            originalColors_.assign(worldPts.size(), Color3f{0.f, 0.f, 0.f});
            hadOriginalColors_ = true;
        }
    }

    geometryDirty_.store(false);
    ++paintVersion_;
    pushColorsToHydra();  // Direct write if context stage already cached
    invalidate();         // Also trigger engine re-eval so processScenegraph runs
    asapUpdate();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  build_handles
// ─────────────────────────────────────────────────────────────────────────────

void AttributePainterOp::build_handles(DD::Image::ViewerContext* ctx) {
    // Detect disable/enable transitions — walk full input chain
    bool isDisabled = node_disabled();
    if (!isDisabled) {
        DD::Image::Op* op = input(0);
        while (op) {
            if (op->node_disabled()) { isDisabled = true; break; }
            op = op->input(0);
        }
        if (!input(0)) isDisabled = true;  // No input connected
    }
    if (isDisabled != wasDisabled_) {
        wasDisabled_ = isDisabled;
        if (isDisabled) {
            restoreOriginalColors();
        } else {
            pushColorsToHydra();
        }
    }

    build_input_handles(ctx);
    build_knob_handles(ctx);
    DD::Image::GeomOp::build_handles(ctx);

    // Lazy-init brush knob
    if (!brushKnob_) {
        DD::Image::Knob* k = knob("brush_handle");
        brushKnob_ = dynamic_cast<ViewportBrushKnob*>(k);
        if (brushKnob_) {
            brushKnob_->setMeshSampler(sampler_.get());
            brushKnob_->setPaintCallback(
                [this](const Vec3f& pos, const Vec3f& normal, bool first) {
                    onPaintTick(pos, normal, first);
                });
            brushKnob_->setStrokeEndCallback([this]() { onStrokeEnd(); });
            brushKnob_->setRadiusCallback([this](float r) {
                k_radius_ = r;
                DD::Image::Knob* rk = knob("radius");
                if (rk) rk->set_value(r);
            });
            brushKnob_->setRebuildCallback([this]() {
                DD::Image::GeomOp* gIn = input0();
                if (gIn) {
                    usg::StageRef stageRef;
                    usg::ArgSet   args;
                    gIn->buildGeometryStage(stageRef, args);
                    usgStage_ = stageRef;
                }
                rebuildFromStage();
                syncBrushStateToKnobs();
            });
        }
    }

    // Rebuild mesh when dirty OR when input has changed (transform, topology, etc.)
    DD::Image::GeomOp* gIn = input0();
    DD::Image::Hash inputHash;
    if (gIn) {
        inputHash = gIn->hash();
    }
    bool inputChanged = (inputHash != lastInputHash_);
    lastInputHash_ = inputHash;

    if (geometryDirty_.load() || inputChanged) {
        if (gIn) {
            usg::StageRef stageRef;
            usg::ArgSet   args;
            gIn->buildGeometryStage(stageRef, args);
            usgStage_ = stageRef;
        }
        rebuildFromStage();
    }

    syncBrushStateToKnobs();
}

void AttributePainterOp::draw_handle(DD::Image::ViewerContext* ctx) {
    if (brushKnob_) brushKnob_->draw_handle(ctx);
}

// ─────────────────────────────────────────────────────────────────────────────
//  onPaintTick
// ─────────────────────────────────────────────────────────────────────────────

void AttributePainterOp::onPaintTick(const Vec3f& pos,
                                      const Vec3f& normal,
                                      bool firstTick) {
    if (!sampler_ || !sampler_->isValid()) return;

    BrushState bs = brushKnob_->brushState();
    bs.center = pos;
    bs.normal = normal;

    std::vector<std::pair<uint32_t,float>> nearby;
    sampler_->verticesInRadius(pos, bs.radius, nearby);
    if (nearby.empty()) return;

    if (firstTick) {
        strokeBefore_.clear();
        for (auto& [idx, dsq] : nearby)
            strokeBefore_.push_back({ idx, sampler_->getColor(idx) });
    }

    for (auto& [idx, dsq] : nearby) {
        float d = std::sqrt(dsq);
        float w = BrushSystem::weight(bs, d);
        if (w <= 0.f) continue;
        Color3f src = sampler_->getColor(idx);
        Color3f dst = BrushSystem::blend(bs, src, w);
        dst = BrushSystem::saturate(dst);
        sampler_->setColor(idx, dst);
    }

    writer_->clearStaged();
    pushColorsToHydra();  // Write directly to Hydra's context stage only
    ++paintVersion_;
}

// ─────────────────────────────────────────────────────────────────────────────
//  onStrokeEnd
// ─────────────────────────────────────────────────────────────────────────────

void AttributePainterOp::onStrokeEnd() {
    if (!sampler_) return;
    std::vector<VertexColor> after;
    after.reserve(strokeBefore_.size());
    for (auto& bv : strokeBefore_)
        after.push_back({ bv.index, sampler_->getColor(bv.index) });
    undoStack_.beginStroke(strokeBefore_);
    undoStack_.endStroke(after);
    strokeBefore_.clear();
    pushColorsToHydra();
    ++paintVersion_;
    invalidate();
    asapUpdate();
}

// ─────────────────────────────────────────────────────────────────────────────
//  commitToUSD
// ─────────────────────────────────────────────────────────────────────────────

void AttributePainterOp::commitToUSD() {
    if (!usgStage_) return;
    usg::MeshPrim mesh = usg::MeshPrim::getInStage(usgStage_, usg::Path(k_primPath_));
    if (!mesh.isValid()) return;
    auto& colors = const_cast<std::vector<Color3f>&>(sampler_->colors());
    writer_->commit(mesh, colors);
    writer_->clearStaged();
}

// ─────────────────────────────────────────────────────────────────────────────
//  pushColorsToHydra — write directly to the cached context stage
//  Bypasses the engine re-evaluation pipeline for live interactive painting.
// ─────────────────────────────────────────────────────────────────────────────

void AttributePainterOp::pushColorsToHydra() {
    if (node_disabled()) return;
    if (engine_ && engine_->cachedContextStage_) {
        engine_->writeColorsToContextStage();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  restoreOriginalColors — write pre-paint colors back to context stage
//  Called when the node is disabled so painted colours disappear.
// ─────────────────────────────────────────────────────────────────────────────

void AttributePainterOp::restoreOriginalColors() {
    if (!engine_ || !engine_->cachedContextStage_) return;
    if (hadOriginalColors_) {
        engine_->writeColors(originalColors_);
    } else {
        // No original displayColor existed — write empty to clear it
        engine_->writeColors({});
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  applyVertexColors (undo/redo)
// ─────────────────────────────────────────────────────────────────────────────

void AttributePainterOp::applyVertexColors(const std::vector<VertexColor>& vcs) {
    if (!sampler_) return;
    for (auto& vc : vcs)
        sampler_->setColor(vc.index, vc.color);
    pushColorsToHydra();
    ++paintVersion_;
    invalidate();
    asapUpdate();
}

// ─────────────────────────────────────────────────────────────────────────
//  Save / Load
// ─────────────────────────────────────────────────────────────────────────
void AttributePainterOp::saveColors() {
    DD::Image::Knob* pk = knob("save_path");
    if (!pk) return;
    std::string path = pk->get_text(nullptr);
    if (path.empty()) { fprintf(stderr, "AttributePainter: no save path set\n"); return; }
    if (k_saveFormat_ == 0 || k_saveFormat_ == 2) saveUSD(path);
    if (k_saveFormat_ == 1 || k_saveFormat_ == 2) saveJSON(path);
    fprintf(stderr, "AttributePainter: saved to %s\n", path.c_str());
}

void AttributePainterOp::loadColors() {
    DD::Image::Knob* pk = knob("save_path");
    if (!pk) return;
    std::string path = pk->get_text(nullptr);
    if (path.empty()) { fprintf(stderr, "AttributePainter: no load path set\n"); return; }
    if (path.size() > 5 && path.substr(path.size()-5) == ".usda") loadUSD(path);
    else loadJSON(path);
    pushColorsToHydra();
    ++paintVersion_;
    invalidate();
}

void AttributePainterOp::saveUSD(const std::string& basePath) {
    if (!sampler_ || !sampler_->isValid()) return;
    std::string path = basePath;
    if (path.size() < 5 || path.substr(path.size()-5) != ".usda")
        path += ".usda";
    usg::LayerRef layer = usg::Layer::Create(path);
    if (!layer) { fprintf(stderr, "AttributePainter: failed to create USD layer\n"); return; }
    usg::StageRef stage = usg::Stage::Create(layer);
    if (!stage) { fprintf(stderr, "AttributePainter: failed to create USD stage\n"); return; }
    usg::MeshPrim mesh = usg::MeshPrim::defineInLayer(layer, usg::Path(k_primPath_));
    if (!mesh.isValid()) { fprintf(stderr, "AttributePainter: failed to define mesh prim\n"); return; }
    const auto& colors = sampler_->colors();
    usg::Vec3fArray vtColors(colors.size());
    for (size_t i = 0; i < colors.size(); ++i)
        vtColors[i] = fdk::Vec3f(colors[i].r, colors[i].g, colors[i].b);
    usg::PrimvarsAPI pvAPI(static_cast<usg::Prim>(mesh));
    usg::Primvar pv = pvAPI.createPrimvar(
        usg::Token("displayColor"),
        usg::Value::Type::Color3fArray,
        usg::GeomTokens.vertex);
    if (pv.isValid()) pv.attribute().setValue(vtColors);
    layer->exportToFile(path);
    fprintf(stderr, "AttributePainter: saved USD to %s\n", path.c_str());
}

void AttributePainterOp::saveJSON(const std::string& basePath) {
    if (!sampler_ || !sampler_->isValid()) return;
    std::string path = basePath;
    if (path.size() < 5 || path.substr(path.size()-5) != ".json")
        path += ".json";
    std::ofstream f(path);
    if (!f) { fprintf(stderr, "AttributePainter: cannot write %s\n", path.c_str()); return; }
    const auto& colors = sampler_->colors();
    f << "{\n  \"primPath\": \"" << k_primPath_ << "\",\n";
    f << "  \"primvarName\": \"" << k_primvarName_ << "\",\n";
    f << "  \"interpolation\": \"vertex\",\n";
    f << "  \"colors\": [\n";
    for (size_t i = 0; i < colors.size(); ++i) {
        f << "    [" << colors[i].r << "," << colors[i].g << "," << colors[i].b << "]";
        if (i+1 < colors.size()) f << ",";
        f << "\n";
    }
    f << "  ]\n}\n";
}

void AttributePainterOp::loadJSON(const std::string& path) {
    if (!sampler_ || !sampler_->isValid()) return;
    std::ifstream f(path);
    if (!f) { fprintf(stderr, "AttributePainter: cannot read %s\n", path.c_str()); return; }
    // Simple JSON parse - read all colors
    std::vector<Color3f> loaded;
    std::string line;
    while (std::getline(f, line)) {
        // Look for lines like: [r,g,b]
        auto lb = line.find('['); auto rb = line.find(']');
        if (lb == std::string::npos || rb == std::string::npos) continue;
        std::string inner = line.substr(lb+1, rb-lb-1);
        float r=0,g=0,b=0;
        if (sscanf(inner.c_str(), "%f,%f,%f", &r, &g, &b) == 3)
            loaded.push_back({r,g,b});
    }
    if (loaded.size() == sampler_->colors().size())
        sampler_->initColors(loaded);
    else
        fprintf(stderr, "AttributePainter: color count mismatch %zu vs %zu\n",
            loaded.size(), sampler_->colors().size());
}

void AttributePainterOp::loadUSD(const std::string& path) {
    if (!sampler_ || !sampler_->isValid()) return;
    usg::StageRef stage = usg::Stage::Open(path);
    if (!stage) { fprintf(stderr, "AttributePainter: cannot open %s\n", path.c_str()); return; }
    usg::MeshPrim mesh = usg::MeshPrim::getInStage(stage, usg::Path(k_primPath_));
    if (!mesh.isValid()) { fprintf(stderr, "AttributePainter: prim not found in %s\n", path.c_str()); return; }
    usg::PrimvarsAPI pvAPI(static_cast<usg::Prim>(mesh));
    usg::Primvar pv(static_cast<usg::Prim>(mesh), usg::Token("displayColor"));
    if (!pv.isValid()) return;
    usg::Vec3fArray vals;
    pv.attribute().getValue(vals);
    if (vals.size() == sampler_->colors().size()) {
        std::vector<Color3f> loaded(vals.size());
        for (size_t i = 0; i < vals.size(); ++i)
            loaded[i] = {vals[i].x, vals[i].y, vals[i].z};
        sampler_->initColors(loaded);
    }
}

} // namespace AP
