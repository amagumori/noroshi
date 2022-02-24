
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


