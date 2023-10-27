#!/usr/bin/env python
#
#   RS41 SubFrame Handling Utilities
#
import base64
import json
import logging
import struct
import requests

class RS41Subframe(object):
    def __init__(self, filename=None, raw_bytes=None):
        """
        RS41 Subframe Storage and data extraction
        """

        # Output data
        self.data = {}

        self.raw_data = b''

        if filename:
            # Read in binary file
            _f = open(filename, 'rb')
            self.raw_data = _f.read()
            _f.close()

        if raw_bytes:
            self.raw_data = raw_bytes

        if (filename is None) and (raw_bytes is None):
            raise IOError("Either a filename, or raw_bytes must be provided!")

        if (len(self.raw_data) != 800) and (len(self.raw_data) != 816):
            raise IOError("Subframe data must be either 800 or 816 bytes in length!")

        # TODO - Check CRC16

        # Extract main cal fields
        self.parse_subframe()

        # Extract runtime-variable area
        if len(self.raw_data) == 816:
            self.parse_runtime_variable()
    
        pass


    def check_subframe_crc(self):
        pass

    def parse_subframe(self):
        """
        Extract all available fields from the binary subframe data

        Reference: https://github.com/einergehtnochrein/ra-firmware/blob/master/src/rs41/rs41private.h#L233
        """

        self.data['frequency'] = 400 + 0.01*self.extract_uint16(0x002)/64
        self.data['startupTxPower'] = self.extract_uint8(0x004)
        self.data['optionFlags'] = self.extract_uint16(0x007)
        self.data['serial'] = self.extract_string(0x00D, 8).rstrip(b'\x00').decode()
        self.data['firmwareVersion'] = self.extract_uint32(0x015)
        self.data['minHeight4Flight'] = self.extract_uint16(0x019)
        self.data['lowBatVoltageThreshold'] = self.extract_uint8(0x01B)*0.1 # Volts - Guess!
        self.data['nfcDetectorThreshold'] = self.extract_uint8(0x01C)*0.025 # Volts
        self.data['refTemperatureTarget'] = self.extract_int8(0x021)
        self.data['lowBatCapacityThreshold'] = self.extract_uint8(0x022)
        self.data['flightKillFrames'] = self.extract_int16(0x027)
        self.data['burstKill'] = self.extract_uint8(0x02B) # Convert to enum
        self.data['freshBatteryCapacity'] = self.extract_uint16(0x02E)
        self.data['allowXdata'] = self.extract_uint8(0x032)
        self.data['ubloxHwVersionHigh'] = self.extract_uint16(0x033)
        self.data['ubloxHwVersionLow'] = self.extract_uint16(0x035)
        self.data['ubloxSwVersion'] = self.extract_uint16(0x037)
        self.data['ubloxSwBuild'] = self.extract_uint16(0x039)
        self.data['ubloxConfigErrors'] = self.extract_uint8(0x03B)
        self.data['radioVersionCode'] = self.extract_uint8(0x03C)
        # Main PTU Calibration Fields
        self.data['refResistorLow'] = self.extract_float(0x03D)
        self.data['refResistorHigh'] = self.extract_float(0x041)
        self.data['refCapLow'] = self.extract_float(0x045)
        self.data['refCapHigh'] = self.extract_float(0x049)
        self.data['taylorT'] = self.extract_float_array(0x04D,3)
        self.data['calT'] = self.extract_float(0x059)
        self.data['polyT'] = self.extract_float_array(0x05D, 6)
        self.data['calibU'] = self.extract_float_array(0x075, 2)
        self.data['matrixU'] = self.extract_float_array(0x07D, 7, 6)
        self.data['taylorTU'] = self.extract_float_array(0x125, 3)
        self.data['calTU'] = self.extract_float(0x131)
        self.data['polyTrh'] = self.extract_float_array(0x135, 6)
        # Other status/config fields
        self.data['startIWDG'] = self.extract_uint8(0x1EC)
        self.data['parameterSetupDone'] = self.extract_uint8(0x1ED)
        self.data['enableTestMode'] = self.extract_uint8(0x1EE)
        self.data['enableTx'] = self.extract_uint8(0x1EF)
        self.data['pressureLaunchSite'] = self.extract_float_array(0x210, 2) # Unsure if this is right?
        # Board version/serial numbers
        self.data['variant'] = self.extract_string(0x218, 10).rstrip(b'\x00').decode()
        self.data['mainboard_version'] = self.extract_string(0x222, 10).rstrip(b'\x00').decode()
        self.data['mainboard_serial'] = self.extract_string(0x22C, 9).rstrip(b'\x00').decode()
        self.data['pressureSensor_serial'] = self.extract_string(0x243, 8).rstrip(b'\x00').decode()
        # More status/config fields
        self.data['xdataUartBaud'] = self.extract_uint8(0x253) # Convert to enum
        self.data['cpuTempSensorVoltageAt25deg'] = self.extract_float(0x255)
        # Pressure sensor calibration data
        self.data['matrixP'] = self.extract_float_array(0x25E, 18)
        self.data['vectorBp'] = self.extract_float_array(0x2A6, 3)
        self.data['matrixBt'] = self.extract_float_array(0x2BA, 12)
        # More settings
        self.data['burstKillFrames'] = self.extract_int16(0x316)



    def parse_runtime_variable(self):
        """
        Extract all available runtime-variable fields from the binary subframe data

        Reference: https://github.com/einergehtnochrein/ra-firmware/blob/master/src/rs41/rs41private.h#L233
        """

        self.data['killCountdown'] = self.extract_int16(0x320)
        self.data['launchAltitude'] = self.extract_int16(0x322)
        self.data['heightOfFlightStart'] = self.extract_uint16(0x324)
        self.data['lastTxPowerLevel'] = self.extract_uint8(0x326)
        self.data['numSoftwareResets'] = self.extract_uint8(0x327)
        self.data['intTemperatureCpu'] = self.extract_int8(0x328)
        self.data['intTemperatureRadio'] = self.extract_int8(0x329)
        self.data['remainingBatteryCapacity'] = self.extract_uint16(0x32A)
        self.data['numUbxDiscarded'] = self.extract_uint8(0x32C)
        self.data['numUbxStall'] = self.extract_uint8(0x32D)


    def extract_uint16(self, address):
        _r = struct.unpack('<H', self.raw_data[address:address+2])
        return _r[0]

    def extract_uint8(self, address):
        _r = struct.unpack('<B', self.raw_data[address:address+1])
        return _r[0]

    def extract_uint32(self, address):
        _r = struct.unpack('<I', self.raw_data[address:address+4])
        return _r[0]

    def extract_int8(self, address):
        _r = struct.unpack('<b', self.raw_data[address:address+1])
        return _r[0]

    def extract_int16(self, address):
        _r = struct.unpack('<h', self.raw_data[address:address+2])
        return _r[0]

    def extract_float(self, address):
        _r = struct.unpack('<f', self.raw_data[address:address+4])
        return _r[0]

    def extract_float_array(self, address, x, y=1):
        _struct_format = '<' + 'f'*x*y
        _r = struct.unpack(_struct_format, self.raw_data[address:address+4*x*y])
        
        if y == 1:
            return list(_r)
        else:
            # Reshape...
            return list(_r)

    def extract_string(self, address, len):
        _r = struct.unpack('<'+str(len)+'s', self.raw_data[address:address+len])
        return _r[0]

    def extract_enum_uint8(self, address, mapping):
        pass



def download_subframe_data(serial):
    """
    Download radiosonde telemetry data for a given radiosonde serial and
    search for a frame containing subframe data
    """

    _url = "https://api.v2.sondehub.org/sonde/" + serial

    try:
        logging.info(f"Starting download of telemetry for serial {serial}")
        logging.debug(f"URL: {_url}")
        r = requests.get(_url)
        data = r.json()
        logging.info(f"Downloaded {len(data)} frames of telemetry")
    except Exception as e:
        logging.error("Error downloading telemetry data")
        logging.exception(e)
        return None
    
    # Now search through the data for the first one with subframe data

    subframe_data = None

    for telem in data:
        if 'rs41_subframe' in telem:
            subframe_data = base64.b64decode(telem['rs41_subframe'])
            return (subframe_data, telem)
        
    logging.error("Could not find subframe data in downloaded telemetry.")
    return (None, None)






if __name__ == "__main__":
    import logging
    import argparse
    import sys
    import pprint


    parser = argparse.ArgumentParser()
    parser.add_argument("filename", default="U5054395", help="Either a path to a subframe file (ending in .bin), or a RS41 serial number.")
    parser.add_argument("-v", "--verbose", action='store_true', default=False, help="Show additional debug info.")
    args = parser.parse_args()

    if args.verbose:
        logging_level = logging.DEBUG
    else:
        logging_level = logging.INFO

    # Set up logging
    logging.basicConfig(format="%(asctime)s %(levelname)s: %(message)s", level=logging_level)

    subframe = None

    if args.filename.endswith('subframe.bin'):
        subframe = RS41Subframe(filename=args.filename)

    else:
        # Assume this is a serial number and try and download it.
        (subframe_data, telem) = download_subframe_data(args.filename)

        if subframe_data:
            subframe = RS41Subframe(raw_bytes=subframe_data)

            print("Telemetry data at time of subframe collection:")
            pprint.pprint(telem)



    if subframe:
        print("Subframe contents:")
        pprint.pprint(subframe.data)

    