# OziPlotter push utils for Sonde auto RX.

import socket
import json


# Network Settings
HORUS_UDP_PORT = 55672
HORUS_OZIPLOTTER_PORT = 8942

def send_payload_summary(callsign, latitude, longitude, altitude, packet_time, speed=-1, heading=-1, comment= '', udp_port = HORUS_UDP_PORT):
    """
    Send an update on the core payload telemetry statistics into the network via UDP broadcast.
    This can be used by other devices hanging off the network to display vital stats about the payload.
    """
    packet = {
        'type' : 'PAYLOAD_SUMMARY',
        'callsign' : callsign,
        'latitude' : latitude,
        'longitude' : longitude,
        'altitude' : altitude,
        'speed' : speed,
        'heading': heading,
        'time' : packet_time,
        'comment' : comment
    }

    # Set up our UDP socket
    s = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
    s.settimeout(1)
    # Set up socket for broadcast, and allow re-use of the address
    s.setsockopt(socket.SOL_SOCKET,socket.SO_BROADCAST,1)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    except:
        pass
    s.bind(('',HORUS_UDP_PORT))
    try:
        s.sendto(json.dumps(packet), ('<broadcast>', udp_port))
    except socket.error:
        s.sendto(json.dumps(packet), ('127.0.0.1', udp_port))


# The new 'generic' OziPlotter upload function, with no callsign, or checksumming (why bother, really)
def oziplotter_upload_basic_telemetry(time, latitude, longitude, altitude, hostname="192.168.88.2", udp_port = HORUS_OZIPLOTTER_PORT):
    """
    Send a sentence of position data to Oziplotter, via UDP.
    """
    sentence = "TELEMETRY,%s,%.5f,%.5f,%d\n" % (time, latitude, longitude, altitude)

    try:
        ozisock = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        ozisock.sendto(sentence,(hostname,udp_port))
        ozisock.close()
    except Exception as e:
        print("Failed to send to Ozi: " % e)


def push_telemetry_to_ozi(telemetry, hostname='127.0.0.1', udp_port = HORUS_OZIPLOTTER_PORT):
    """ 
    Grab the relevant fields from the incoming telemetry dictionary, and pass onto oziplotter.
    """
    oziplotter_upload_basic_telemetry(telemetry['short_time'], telemetry['lat'], telemetry['lon'], telemetry['alt'], hostname=hostname, udp_port=udp_port)


def push_payload_summary(telemetry, udp_port = HORUS_UDP_PORT):
    """
    Extract the needed data from a telemetry dictionary, and send out a payload summary packet.
    """
    # Insert the payload type, frequency and ID as a comment field.
    summary_comment = "%s,%s,%s" % (telemetry['type'], telemetry['freq'], telemetry['id'])

    # Prepare heading & speed fields, if they are provided in the incoming telemetry blob.
    if 'heading' in telemetry.keys():
        _heading = telemetry['heading']
    else:
        _heading = -1

    if 'vel_h' in telemetry.keys():
        _speed = telemetry['vel_h']*3.6
    else:
        _speed = -1

    send_payload_summary(telemetry['id'], 
                        telemetry['lat'], 
                        telemetry['lon'], 
                        telemetry['alt'], 
                        telemetry['short_time'],
                        heading=_heading,
                        speed=_speed, 
                        comment=summary_comment, 
                        udp_port=udp_port)

