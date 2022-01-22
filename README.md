# ansible

### a low-power, secure push-to-talk device 

#### in the future, a portable silent sneakernet terminal, and a medium of secure data sync over other radio methods 

**1/22/22**

completely dropping the whole LWm2m thing

MQTT with QOS-1 (ack'd) frames.
assemble in order on the recipient end.
send a header with frame count.

### RTOS modules (zephyr)

UI module - UI events, interactions through LVGL

HW module - the physical buttons - this is tightly coupled to UI and could even combine ...?

MODEM module - self explanatory

do you separate out MQTT protocol layer into its own module? leaning towards ...no...


*radio protocol over LTE*

i wanted to just do LWm2m and i'll fall back if my idea ends up not working, but...

we essentially just want in-order packet delivery.

so, you make a recording using the PDM microphone, let's say 30 seconds max transmit time.

this gets split into frames, with a header:

**header**

+ u32 id of transmitter

+ u16 frame count

+ u32[] recipient ids, and if this[0] is 0 then it's a public message.  server handles that

**frame**

+ u16 sequence number

+ u16 buffer of FRAMESIZE 

but actually i don't like this.  i think..

header: transmitter id, recipients

frame: sequence number, buffer, bool done?

this way we can opportunistically shove encoded frames out the door
instead of filling a on-device buffer first.

*LTE-M / NB-IoT*

NRF9160

enables PTT, in concert with a MQTT server.

kicking around the idea of having "freqs" on the server.

enable both internet-radio (RX only) and communication (TX / RX) applications.

*Wi-Fi*

ESP8266 etc.

enables "distributed sneakernet".

essentially to have a...something, git repo + wiki + forum + fileserver, some kind of distributed locus of info (files), "slow" communications (long-form discussions, "letters") , and ... other stuff

the ESP8266 passively updates this network, when in range of a known and trusted *beacon*.

`security weak point!` 
*beacon* key exchange can be done over LTE / Internet or via NFC, in person.

the group data store can be viewed as some kind of tree that can be diff'd, similar to git or react?

*LoRaWAN* 

only having Wifi capability severely limits the capabilities of *beacons*.

LoRa enables better capability for long-range, passive syncing of the datastore state.

the more "mirrors" / *beacons* the better, so it follows that having more of them be able to connect is important.

--- 

there's a fundamental question of whether to enable backchanneling over the internet at all.

at the very least, file/comm permissions should allow a "No-Internet" status.
these updates would still be synced to the state (not their actual content, but a dummy update).

the "No-Internet" status would mark them for updating if ansible receives a diff from the local network with the relevant data.

if updated via internet, would only display a "Encrypted: Local Only" message as displayed in the rendering of the wiki/filestore/whatever.

---

the radio stores contact sets, from entire-community-level to smaller subgroups.

files are worked on a sans-internet computer, then updates are encrypted and synced to radio.

radio then transfers updates to each user, who holds a copy of the total state.
this happens over LTE

## Hardware Design

**AS SIMPLE AS POSSIBLE**
that means as few inputs as possible.

all physical buttons, no touch.  medium-rugged and repairable design.

shooting for ~1.75" width x 4-5"?  depth whatever.  it can be a lil thicc

3Ah+ battery.  aiming for slightly chunky nextel form factor...but appealing.

pcbway - UTR-8100 resin, dyed after the fact?

OLED module fr sure.  1-1,5"

*input interface*

+ PTT button, located as on a motorola XPR

PTT button is essentially a no go because it's for PTT and only PTT, always.

+ up/down buttons on PTT side.

+ thumb button on opposite side.

this way you grip across face of radio, index finger PTT, thumbs up/down 

+ some kind of wheel.  probably volume / power knob located top.

this is also off limits design wise.  volume / pwr only.

it's gonna need 1 or 2 more inputs than this but hopefully not many more.

*connectors*

SMA(m) connector.














