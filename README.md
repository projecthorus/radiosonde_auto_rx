![auto_rx logo](autorx.png)
# Automatic Radiosonde Receiver Utilities

**Please refer to the [auto_rx wiki](https://github.com/projecthorus/radiosonde_auto_rx/wiki) for the latest information.**

This project is built around [rs1279's RS](https://github.com/rs1729/RS) demodulators, and provides a set of utilities ('auto_rx') to allow automatic reception and uploading of [Radiosonde](https://en.wikipedia.org/wiki/Radiosonde) positions to multiple services, including:

* The [SondeHub Radiosonde Tracker](https://tracker.sondehub.org) - a tracking website specifically designed for tracking radiosondes!
* APRS-IS, for display on sites such as [radiosondy.info](https://radiosondy.info). (Note that aprs.fi now blocks radiosonde traffic.)
* [ChaseMapper](https://github.com/projecthorus/chasemapper) for mobile
  radiosonde chasing.

Auto-RX's [Web Interface](https://github.com/projecthorus/radiosonde_auto_rx/wiki/Web-Interface-Guide) provides a way of seeing the live status of your station, and also a means of reviewing and analysing previous radiosonde flights. Collected meteorological data can be plotted in the common 'Skew-T' format.

### Radiosonde Support Matrix

Manufacturer | Model | Position | Temperature | Humidity | Pressure | XDATA
-------------|-------|----------|-------------|----------|----------|------
Vaisala | RS92-SGP/NGP | :heavy_check_mark: | :heavy_check_mark: | :heavy_check_mark: | :heavy_check_mark: | :heavy_check_mark:
Vaisala | RS41-SG/SGP/SGM | :heavy_check_mark: | :heavy_check_mark: | :heavy_check_mark: | :heavy_check_mark: (for -SGP) | :heavy_check_mark:
Graw | DFM06/09/17 | :heavy_check_mark: | :heavy_check_mark: | :x: | :x: | :heavy_check_mark:
Meteomodem | M10 | :heavy_check_mark: | :heavy_check_mark: | :heavy_check_mark: | Not Sent | :x:
Meteomodem | M20 | :heavy_check_mark: | :heavy_check_mark: | :heavy_check_mark: | :heavy_check_mark: (For some models) | :x:
Intermet Systems | iMet-4 | :heavy_check_mark: | :heavy_check_mark: | :heavy_check_mark: | :heavy_check_mark: | :heavy_check_mark:
Intermet Systems | iMet-54 | :heavy_check_mark: | :heavy_check_mark: | :heavy_check_mark: | Not Sent | :x:
Lockheed Martin | LMS6-400/1680 | :heavy_check_mark: | :x: | :x: | :x: | Not Sent
Meisei | iMS-100 | :heavy_check_mark: | :heavy_check_mark: | :heavy_check_mark: | :x: | Not Sent
Meisei | RS11G | :heavy_check_mark: | :heavy_check_mark: | :heavy_check_mark: | :x: | Not Sent
Meteo-Radiy | MRZ-H1 (400 MHz) | :heavy_check_mark: | :heavy_check_mark: | :heavy_check_mark: | :x: | Not Sent
Meteosis | MTS01 | :heavy_check_mark: | :heavy_check_mark: | :x: | :x: | Not Sent

Support for other radiosondes may be added as required - please send us sondes to test with! If you have any information about telemetry formats, we'd love to hear from you (see our contact details below).

Improvements from the upstream RS codebase will be merged into this codebase when/where appropriate. A big thanks to rs1729 for continuing to develop and improve these decoders, and working with us to make auto_rx decode *all* the radiosondes!

### Updates

**This software is under regular development. Please [update regularly](https://github.com/projecthorus/radiosonde_auto_rx/wiki/Performing-Updates) to get bug-fixes and improvements!**

Please consider joining the Google Group to receive updates on new software features:
https://groups.google.com/forum/#!forum/radiosonde_auto_rx

We also have a channel in the SondeHub Discord server: https://sondehub.org/go/discord

## Presentations
* Linux.conf.au 2019 - https://www.youtube.com/watch?v=YBy-bXEWZeM
* UKHAS Conference 2019 - [Presented via Skype](https://youtu.be/azDJmMywBgw?t=643) which had some audio issues at the start. Slides [here](https://rfhead.net/sondes/auto_rx_presentation_UKHAS2019.pdf).

## Contacts
* [Mark Jessop](https://github.com/darksidelemm) - vk5qi@rfhead.net
* [Michaela Wheeler](https://github.com/TheSkorm) - radiosonde@michaela.lgbt

## Licensing Information
All software within this repository is licensed under the GNU General Public License v3. Refer this repositories LICENSE file for the full license text.

Radiosonde telemetry data captured via this software and uploaded into the [Sondehub](https://sondehub.org/) Database system is licensed under [Creative Commons BY-SA v2.0](https://creativecommons.org/licenses/by-sa/2.0/). 
Telemetry data uploaded into the APRS-IS network is generally considered to be released into the public domain. 

By uploading data into these systems (by enabling the relevant uploaders within the `station.cfg` file) you as the user agree for your data to be made available under these licenses. Note that uploading to Sondehub is enabled by default. 