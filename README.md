# Automatic Radiosonde Receiver Utilities
This fork of [rs1279's RS](https://github.com/rs1729/RS) codebase provides a set of utilities ('auto_rx') to allow automatic reception and uploading of [Radiosonde](https://en.wikipedia.org/wiki/Radiosonde) positions to multiple services, including:
* The [Habitat High-Altitude Balloon Tracker](https://tracker.habhub.org)
* APRS-IS (for display on sites such as [aprs.fi](https://aprs.fi)
* [OziPlotter](https://github.com/projecthorus/oziplotter), for mobile radiosonde chasing.

Currently we support the following radiosonde types:
* Vaisala RS92SGP
* Vaisala RS41SGP

Support for other radiosondes may be added as required (send us sondes to test with!)

This software performs the following steps:
1. Use rtl_power to scan across a user-defined frequency range, and detect peaks in the spectrum.
2. For each detected peak frequency, run the rs_detect utility, which determines if a radiosonde signal is present, and what type it is.
3. If a radiosonde signal is found, start demodulating it, and upload data to various internet services.
4. If no peaks are found, or if no packets are heard from the radiosonde in a given amount of time (2 minutes by default), go back to step 1.

Refer to the wiki for [installation and setup instructions](https://github.com/projecthorus/radiosonde_auto_rx/wiki).

### Contacts
* [Mark Jessop](https://github.com/darksidelemm) - vk5qi@rfhead.net
* [Michael Wheeler](https://github.com/TheSkorm) - git@mwheeler.org

You can often find us in the #highaltitude IRC Channel on [Freenode](https://webchat.freenode.net/).
