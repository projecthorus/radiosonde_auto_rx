#!/usr/bin/env python
#
# Radiosonde Auto RX Tools - Configuration File Parser
#
# 2017-04 Mark Jessop <vk5qi@rfhead.net>
#
import ConfigParser
import logging
import traceback
import json

def read_auto_rx_config(filename):
	# Configuration Defaults:
	auto_rx_config = {
		'per_sonde_log' : True,
		'sdr_fm_path': 'rtl_fm',
		'sdr_power_path': 'rtl_power',
		'sdr_ppm'	:	0,
		'sdr_gain'	:	-1,
		'sdr_bias'	: False,
		'search_attempts':	5,
		'search_delay'	: 10,
		'min_freq'		: 400.4,
		'max_freq'		: 404.0,
		'search_step'	: 800,
		'min_snr'		: 10,
		'min_distance'	: 1000,
		'dwell_time'	: 10,
		'quantization'	: 10000,
		'rx_timeout'	: 120,
		'station_lat'	: 0.0,
		'station_lon'	: 0.0,
		'station_alt'	: 0.0,
		'upload_rate'	: 30,
		'synchronous_upload' : False,
		'enable_aprs'	: False,
		'enable_habitat': False,
		'aprs_user'		: 'N0CALL',
		'aprs_pass'		: '00000',
		'aprs_server'	: 'rotate.aprs2.net',
		'aprs_object_id': '<id>',
		'aprs_custom_comment': 'Radiosonde Auto-RX <freq>',
		'payload_callsign': '<id>',
		'payload_description': 'Meteorological Radiosonde',
		'uploader_callsign': 'SONDE_AUTO_RX',
		'upload_listener_position': False,
		'enable_rotator': False,
		'rotator_hostname': '127.0.0.1',
		'rotator_port'	: 4533,
		'rotator_homing_enabled': False,
		'rotator_home_azimuth': 0,
		'rotator_home_elevation': 0,
		'ozi_enabled'	: False,
		'ozi_update_rate': 5,
		'ozi_hostname'	: '127.0.0.1',
		'ozi_port'		: 55681,
		'mqtt_enabled'	: False,
		'mqtt_hostname'	: '127.0.0.1',
		'mqtt_port'		: 1883,
		'payload_summary_enabled': False,
		'payload_summary_port' : 55672,
		'whitelist'	: [],
		'blacklist'	: [],
		'greylist'	: [],
		'max_altitude'	: 50000,
		'max_radius_km'	: 1000,
		'payload_id_valid' : 5 # TODO: Add this to config file in next bulk update.
	}

	try:
		config = ConfigParser.RawConfigParser(auto_rx_config)
		config.read(filename)

		auto_rx_config['per_sonde_log'] = config.getboolean('logging', 'per_sonde_log')
		auto_rx_config['sdr_fm_path'] = config.get('sdr','sdr_fm_path')
		auto_rx_config['sdr_power_path'] = config.get('sdr','sdr_power_path')
		auto_rx_config['sdr_ppm'] = int(config.getfloat('sdr', 'sdr_ppm'))
		auto_rx_config['sdr_gain'] = config.getfloat('sdr', 'sdr_gain')
		auto_rx_config['sdr_bias'] = config.getboolean('sdr', 'sdr_bias')
		auto_rx_config['search_attempts'] = config.getint('search_params', 'search_attempts')
		auto_rx_config['search_delay'] = config.getint('search_params', 'search_delay')
		auto_rx_config['min_freq'] = config.getfloat('search_params', 'min_freq')
		auto_rx_config['max_freq'] = config.getfloat('search_params', 'max_freq')
		auto_rx_config['search_step'] = config.getfloat('search_params', 'search_step')
		auto_rx_config['min_snr'] = config.getfloat('search_params', 'min_snr')
		auto_rx_config['min_distance'] = config.getfloat('search_params', 'min_distance')
		auto_rx_config['dwell_time'] = config.getint('search_params', 'dwell_time')
		auto_rx_config['quantization'] = config.getint('search_params', 'quantization')
		auto_rx_config['rx_timeout'] = config.getint('search_params', 'rx_timeout')
		auto_rx_config['station_lat'] = config.getfloat('location', 'station_lat')
		auto_rx_config['station_lon'] = config.getfloat('location', 'station_lon')
		auto_rx_config['station_alt'] = config.getfloat('location', 'station_alt')
		auto_rx_config['upload_rate'] = config.getint('upload', 'upload_rate')
		auto_rx_config['synchronous_upload'] = config.getboolean('upload','synchronous_upload')
		auto_rx_config['enable_aprs'] = config.getboolean('upload', 'enable_aprs')
		auto_rx_config['enable_habitat'] = config.getboolean('upload', 'enable_habitat')
		auto_rx_config['aprs_user'] = config.get('aprs', 'aprs_user')
		auto_rx_config['aprs_pass'] = config.get('aprs', 'aprs_pass')
		auto_rx_config['aprs_server'] = config.get('aprs', 'aprs_server')
		auto_rx_config['aprs_object_id'] = config.get('aprs', 'aprs_object_id')
		auto_rx_config['aprs_custom_comment'] = config.get('aprs', 'aprs_custom_comment')
		auto_rx_config['payload_callsign'] = config.get('habitat', 'payload_callsign')
		auto_rx_config['payload_description'] = config.get('habitat', 'payload_description')
		auto_rx_config['uploader_callsign'] = config.get('habitat', 'uploader_callsign')
		auto_rx_config['upload_listener_position'] = config.getboolean('habitat','upload_listener_position')
		auto_rx_config['enable_rotator'] = config.getboolean('rotator','enable_rotator')
		auto_rx_config['rotator_hostname'] = config.get('rotator', 'rotator_hostname')
		auto_rx_config['rotator_port'] = config.getint('rotator', 'rotator_port')
		auto_rx_config['rotator_homing_enabled'] = config.getboolean('rotator', 'rotator_homing_enabled')
		auto_rx_config['rotator_home_azimuth'] = config.getfloat('rotator', 'rotator_home_azimuth')
		auto_rx_config['rotator_home_elevation'] = config.getfloat('rotator', 'rotator_home_elevation')
		auto_rx_config['ozi_enabled'] = config.getboolean('oziplotter', 'ozi_enabled')
		auto_rx_config['ozi_update_rate'] = config.getint('oziplotter', 'ozi_update_rate')
		auto_rx_config['ozi_port'] = config.getint('oziplotter', 'ozi_port')
		auto_rx_config['payload_summary_enabled'] = config.getboolean('oziplotter', 'payload_summary_enabled')
		auto_rx_config['payload_summary_port'] = config.getint('oziplotter', 'payload_summary_port')

		# Read in lists using a JSON parser.
		auto_rx_config['whitelist'] = json.loads(config.get('search_params', 'whitelist'))
		auto_rx_config['blacklist'] = json.loads(config.get('search_params', 'blacklist'))
		auto_rx_config['greylist'] = json.loads(config.get('search_params', 'greylist'))

		# Position Filtering
		auto_rx_config['max_altitude'] = config.getint('filtering', 'max_altitude')
		auto_rx_config['max_radius_km'] = config.getint('filtering', 'max_radius_km')

		# MQTT settings
		auto_rx_config['mqtt_enabled'] = config.getboolean('mqtt', 'mqtt_enabled')
		auto_rx_config['mqtt_hostname'] = config.get('mqtt', 'mqtt_hostname')
		auto_rx_config['mqtt_port'] = config.getint('mqtt', 'mqtt_port')

		return auto_rx_config

	except:
		traceback.print_exc()
		logging.error("Could not parse config file, using defaults.")
		return auto_rx_config


if __name__ == '__main__':
	''' Quick test script to attempt to read in a config file. '''
	import sys
	print(read_auto_rx_config(sys.argv[1]))


