![auto_rx logo](autorx.png)
# Automatic Radiosonde Receiver Utilities

**Please refer to the [auto_rx wiki](https://github.com/projecthorus/radiosonde_auto_rx/wiki) for the latest information.**

This fork of [rs1279's RS](https://github.com/rs1729/RS) codebase
provides a set of utilities ('auto_rx') to allow automatic reception
and uploading of
[Radiosonde](https://en.wikipedia.org/wiki/Radiosonde) positions to
multiple services, including:

* The [Habitat High-Altitude Balloon Tracker](https://tracker.habhub.org)

  * **Please note the HabHub Tracker now filters out radiosondes by
      default.** To view the radiosondes again, clear the search field
      at the top-left of the tracker of all text, and press
      enter. Alternatively, use our front-end to HabHub at:
      [https://sondehub.org/](https://sondehub.org/)
  
* APRS-IS (for display on sites such as [aprs.fi](https://aprs.fi)

* [ChaseMapper](https://github.com/projecthorus/chasemapper) and
  [OziPlotter](https://github.com/projecthorus/oziplotter), for mobile
  radiosonde chasing.

There is also a web interface provided (defaults to port 5000),
allowing display of station status and basic tracking of the sonde
position.

Currently we support the following radiosonde types:
* Vaisala RS92
* Vaisala RS41
* Graw DFM06/DFM09/DFM17/PS-15
* Meteomodem M10 (Thanks Viproz!)
* Intermet iMet-4 (and 'narrowband' iMet-1 sondes)
* Lockheed Martin LMS6 and Mk2a
* Meisei iMS-100

Support for other radiosondes may be added as required (please send us
sondes to test with!)

This software performs the following steps:

1. Use rtl_power to scan across a user-defined frequency range, and
   detect peaks in the spectrum.

2. For each detected peak frequency, run the rs_detect utility, which
   determines if a radiosonde signal is present, and what type it is.

3. If a radiosonde signal is found, start demodulating it, and upload
   data to various internet services.

4. If no peaks are found, or if no packets are heard from the
   radiosonde in a given amount of time (2 minutes by default), go back
   to step 1.

The latest version can make use of multiple RTLSDRs to allow for
tracking of many radiosondes simultaneously. The number of
simultaneous radiosondes you can track is limited only by the number
of RTLSDRs you have setup!

Refer to the wiki for the [latest updates, and installation/setup
instructions](https://github.com/projecthorus/radiosonde_auto_rx/wiki).

**This software is under regular development. Please [update
  regularly](https://github.com/projecthorus/radiosonde_auto_rx/wiki/Performing-Updates)
  to get bug-fixes and improvements!**

### Contacts
* [Mark Jessop](https://github.com/darksidelemm) - vk5qi@rfhead.net
* [Michael Wheeler](https://github.com/TheSkorm) - git@mwheeler.org

You can often find us in the #highaltitude IRC Channel on
[Freenode](https://webchat.freenode.net/).
