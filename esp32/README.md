# ESP32 side 

## frictionless, automatic data syncing between friends.

basically:

+ any time you're on your home SSID, a directory you select on your computer will be rsync'd to noroshi.

+ any time a friend SSID is visible, noroshi will silently do an rdiff and sync all file deltas.

---

kind of want to do SPIFFS bc flat directory structure, simple to iterate.


