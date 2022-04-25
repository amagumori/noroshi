# ESP32 side 

friend ids are stored as 64-bit (?) uuids with low collision hash type thing

# Sync Process

friend ids are stored as 64-bit (?) uuids with low collision hash - murmur3 etc

send and compare friend uuids

iterate 1deep root '/' - directories that have common hashes as directory name
  check dir mtimes - if mtimes match, skip

for each dir:

do a file tree walk

  per file:
    send path + mtime
    recv path + mtime
    if mtimes match within epsilon: skip

    if dir mtime matches, don't walk into it!!

    if remote mtime newer - immediately send sig, expect delta to come back
    if remote mtime older - expect sig to come in directly, send delta back
  
  if mtime check is different between local and remote - ...???

could also explicitly do this as a client / server, which makes more sense...
but how do you pick who is who.


## frictionless, automatic data syncing between friends.

basically:

+ any time you're on your home SSID, a directory you select on your computer will be rsync'd to noroshi.

+ any time a friend SSID is visible, noroshi will silently do an rdiff and sync all file deltas.

---

kind of want to do SPIFFS bc flat directory structure, simple to iterate.


