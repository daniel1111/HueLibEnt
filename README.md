LibHueEnt - A library to control Hue entertainment areas from C
---------------------------------------------------------------
There are two main parts to this project:
* The LibHueEnt library which can be used to control up to 10 hue lights simultaneously with up to 25 updates per second. It is _not_ suitable for infrequently toggling single lamps. See [API docs](https://raw.githack.com/daniel1111/HueLibEnt/master/docs/index.html)
* HueVis - An audio visualiser for hue lights using LibHueEnt, heavily based on the [CAVA project](https://github.com/karlstav/cava/)

There is also an additional example app (BasicColourFade) which is a basic example of using LibHueEnt to cycle though colours on each bulb in the entertainment area in turn. Unlike HueVis, this app isn't capable of registering itself with the bridge, so that needs to be done manually using the REST API on the bridge (for HueVis).

Requirements
------------
* OS: Currently only tested on Linux / Debian Stretch
* Hue: A v2 Hue bridge on API version >= 1.22 

Building
--------

Debian:

    apt-get install make libconfig-dev libssl-dev libjson-c-dev libfftw3-dev libpulse-dev libcurl4-gnutls-dev cmake

Then run `cmake .` followed by `make` in the repository root.


## Usage
Before LibHueEnt will work, an entertainment area must be set up using the Hue app. This can be done from Settings > Entertainment Areas > Create entertainment area, then add some rooms / bulbs to the area. Once set up, run though the test to check it's working.

## BasicColourFade
BasicColourFade is intended to be a simple example of using LibHueEnt, and is missing basic features like being able register itself with the bridge and using a config file, so everything must supplied on the command line.

So, to use it:
1. Either follow the instructions on the [Hue website](https://developers.meethue.com/develop/hue-entertainment/philips-hue-entertainment-api/) to register with the bridge and get a username & clientkey, or use HueVis to register with the bridge and take its credentials from the generated bridge_credentials.conf file
2. From the root directory, run `./bin/bcf -a <ip address> -i <username> -p <clientkey>`

All going well, you should something along the lines of:

    Getting entertainment areas
    Enabling entertainment area [Entertainment area 1]
    Making DTLS connection to bridge
    Running...
    Light = 0 (id = 12)

And each light in the entertainment cycling though colours in turn.

## HueVis
HueVis is really the reason LibHueEnt exists - I wanted a music visualiser for Hue lights, but couldn't find anything for Linux that would use the entertainment area stuff that was recently(-ish) added to Hue.
All the logic for audio capture and processing is pretty much ripped off from the [CAVA project](https://github.com/karlstav/cava/), which is designed to show a bar spectrum audio visualizer on the console.
HueVis currently has two modes of operation (set in huevis.conf):
1. cava_mode=1 - The brightness of each bulb represents what would have been a bar in the CAVA output. This means that all lamps are always the same colour, and the brighness of each light is (generally) different
2. cava_mode=2 - CAVA is used to generate the equivalent of 3 bars, and the values are used for the value of Red/Green/Blue for all bulbs

### Configuration
The `huevis.conf` file includes comments for the few options HueVis currently has, but the important one is `audio_input` - this controls where HueVis gets the audio from. The options are:
* pulse - Use pulseaudio, the default can be specified using `pulse_source`, but if blank, the default device is used.
* squeezelite - Gets audio from a running instance of [squeezelite](https://github.com/ralph-irving/squeezelite). Note that must be built with visualiser support and the `-v` parameter passed to it
 
### Usage
After building, the first step is to register HueVis with the bridge. From the root directory, press the link button on the bridge and then _within 30 seconds_ run `./bin/HueVis -r <ip address of bridge>`. HueVis should then register with the bridge and write the connection details to a `bridge_credentials.conf` file.

### Example
Example of HueVis running:

    $ ./bin/huevis 
    Getting entertainment areas
    Enabling entertainment area [Entertainment area 1]
    Making DTLS connection to bridge
    Init audio
    Using audio source: alsa_output.pci-0000_00_1b.0.analog-stereo.monitor
    Start audio capture & processing
    Running...


[![HueVis](http://img.youtube.com/vi/OZpMm7RhmM8/0.jpg)](https://youtu.be/OZpMm7RhmM8)

## Hutil
A simple utility that can register with the bridge, show the whitelist (registered applications) and list configured entertainment areas.

### Example
Registering; after pressing the link button on th bridge:

    $ ./bin/hutil -r 192.0.2.10
    Registered with hue bridge, config saved

Showing entertainment areas:

    $ ./bin/hutil -e

    Entertainment areas:
    ID      Name                             Light IDs
    6       Entertainment area 1             12 13 15 14 



## TODO
LibHueEnt:
* Allow automatic bridge discovery - instead of always requiring an IP address to be entered - by following the notes on the [Hue website](https://developers.meethue.com/develop/application-design-guidance/hue-bridge-discovery/)
* A better example - more involved than BasicColourFade e.g. with registration, maybe using multiple entertainment areas, etc

HueVis:
* Most settings available in the CAVA project currently have hard-coded values. Some of these should probably be exposed in the config file
* Other modes. The code is structured to allow adding something other than CAVA to process the audio at some point. CAVA works well, but other options could be interesting. In particular, a mode that uses varying colour and the same time as different bulbs doing different things would be good
