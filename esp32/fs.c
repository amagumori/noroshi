
const esp_vfs_t vfs;

int init_vfs ( void ) {
  vfs = {
    .flags = ESP_VFS_FLAG_DEFAULT,
    .write = &
}
