#include <assert.h>
#include <string.h>
#include "gesture_command.h"

static GestureCommandOutput feed(GestureCommandState *s, int raw, int x, int y)
{
    GestureCommandInput in = {raw, x, y, 0, 0};
    return gesture_command_update(s, &in);
}

int main(void)
{
    GestureCommandState s;
    GestureCommandOutput out;
    int i;

    gesture_command_init(&s);
    for (i = 0; i < GC_TRIGGER_FRAMES_V - 1; ++i)
        assert(feed(&s, GC_GESTURE_V, 48, 36).action == GC_ACTION_STOP);
    assert(feed(&s, GC_GESTURE_V, 48, 36).action == GC_ACTION_UNLOCK);
    assert(feed(&s, GC_GESTURE_NONE, -1, -1).action == GC_ACTION_STOP);

    for (i = 0; i < GC_TRIGGER_FRAMES_OPEN; ++i)
        out = feed(&s, GC_GESTURE_OPEN, 10, 36);
    assert(out.action == GC_ACTION_STOP);
    out = feed(&s, GC_GESTURE_OPEN, 10, 36);
    assert(out.action == GC_ACTION_LEFT);

    out = feed(&s, GC_GESTURE_OPEN, 48, 10);
    assert(out.action == GC_ACTION_STOP);
    out = feed(&s, GC_GESTURE_OPEN, 48, 10);
    assert(out.action == GC_ACTION_FORWARD);

    assert(feed(&s, GC_GESTURE_NONE, -1, -1).action == GC_ACTION_STOP);
    for (i = 0; i < GC_TRIGGER_FRAMES_FIST; ++i)
        out = feed(&s, GC_GESTURE_FIST, 48, 36);
    assert(out.action == GC_ACTION_LAND);
    assert(strcmp(gesture_action_name(out.action), GC_ACTION_FIST_NAME) == 0);
    gesture_command_init(&s);
    feed(&s,1,48,36); feed(&s,1,48,36); feed(&s,2,48,36);
    feed(&s,1,48,36); feed(&s,1,48,36);
    assert(feed(&s,1,48,36).action==GC_ACTION_UNLOCK);
    /* One false OPEN cannot produce motion; returning V need not wait five new frames. */
    assert(feed(&s,2,48,36).action==GC_ACTION_STOP);
    assert(feed(&s,1,48,36).action==GC_ACTION_UNLOCK);
    assert(feed(&s,0,-1,-1).action==GC_ACTION_STOP);
    assert(feed(&s,1,48,36).action==GC_ACTION_STOP);
    assert(gesture_command_selftest());
    return 0;
}
