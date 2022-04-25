# UI Plan

#### boot screen

# select server

PTT and "radio" happen over a "repeater" server.
you have a list of these in the "codeplug". 

codeplug will automatically GET each server with your shared secret, if you have it.

  if auth:
    server will return list of your auth'd talkgroups and active stations

  if no auth:
    servers will return list of public stations

if auth fails / no key, list item for server will show NO-AUTH
if auth success, list item for server will look good

we want a way to dynamically access anyone's "radio station" at any time
without a server-switch interaction

we want to send a MQTT notify if a talkgroup member starts broadcasting

and then if we want to connect to that "radio station"...
  we open a different (or OTHER) socket conn to that server.

broadcasting happens thru your "home" repeater by default

## PTT

### select talkgroup

#### connecting to server... view

opens MQTT connection to server, start listening

#### PTT view: talkgroups and members

default view shows active talkgroups

alternate view lists all server members

#### Talkgroup view

talkgroup name, then "talking" view, if someone's talking

alternate view lists all talkgroup members

#### talking (transmitting) view

"transmitting" icon + name of talkgroup + waveform view

#### listening ( receiving ) view

talker's user icon + talkers name + waveform view

---

#### connecting to radio... view

have a "tuning" sound effect like JSRF

## Radio

### tuning view

show list of all active stations on all servers 

  public stations on un'authd servers
  
  public + private stations on auth'd servers

Broadcaster's user icon OR custom station icon

station name

broadcaster name: repeater name

lil waveform animation view??

### Radio Station ID Jingle

an audio clip "header" that plays when you connect.

broadcaster sends this to repeater when beginning broadcast

repeater saves it for the life of the broadcast session, sends it to each new listener 


