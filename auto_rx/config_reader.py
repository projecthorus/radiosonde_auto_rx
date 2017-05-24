#!/usr/bin/env python
#
# Radiosonde Auto RX Tools - Configuration File Parser
#
# 2017-04 Mark Jessop <vk5qi@rfhead.net>
#
import ConfigParser
import logging
import traceback

def read_auto_rx_config(filename):
	# Configuration Defaults:
	auto_rx_config = {
		'rtlsdr_ppm'	:	0,
		'rtlsdr_gain'	:	0,
		'rtlsdr_bias'	: False,
		'search_attempts':	5,
		'search_delay'	: 120,
		'min_freq'		: 400.4,
		'max_freq'		: 404.0,
		'search_step'	: 800,
		'min_snr'		: 10,
		'min_distance'	: 1000,
		'quantization'	: 10000,
		'rx_timeout'	: 120,
		'upload_rate'	: 30,
		'synchronous_upload' : False,
		'enable_aprs'	: False,
		'enable_habitat': False,
		'aprs_user'		: 'N0CALL',
		'aprs_pass'		: '00000',
		'aprs_object_id': '<id>',
		'aprs_custom_comment': 'Radiosonde Auto-RX <freq>',
		'payload_callsign': 'RADIOSONDE',
		'uploader_callsign': 'SONDE_AUTO_RX',
		'uploader_lat' 	: 0.0,
		'uploader_lon'	: 0.0
	}

	try:
		config = ConfigParser.RawConfigParser()
		config.read(filename)

		auto_rx_config['rtlsdr_ppm'] = config.getint('rtlsdr', 'rtlsdr_ppm')
		auto_rx_config['rtlsdr_gain'] = config.getint('rtlsdr', 'rtlsdr_gain')
		auto_rx_config['rtlsdr_bias'] = config.getboolean('rtlsdr', 'rtlsdr_bias')
		auto_rx_config['search_attempts'] = config.getint('search_params', 'search_attempts')
		auto_rx_config['search_delay'] = config.getint('search_params', 'search_delay')
		auto_rx_config['min_freq'] = config.getfloat('search_params', 'min_freq')
		auto_rx_config['max_freq'] = config.getfloat('search_params', 'max_freq')
		auto_rx_config['search_step'] = config.getfloat('search_params', 'search_step')
		auto_rx_config['min_snr'] = config.getfloat('search_params', 'min_snr')
		auto_rx_config['min_distance'] = config.getfloat('search_params', 'min_distance')
		auto_rx_config['quantization'] = config.getint('search_params', 'quantization')
		auto_rx_config['rx_timeout'] = config.getint('search_params', 'rx_timeout')
		auto_rx_config['upload_rate'] = config.getint('upload', 'upload_rate')
		auto_rx_config['synchronous_upload'] = config.getboolean('upload','synchronous_upload')
		auto_rx_config['enable_aprs'] = config.getboolean('upload', 'enable_aprs')
		auto_rx_config['enable_habitat'] = config.getboolean('upload', 'enable_habitat')
		auto_rx_config['aprs_user'] = config.get('aprs', 'aprs_user')
		auto_rx_config['aprs_pass'] = config.get('aprs', 'aprs_pass')
		auto_rx_config['aprs_object_id'] = config.get('aprs', 'aprs_object_id')
		auto_rx_config['aprs_custom_comment'] = config.get('aprs', 'aprs_custom_comment')
		auto_rx_config['payload_callsign'] = config.get('habitat', 'payload_callsign')
		auto_rx_config['uploader_callsign'] = config.get('habitat', 'uploader_callsign')
		auto_rx_config['uploader_lat'] = config.getfloat('habitat', 'uploader_lat')
		auto_rx_config['uploader_lon'] = config.getfloat('habitat', 'uploader_lon')

		return auto_rx_config

	except:
		traceback.print_exc()
		logging.error("Could not parse config file, using defaults.")
		return auto_rx_config

