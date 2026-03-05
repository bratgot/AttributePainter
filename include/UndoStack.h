#pragma once
#include "Types.h"
#include <deque>
#include <functional>

namespace AP {

// ─────────────────────────────────────────────────────────────────────────────
//  UndoStack
//  Records complete before/after snapshots for each brush stroke.
//  Integrates with Nuke's knob_changed undo mechanism via callbacks.
// ─────────────────────────────────────────────────────────────────────────────
class UndoStack {
public:
    static constexpr size_t MAX_DEPTH = 100;

    using ApplyFn = std::function<void(const std::vector<VertexColor>&)>;

    UndoStack() = default;

    // Call at stroke begin — snapshot the vertices that will change
    void beginStroke(const std::vector<VertexColor>& before) {
        pending_.before = before;
        pending_.after.clear();
    }

    // Call at stroke end — record the final state
    void endStroke(const std::vector<VertexColor>& after) {
        if (pending_.before.empty() && after.empty()) return;
        pending_.after = after;
        redoStack_.clear(); // new action invalidates redo
        undoStack_.push_back(std::move(pending_));
        if (undoStack_.size() > MAX_DEPTH)
            undoStack_.pop_front();
        pending_ = {};
    }

    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }

    // Apply undo — calls fn with the before-state vertex colours
    void undo(const ApplyFn& fn) {
        if (undoStack_.empty()) return;
        StrokeSnapshot snap = std::move(undoStack_.back());
        undoStack_.pop_back();
        fn(snap.before);
        redoStack_.push_back(std::move(snap));
    }

    // Apply redo — calls fn with the after-state vertex colours
    void redo(const ApplyFn& fn) {
        if (redoStack_.empty()) return;
        StrokeSnapshot snap = std::move(redoStack_.back());
        redoStack_.pop_back();
        fn(snap.after);
        undoStack_.push_back(std::move(snap));
    }

    void clear() {
        undoStack_.clear();
        redoStack_.clear();
        pending_ = {};
    }

private:
    std::deque<StrokeSnapshot> undoStack_;
    std::deque<StrokeSnapshot> redoStack_;
    StrokeSnapshot pending_;
};

} // namespace AP
