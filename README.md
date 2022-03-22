# Noroshi
#### Portable LTE/Wifi Radio Terminal

Noroshi is a project that aims to explore the question:
**What capabilities enabled by the Internet are compelling, and...**
**How can we provide those capabilities in a way that is:**

+ More decentralized
+ More autonomous
+ More empowering for the individual?

The Internet of today is a vastly different place from the days of its infancy.  We've moved from a "many-servers" paradigm to a "many-clients" paradigm, as monolithic platforms have conquered the space.  The majority of Internet communication now exists in a TV-like paradigm, with a single-digit number of service providers.

These providers provide a variety of technological capabilities, each with unique social and cultural relevance, which overlap and can be broadly grouped into "live" and "static" groups, though the "liveness" is a spectrum.


### Live Capabilities
**Streaming** - Youtube, Twitch, IG Live, FB Live.
**Group Chat** - Discord, Slack, IRC (barely hanging on)
**Messaging** - Every platform

### Slow Capabilities
**Microblogging** - Twitter, IG, FB
**File Sharing** - Google Drive, Dropbox, OneDrive, etc
**Email** - Gmail, of course
**~~Blogging~~**  doesn't exist anymore
**~~Websites~~** don't exist anymore (in social or cultural relevance)


---

What would it look like to envision off-internet networking at a community or friend-group level?  And what capabilities could we provide while remaining free from the Internet or monolithic service providers?

The Internet provides a variety of capabilities, from realtime, streaming applications to static data.  We aim to attack this problem on both ends - one, the streaming, realtime, low-latency side of Internet functionality, using the NRF9160 to communicate over LTE; and the other, the slower, static side, using the ESP32 to enable peer-to-peer data exchange over Wifi.

### On the LTE side, a "radio" mode of using internet connectivity.  

+ Secure, frictionless, realtime push-to-talk communication, as with a VHF/UHF radio.

+ Broadcasting and receiving streaming audio, as with an FM radio.

Both of these applications are out of reach for the "regular citizen", especially within a city.  Ham radio does not enable encrypted communications, and is limited by regulation to a pure hobbyist pursuit.  Ham cannot be used for group-exclusive communications, and has a variety of limitations and frictions that prevent it from being a viable day-to-day communication tool.

On the other side, to set up a private VHF/UHF PTT capability within a city, a customer must lease radios from a vertically integrated provider, because only these will own repeater space on the local coverage points; you can get licensed, but a prohibitively expensive outlay is required to actually achieve full coverage in a metropolitan area.

These regulatory limitations prevent VHF/UHF from being used in an autonomous, secure, and self-sufficient manner.  Either you're a HAM, limited to communications-as-a-hobby - QSLs, etc; or you're a lessee of service from a provider.

This project aims to take a specific, compelling application of cell technology (low-latency audio streaming) and build a dedicated, stripped-down, and focused product for it, providing a more autonomous and open-source way to use IoT without relying on current monolithic tech ecosystems.

The goal of this project is to provide a different vision of communication over cell networks.  The value of this radio is in it being a dedicated object with a focused purpose and frictionless UX.  We aim for the main mode of interaction with the object to be physical, with the screen providing extremely concise information.  As far as possible from the interaction mode of a mobile app, we want this to be a physical device that fits well in the hand, has interactions recognizable by feel, and can be used without looking at the screen.

To illustrate this, mobile apps like Zello enable PTT functionality; but they also require buying into the entire cell phone ecosystem, from the hardware to the OS, and your communications go through an opaque signed binary to Zello's servers.  Cell phone apps have ever-present interaction friction at every level - multiple touch interactions to talk by default (unlocking the screen to hit the talk button, etc); coexisting with other apps on the phone, which can interfere with its operation or availability; difficulty of manipulating the phone in the hand; and so on.

Similarly, internet radio already exists; but there is no *dedicated* and simplified way to interact with it, and as such the technology is effectively dead.  Streaming video platforms such as Youtube or Twitch are now the dominant platform for audio streaming, because they provide lower barriers to entry and vastly better UX than Shoutcast or similar.  By providing a simple and frictionless way to broadcast and receive audio in realtime, we aim to empower individual users and rejuvenate a dying medium, with everyone being the owner, operator, and DJ of their own radio station, not through an app, but through a dedicated physical device.  Ultimately, I envision this as pushing a second age of radio, with individuals able to frictionlessly "speak" to an audience - every individual having space within the frequency spectrum, free of barriers to entry.

### Wifi / LoRa - Wifi for a peer-to-peer sneakernet, decoupled from the Internet.

The aim of the Wifi side of functionality is to provide a way to slowly and organically share static data among a local community without using the Internet - in other words, to provide a truly autonomous form of social networking.  

This aims at a similar area as existing platforms such as Mastodon that aim to provide decentralized alternatives to monolithic social networks. This project aims to provide a generic and open-ended framework for all types of information exchange - social applications like microblogging, as well as media and file sharing, forums, and wikis.  It also aims to provide a hardy and simple method for updating this shared state, that can be used not just with WiFi, but with other wireless connections - LoRa, etc.

## Design Methodology

We envision a community's "shared state" as a traditional filesystem, with each member possessing a directory.  At a global level, there are N numbers of dedicated *reserved filenames* that indicate the contextual meaning of the data.  a directory named "blog" will be viewed on the client side as that user's blog, similar to a twitter feed.  all files within that directory will be parsed as Markdown and displayed as blog entries in a time-sorted list based on the file's creation date.

### Connection

The public keys of "friends" in the community are indicated by the SSID broadcasted by the peer AP, and the shared secret is used as the WPA2 password.  With both devices in Station+SoftAP mode, operating in both modes simultaneously, we open a TCP socket connection.

### Syncing

After connecting to the peer, both clients send hashes of each "friend"'s directory, as well as timestamps of its last update.  both clients symmetrically use this info to determine the friends for which they own newer data, and the friends for which they own older data.

Another *reserved file* is the "friends" file.  This file exists within each person's directory at the top level, and describes permission groups (The ESP32 has a stripped-down libc, but runs on FreeRTOS, and has a very spare filesystem abstraction layer.)

We use **librsync**, specifically **rdiff**, to handle generating file signatures, deltas, and patching.  Signature files are generated AoT, not at connection time.

The friend directory trees are walked with **ftw**, permissions are checked, and the fds of new / updated signature files are pushed to a queue.  These signature files are compressed and sent; used to generate deltas; and deltas are compressed and sent, completing the sync process.

