#ifndef GESTURE_COMMAND_H
#define GESTURE_COMMAND_H

#include "gesture_command_config.h"

enum {
    GC_GESTURE_NONE = 0,
    GC_GESTURE_V = 1,
    GC_GESTURE_OPEN = 2,
    GC_GESTURE_FIST = 3
};

typedef enum {
    GC_ACTION_CALIBRATING = 0,
    GC_ACTION_STOP,
    GC_ACTION_UNLOCK,
    GC_ACTION_HOLD,
    GC_ACTION_FORWARD,
    GC_ACTION_BACK,
    GC_ACTION_LEFT,
    GC_ACTION_RIGHT,
    GC_ACTION_LAND
} GestureAction;

typedef struct {
    int candidate;
    int candidate_frames;
    int stable;
    GestureAction position_candidate;
    int position_frames;
    GestureAction action;
    int history[GC_VOTE_WINDOW];
    int history_index;
} GestureCommandState;

typedef struct {
    int raw;
    int cx;
    int cy;
    int touches_edge;
    int calibrating;
} GestureCommandInput;

typedef struct {
    int stable;
    GestureAction action;
    int changed;
} GestureCommandOutput;

static int gesture_command_required_frames(int gesture)
{
    if (gesture == GC_GESTURE_V) return GC_TRIGGER_FRAMES_V;
    if (gesture == GC_GESTURE_OPEN) return GC_TRIGGER_FRAMES_OPEN;
    if (gesture == GC_GESTURE_FIST) return GC_TRIGGER_FRAMES_FIST;
    return 1;
}

static const char *gesture_action_name(GestureAction action)
{
    switch (action) {
    case GC_ACTION_CALIBRATING: return "CALIBRATING";
    case GC_ACTION_STOP: return "STOP";
    case GC_ACTION_UNLOCK: return GC_ACTION_V_NAME;
    case GC_ACTION_HOLD: return GC_ACTION_OPEN_CENTER_NAME;
    case GC_ACTION_FORWARD: return "FORWARD";
    case GC_ACTION_BACK: return "BACK";
    case GC_ACTION_LEFT: return "LEFT";
    case GC_ACTION_RIGHT: return "RIGHT";
    case GC_ACTION_LAND: return GC_ACTION_FIST_NAME;
    default: return "STOP";
    }
}

static GestureAction gesture_command_open_position(const GestureCommandInput *in)
{
    int dx, dy, ax, ay;
    if (in->cx < 0 || in->cx >= 96 || in->cy < 0 || in->cy >= 72)
        return GC_ACTION_STOP;
    if (GC_STOP_WHEN_MASK_TOUCHES_EDGE && in->touches_edge)
        return GC_ACTION_STOP;
    if (in->cx >= GC_POSITION_CENTER_X_MIN && in->cx <= GC_POSITION_CENTER_X_MAX &&
        in->cy >= GC_POSITION_CENTER_Y_MIN && in->cy <= GC_POSITION_CENTER_Y_MAX)
        return GC_ACTION_HOLD;
    dx = in->cx - 48;
    dy = in->cy - 36;
    ax = dx < 0 ? -dx : dx;
    ay = dy < 0 ? -dy : dy;
    if (ax * 72 >= ay * 96)
        return dx < 0 ? GC_ACTION_LEFT : GC_ACTION_RIGHT;
    if (GC_REVERSE_FORWARD_BACK)
        return dy < 0 ? GC_ACTION_BACK : GC_ACTION_FORWARD;
    return dy < 0 ? GC_ACTION_FORWARD : GC_ACTION_BACK;
}

static void gesture_command_init(GestureCommandState *state)
{
    int i;
    for(i=0;i<GC_VOTE_WINDOW;++i) state->history[i]=GC_GESTURE_NONE;
    state->history_index=0;
    state->candidate = GC_GESTURE_NONE;
    state->candidate_frames = 0;
    state->stable = GC_GESTURE_NONE;
    state->position_candidate = GC_ACTION_STOP;
    state->position_frames = 0;
    state->action = GC_ACTION_CALIBRATING;
}

static GestureCommandOutput gesture_command_update(GestureCommandState *state,
                                                    const GestureCommandInput *in)
{
    GestureCommandOutput out;
    GestureAction next = GC_ACTION_STOP;
    GestureAction previous = state->action;
    int votes=0,i;

    if (in->calibrating) {
        gesture_command_init(state);
        out.stable = GC_GESTURE_NONE;
        out.action = GC_ACTION_CALIBRATING;
        out.changed = previous != out.action;
        return out;
    }

    state->history[state->history_index]=in->raw;
    state->history_index=(state->history_index+1)%GC_VOTE_WINDOW;
    for(i=0;i<GC_VOTE_WINDOW;++i) if(state->history[i]==in->raw) ++votes;
    if (in->raw == GC_GESTURE_NONE) {
        state->candidate = state->stable = GC_GESTURE_NONE;
        state->candidate_frames = 0;
        /* A real disappearance invalidates old votes; it cannot re-arm on return. */
        for(i=0;i<GC_VOTE_WINDOW;++i) state->history[i]=GC_GESTURE_NONE;
    } else {
        if (in->raw != state->candidate) {
            state->candidate = in->raw;
            state->candidate_frames = 1;
        } else if (state->candidate_frames < gesture_command_required_frames(in->raw)) {
            ++state->candidate_frames;
        }
        /* A changing/uncertain gesture always stops until it is confirmed. */
        state->stable = (in->raw==GC_GESTURE_FIST ? state->candidate_frames : votes) >= gesture_command_required_frames(in->raw)
                      ? state->candidate : GC_GESTURE_NONE;
    }

    if (state->stable == GC_GESTURE_V) next = GC_ACTION_UNLOCK;
    else if (state->stable == GC_GESTURE_FIST) next = GC_ACTION_LAND;
    else if (state->stable == GC_GESTURE_OPEN) {
        GestureAction position = gesture_command_open_position(in);
        if (position != state->position_candidate) {
            state->position_candidate = position;
            state->position_frames = 1;
        } else if (state->position_frames < GC_TRIGGER_FRAMES_POSITION) {
            ++state->position_frames;
        }
        next = state->position_frames >= GC_TRIGGER_FRAMES_POSITION
             ? state->position_candidate : GC_ACTION_STOP;
    } else {
        state->position_candidate = GC_ACTION_STOP;
        state->position_frames = 0;
    }

    state->action = next;
    out.stable = state->stable;
    out.action = state->action;
    out.changed = previous != state->action;
    return out;
}

static int gesture_command_selftest(void)
{
    GestureCommandState state;
    GestureCommandInput in = {GC_GESTURE_NONE, 48, 36, 0, 0};
    GestureCommandOutput out;
    int i;
    gesture_command_init(&state);
    for (i = 0; i < GC_TRIGGER_FRAMES_V; ++i) {
        in.raw = GC_GESTURE_V;
        out = gesture_command_update(&state, &in);
    }
    if (out.action != GC_ACTION_UNLOCK) return 0;
    in.raw = GC_GESTURE_NONE;
    if (gesture_command_update(&state, &in).action != GC_ACTION_STOP) return 0;
    for (i = 0; i < GC_TRIGGER_FRAMES_OPEN; ++i) {
        in.raw = GC_GESTURE_OPEN; in.cx = 10; in.cy = 36;
        out = gesture_command_update(&state, &in);
    }
    out = gesture_command_update(&state, &in);
    if (out.action != GC_ACTION_LEFT) return 0;
    in.cx = 48; in.cy = 10;
    gesture_command_update(&state, &in);
    if (gesture_command_update(&state, &in).action != GC_ACTION_FORWARD) return 0;
    in.raw = GC_GESTURE_NONE;
    gesture_command_update(&state, &in);
    for (i = 0; i < GC_TRIGGER_FRAMES_FIST; ++i) {
        in.raw = GC_GESTURE_FIST;
        out = gesture_command_update(&state, &in);
    }
    return out.action == GC_ACTION_LAND;
}

#endif
