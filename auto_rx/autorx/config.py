#!/usr/bin/env python
#
#   radiosonde_auto_rx - Configuration File Reader
#
#   Copyright (C) 2018  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#

import logging
import traceback
import json
from .utils import rtlsdr_test

try:
    # Python 2
    from ConfigParser import RawConfigParser
except ImportError:
    # Python 3
    from configparser import RawConfigParser

def read_auto_rx_config(filename):
	""" Read an Auto-RX v2 Station Configuration File.

	This function will attempt to parse a configuration file.
	It will also confirm the accessibility of any SDRs specified in the config file.

	Args:
		filename (str): Filename of the configuration file to read.

	Returns:
		auto_rx_config (dict): The configuration dictionary.
		sdr_config (dict): A dictionary with SDR parameters.
	"""
	# Configuration Defaults:
	auto_rx_config = {
		# Log Settings
		'per_sonde_log' : True,
		# SDR Settings
		'sdr_fm': 'rtl_fm',
		'sdr_power': 'rtl_power',
		'sdr_quantity': 1,
		# Search Parameters
		'min_freq'		: 400.4,
		'max_freq'		: 404.0,
		'rx_timeout'	: 120,
		'whitelist'	: [],
		'blacklist'	: [],
		'greylist'	: [],
		# Location Settings
		'station_lat'	: 0.0,
		'station_lon'	: 0.0,
		'station_alt'	: 0.0,
		# Position Filter Settings
		'max_altitude'	: 50000,
		'max_radius_km'	: 1000,
		# Habitat Settings
		'habitat_enabled': False,
		'habitat_upload_rate': 30,
		'habitat_uploader_callsign': 'SONDE_AUTO_RX',
		'habitat_uploader_antenna': '1/4-wave',
		'habitat_upload_listener_position': False,
		'habitat_payload_callsign': '<id>',
		# APRS Settings
		'aprs_enabled'	: False,
		'aprs_upload_rate': 30,
		'aprs_user'		: 'N0CALL',
		'aprs_pass'		: '00000',
		'aprs_server'	: 'rotate.aprs2.net',
		'aprs_object_id': '<id>',
		'aprs_custom_comment': 'Radiosonde Auto-RX <freq>',
		# Advanced Parameters
		'search_step'	: 800,
		'snr_threshold'		: 10,
		'min_distance'	: 1000,
		'dwell_time'	: 10,
		'max_peaks'		: 10,
		'quantization'	: 10000,
		'synchronous_upload' : False,
		'scan_dwell_time' : 20,
		'detect_dwell_time' : 5,
		'scan_delay' : 10,
		'payload_id_valid' : 5, 
		# Rotator Settings
		'enable_rotator': False,
		'rotator_hostname': '127.0.0.1',
		'rotator_port'	: 4533,
		'rotator_homing_enabled': False,
		'rotator_home_azimuth': 0,
		'rotator_home_elevation': 0,
		# OziExplorer Settings
		'ozi_enabled'	: False,
		'ozi_update_rate': 5,
		'ozi_port'		: 55681,
		'payload_summary_enabled': False,
		'payload_summary_port' : 55672
	}

	sdr_settings = {}#'0':{'ppm':0, 'gain':-1, 'bias': False}}

	try:
		config = RawConfigParser(auto_rx_config)
		config.read(filename)

		# Log Settings
		auto_rx_config['per_sonde_log'] = config.getboolean('logging', 'per_sonde_log')

		# SDR Settings
		auto_rx_config['sdr_fm'] = config.get('advanced', 'sdr_fm_path')
		auto_rx_config['sdr_power'] = config.get('advanced', 'sdr_power_path')
		auto_rx_config['sdr_quantity'] = config.getint('sdr', 'sdr_quantity')

		# Search Parameters
		auto_rx_config['min_freq'] = config.getfloat('search_params', 'min_freq')
		auto_rx_config['max_freq'] = config.getfloat('search_params', 'max_freq')
		auto_rx_config['rx_timeout'] = config.getint('search_params', 'rx_timeout')
		auto_rx_config['whitelist'] = json.loads(config.get('search_params', 'whitelist'))
		auto_rx_config['blacklist'] = json.loads(config.get('search_params', 'blacklist'))
		auto_rx_config['greylist'] = json.loads(config.get('search_params', 'greylist'))

		# Location Settings
		auto_rx_config['station_lat'] = config.getfloat('location', 'station_lat')
		auto_rx_config['station_lon'] = config.getfloat('location', 'station_lon')
		auto_rx_config['station_alt'] = config.getfloat('location', 'station_alt')

		# Position Filtering
		auto_rx_config['max_altitude'] = config.getint('filtering', 'max_altitude')
		auto_rx_config['max_radius_km'] = config.getint('filtering', 'max_radius_km')

		# Habitat Settings
		auto_rx_config['habitat_enabled'] = config.getboolean('habitat', 'habitat_enabled')
		auto_rx_config['habitat_upload_rate'] = config.getint('habitat', 'upload_rate')
		auto_rx_config['habitat_payload_callsign'] = config.get('habitat', 'payload_callsign')
		auto_rx_config['habitat_uploader_callsign'] = config.get('habitat', 'uploader_callsign')
		auto_rx_config['habitat_upload_listener_position'] = config.getboolean('habitat','upload_listener_position')

		# APRS Settings
		auto_rx_config['aprs_enabled'] = config.getboolean('aprs', 'aprs_enabled')
		auto_rx_config['aprs_upload_rate'] = config.getint('aprs', 'upload_rate')
		auto_rx_config['aprs_user'] = config.get('aprs', 'aprs_user')
		auto_rx_config['aprs_pass'] = config.get('aprs', 'aprs_pass')
		auto_rx_config['aprs_server'] = config.get('aprs', 'aprs_server')
		auto_rx_config['aprs_object_id'] = config.get('aprs', 'aprs_object_id')
		auto_rx_config['aprs_custom_comment'] = config.get('aprs', 'aprs_custom_comment')

		# OziPlotter Settings
		auto_rx_config['ozi_enabled'] = config.getboolean('oziplotter', 'ozi_enabled')
		auto_rx_config['ozi_update_rate'] = config.getint('oziplotter', 'ozi_update_rate')
		auto_rx_config['ozi_port'] = config.getint('oziplotter', 'ozi_port')
		auto_rx_config['payload_summary_enabled'] = config.getboolean('oziplotter', 'payload_summary_enabled')
		auto_rx_config['payload_summary_port'] = config.getint('oziplotter', 'payload_summary_port')

		# Advanced Settings
		auto_rx_config['search_step'] = config.getfloat('advanced', 'search_step')
		auto_rx_config['snr_threshold'] = config.getfloat('advanced', 'snr_threshold')
		auto_rx_config['min_distance'] = config.getfloat('advanced', 'min_distance')
		auto_rx_config['dwell_time'] = config.getint('advanced', 'dwell_time')
		auto_rx_config['quantization'] = config.getint('advanced', 'quantization')
		auto_rx_config['max_peaks'] = config.getint('advanced', 'max_peaks')
		auto_rx_config['scan_dwell_time'] = config.getint('advanced', 'scan_dwell_time')
		auto_rx_config['detect_dwell_time'] = config.getint('advanced', 'detect_dwell_time')
		auto_rx_config['scan_delay'] = config.getint('advanced', 'scan_delay')
		auto_rx_config['payload_id_valid'] = config.getint('advanced', 'payload_id_valid')
		auto_rx_config['synchronous_upload'] = config.getboolean('advanced', 'synchronous_upload')

		# Rotator Settings (TBC)
		auto_rx_config['rotator_enabled'] = config.getboolean('rotator','rotator_enabled')
		auto_rx_config['rotator_update_rate'] = config.getint('rotator', 'update_rate')
		auto_rx_config['rotator_hostname'] = config.get('rotator', 'rotator_hostname')
		auto_rx_config['rotator_port'] = config.getint('rotator', 'rotator_port')
		auto_rx_config['rotator_homing_enabled'] = config.getboolean('rotator', 'rotator_homing_enabled')
		auto_rx_config['rotator_home_azimuth'] = config.getfloat('rotator', 'rotator_home_azimuth')
		auto_rx_config['rotator_home_elevation'] = config.getfloat('rotator', 'rotator_home_elevation')


		# New setting in this version (20180616). Keep it in a try-catch to avoid bombing out if the new setting isn't present.
		try:
			auto_rx_config['habitat_uploader_antenna'] = config.get('habitat', 'uploader_antenna').strip()
		except:
			logging.error("Config - Missing uploader_antenna setting. Using default.")
			auto_rx_config['habitat_uploader_antenna'] = '1/4-wave'

		# Now we attempt to read in the individual SDR parameters.
		auto_rx_config['sdr_settings'] = {}

		for _n in range(1,auto_rx_config['sdr_quantity']+1):
			_section = "sdr_%d" % _n
			try:
				_device_idx = config.get(_section,'device_idx')
				_ppm = config.getint(_section, 'ppm')
				_gain = config.getfloat(_section, 'gain')
				_bias = config.getboolean(_section, 'bias')

				if (auto_rx_config['sdr_quantity'] > 1) and (_device_idx == '0'):
					logging.critical("Config - SDR Device ID of 0 used with a multi-SDR configuration. Go read the warning in the config file!")
					return None

				# See if the SDR exists.
				_sdr_valid = rtlsdr_test(_device_idx)
				if _sdr_valid:
					auto_rx_config['sdr_settings'][_device_idx] = {'ppm':_ppm, 'gain':_gain, 'bias':_bias, 'in_use': False, 'task': None}
					logging.info('Config - Tested SDR #%s OK' % _device_idx)
				else:
					logging.warning("Config - SDR #%s invalid." % _device_idx)
			except Exception as e:
				logging.error("Config - Error parsing SDR %d config - %s" % (_n,str(e)))
				continue

		# Sanity checks when using more than one SDR
		if (len(auto_rx_config['sdr_settings'].keys()) > 1) and (auto_rx_config['habitat_payload_callsign'] != "<id>"):
			logging.critical("Fixed Habitat Payload callsign used in a multi-SDR configuration. Go read the warnings in the config file!")
			return None

		if (len(auto_rx_config['sdr_settings'].keys()) > 1) and (auto_rx_config['aprs_object_id'] != "<id>"):
			logging.critical("Fixed APRS object ID used in a multi-SDR configuration. Go read the warnings in the config file!")
			return None

		if (len(auto_rx_config['sdr_settings'].keys()) > 1) and (auto_rx_config['rotator_enabled']):
			logging.critical("Rotator enabled in a multi-SDR configuration. Go read the warnings in the config file!")
			return None

		# TODO: Revisit this limitation once the OziPlotter output sub-module is complete.
		if (len(auto_rx_config['sdr_settings'].keys()) > 1) and (auto_rx_config['ozi_enabled'] or auto_rx_config['payload_summary_enabled']):
			logging.critical("Chase car outputs (OziPlotter/Payload Summary) enabled in a multi-SDR configuration.")
			return None


		if len(auto_rx_config['sdr_settings'].keys()) == 0:
			# We have no SDRs to use!!
			logging.error("Config - No working SDRs! Cannot run...")
			return None
		else:
			return auto_rx_config


	except:
		traceback.print_exc()
		logging.error("Could not parse config file.")
		return None


if __name__ == '__main__':
	''' Quick test script to attempt to read in a config file. '''
	import sys, pprint
	logging.basicConfig(format='%(asctime)s %(levelname)s:%(message)s', level=logging.DEBUG)

	config = read_auto_rx_config(sys.argv[1])

	pprint.pprint(config)