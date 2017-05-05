# APRS push utils for Sonde auto RX.

from socket import *


# Push a Radiosonde data packet to APRS as an object.
def push_balloon_to_aprs(sonde_data, object_name="<id>", aprs_comment="BOM Balloon", aprsUser="N0CALL", aprsPass="00000", serverHost = 'rotate.aprs2.net', serverPort = 14580):
	if object_name == "<id>":
		object_name = sonde_data["id"].strip()
	
	# Pad or limit the object name to 9 characters.
	if len(object_name) > 9:
		object_name = object_name[:9]
	elif len(object_name) < 9:
		object_name = object_name + " "*(9-len(object_name))
	
	# Convert float latitude to APRS format (DDMM.MM)
	lat = float(sonde_data["lat"])
	lat_degree = abs(int(lat))
	lat_minute = abs(lat - int(lat)) * 60.0
	lat_min_str = ("%02.2f" % lat_minute).zfill(5)
	lat_dir = "S"
	if lat>0.0:
		lat_dir = "N"
	lat_str = "%02d%s" % (lat_degree,lat_min_str) + lat_dir
	
	# Convert float longitude to APRS format (DDDMM.MM)
	lon = float(sonde_data["lon"])
	lon_degree = abs(int(lon))
	lon_minute = abs(lon - int(lon)) * 60.0
	lon_min_str = ("%02.2f" % lon_minute).zfill(5)
	lon_dir = "E"
	if lon<0.0:
		lon_dir = "W"
	lon_str = "%03d%s" % (lon_degree,lon_min_str) + lon_dir
	
	# Convert Alt (in metres) to feet
	alt = int(float(sonde_data["alt"])/0.3048)

	# TODO: Process velocity/heading, if supplied.

	# TODO: Limit comment length.
	
	# Produce the APRS object string.

	if ('heading' in sonde_data.keys()) and ('vel_h' in sonde_data.keys()):
		course_speed = "%03d/%03d" % (int(sonde_data['heading']), int(sonde_data['vel_h']*1.944))
	else:
		course_speed = "000/000"

	out_str = ";%s*111111z%s/%sO%s/A=%06d %s" % (object_name,lat_str,lon_str,course_speed,alt,aprs_comment)
	
	# Connect to an APRS-IS server, login, then push our object position in.
	
	# create socket & connect to server
	sSock = socket(AF_INET, SOCK_STREAM)
	sSock.connect((serverHost, serverPort))
	# logon
	sSock.send('user %s pass %s vers VK5QI-Python 0.01\n' % (aprsUser, aprsPass) )
	# send packet
	sSock.send('%s>APRS:%s\n' % (aprsUser, out_str) )
	
	# close socket
	sSock.shutdown(0)
	sSock.close()

	return out_str