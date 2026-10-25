#include "ui.h"

#include "screens.h"

static lv_obj_t *screen_for(enum ScreensEnum screen_id)
{
    switch (screen_id)
    {
    case SCREEN_ID_MAIN:
        return objects.main;
    case SCREEN_ID_COOKBOOK_DETAIL:
        return objects.cookbook_detail;
    case SCREEN_ID_RECIPE_DETAIL:
        return objects.recipe_detail;
    default:
        return objects.main;
    }
}

void loadScreen(enum ScreensEnum screenId)
{
    lv_obj_t *screen = screen_for(screenId);
    if (!screen)
        return;
    lv_screen_load_anim(screen, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
}

void ui_init(void)
{
    create_screens();
    lv_screen_load(objects.main);
}

void ui_tick(void)
{
}
