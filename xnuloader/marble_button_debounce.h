#ifndef MARBLE_BUTTON_DEBOUNCE_H
#define MARBLE_BUTTON_DEBOUNCE_H
typedef struct { unsigned live,last,down; } MarbleButton;
static unsigned marble_button_update(MarbleButton *b,unsigned held)
{
    held=!!held;
    if(!b->live) { if(!held)b->live=1;b->last=held;return 0; }
    unsigned event=held==b->last && held!=b->down?(held?4:5):0;
    if(event)b->down=held;
    b->last=held;return event;
}
#endif
