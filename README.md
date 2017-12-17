### Automatic Radiosonde RX Station Extensions ###
This fork of [rs1279's RS](https://github.com/rs1729/RS) codebase provides a set of utilities ('auto_rx') to allow automatic reception and uploading of radiosonde positions to multiple services, including:
* The [Habitat High-Altitude Balloon Tracker](https://tracker.habhub.org)
* APRS-IS (for display on sites such as [aprs.fi](https://aprs.fi)
* [OziPlotter](https://github.com/projecthorus/oziplotter), for mobile radiosonde chasing.

Currently we support the following radiosonde types:
* Vaisala RS92SGP
* Vaisala RS41SGP
Support for other radiosondes may be added as required (send us sondes to test with!)

The key changes from the RS master codebase are:
* Addition of the auto_rx directory, containing the auto_rx automatic reception software
* Modifications to the rs92ecc and rs41ecc decoders, to provide telemetry output in JSON format.

Refer to the wiki for [installation and setup instructions](https://github.com/projecthorus/radiosonde_auto_rx/wiki).