#include <lvgl.h>

#include "users.h"

#define MAX_USERS 24
#define MAX_USERNAME_LENGTH 36 

const char* icon_path = "./icon.bin";

static const lv_font_t *font;
static const char currently_talking[MAX_USERNAME_LENGTH];
static const u32 user_id_list[MAX_USERS];

static const char username_list[MAX_USERNAME_LENGTH][MAX_USERS];

// top level container
static const lv_obj_t *container;

static const lv_obj_t *list_view;
static const lv_style_t *style_username;

static const lv_obj_t *listen_view;
static const lv_obj_t *currently_talking_label;

// this can be void if it's just init'ing the top level container for now.

void create_main_container( lv_obj_t *parent ) {
  container = lv_obj_create(parent);
  lv_obj_clear_flag(container, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_remove_style_all(container);
  lv_obj_set_size(container, lv_pct(100), lv_pct(100));
  lv_obj_set_scroll_snap_y(container, LV_SCROLL_SNAP_CENTER);
 
  // initialize this to nothing  
  currently_talking = "\0";

}

lv_obj_t *create_user_list_view( lv_obj_t *parent, session *session ) {

  u32 user_count = get_user_count( user_id_list_view );
  if ( user_count > MAX_USERS ) {
    printf("connected users exceeded MAX_USERS");
  }
  for ( u32 i=0; i < user_count; i++ ) {
    username_list_view[i] = get_username(user_id_list_view[i]);
  }

  // single 100% column for now 
  static const lv_coord_t grid_columns[] = { LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
  // 14px tall rows * user_count
  static const lv_coord_t grid_rows[] = { user_count, 14, LV_GRID_TEMPLATE_LAST };

  list_view = lv_obj_create(parent);
  lv_obj_remove_style_all(list_view);
  lv_style_set_grid_column_dsc_array(&list_view, grid_columns);
  lv_style_set_grid_row_dsc_array(&list_view, grid_rows);
  lv_style_set_grid_align(&list_view, LV_GRID_ALIGN_CENTER);
  lv_style_set_layout(&list_view, LV_LAYOUT_GRID);
  lv_obj_set_size(list_view, HORIZ_RES, VERT_RES);
  lv_obj_set_flex_flow(list_view, LV_FLEX_FLOW_COLUMN);
}

static lv_obj_t *create_user_list_button lv_obj_t *parent, char *username ) {

  lv_obj_t *btn = lv_obj_create(parent);
  lv_obj_remove_style_all(btn);
  lv_obj_set_size(btn, lv_pct(100), 20);
  lv_obj_t *text = lv_label_create(btn);
  lv_label_set_text(text, username);
  lv_obj_add_style(text, &style_username, 0);

  return btn;
}

static void item_click_cb( lv_event_t *e ) {
  lv_obj_t *el = lv_event_get_target(e);
  u32 index = lv_obj_get_child_id(el);

  printf("clicked id: %u", index);
}

///////////////////////////////////

lv_obj_t *create_listening_view( lv_obj_t *parent ) {
  lv_obj_t *listen_container = lv_obj_create(parent);
  lv_obj_remove_style_all(listen_container);
  lv_obj_set_height(listen_container, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(listen_container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(listen_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *icon;
  LV_IMG_DECLARE(icon);
  icon = lv_obj_create(container);
  lv_img_set_src(icon, icon_path);

  currently_talking_label = lv_obj_create(listen_container);
  lv_obj_remove_style_all(currently_talking_label);
  lv_label_set_text(label, currently_talking_label);

  return listen_container;
}

static void update_talker( u32 id ) {
  // this is NOT how you do this
  lv_label_set_text(currently_talking_label, get_username(id));
}


