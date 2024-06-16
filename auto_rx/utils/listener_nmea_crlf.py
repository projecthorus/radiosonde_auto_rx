#!/usr/bin/env python
#
#   UDP to NMEA Converter.
#   Listen for telemetry messages via UDP, and write NMEA strings to stdout.
#
#   Copyright (C) 2018  Mark Jessop <vk5qi@rfhead.net>
#   Copyright (C) 2020  Vigor Geslin
#   Released under GNU GPL v3 or later
#


import socket, json, sys, traceback
from threading import Thread
from dateutil.parser import parse
import datetime
from io import StringIO
import time


MAX_JSON_LEN = 32768


def fix_datetime(datetime_str, local_dt_str = None):
    '''
    Given a HH:MM:SS string from an telemetry sentence, produce a complete timestamp, using the current system time as a guide for the date.
    '''

    if local_dt_str is None:
        _now = datetime.datetime.now(datetime.timezone.utc)
    else:
        _now = parse(local_dt_str)

    # Are we in the rollover window?
    if _now.hour == 23 or _now.hour == 0:
        _outside_window = False
    else:
        _outside_window = True
    
    # Append on a timezone indicator if the time doesn't have one.
    if datetime_str.endswith('Z') or datetime_str.endswith('+00:00'):
        pass
    else:
        datetime_str += "Z"


    # Parsing just a HH:MM:SS will return a datetime object with the year, month and day replaced by values in the 'default'
    # argument. 
    _telem_dt = parse(datetime_str, default=_now)

    if _outside_window:
        # We are outside the day-rollover window, and can safely use the current zulu date.
        return _telem_dt
    else:
        # We are within the window, and need to adjust the day backwards or forwards based on the sonde time.
        if _telem_dt.hour == 23 and _now.hour == 0:
            # Assume system clock running slightly fast, and subtract a day from the telemetry date.
            _telem_dt = _telem_dt - datetime.timedelta(days=1)

        elif _telem_dt.hour == 00 and _now.hour == 23:
            # System clock running slow. Add a day.
            _telem_dt = _telem_dt + datetime.timedelta(days=1)

        return _telem_dt

def udp_listener_nmea_callback(info):
    ''' Handle a Payload Summary Message from UDPListener '''

    dateRS = datetime.datetime.strptime(info['time'], '%H:%M:%S')

    hms = dateRS.hour*10000.0+dateRS.minute*100.0+dateRS.second+dateRS.microsecond;
    dateNMEA = dateRS.year%100+dateRS.month*100+dateRS.day*10000

    lat = int(info['latitude']) * 100 + (info['latitude'] - int(info['latitude'])) * 60
    ns = ''
    if lat < 0 :
        lat *= -1.0
        ns = 'S'
    else:
        ns = 'N'

    lon = int(info['longitude']) * 100 + (info['longitude'] - int(info['longitude'])) * 60
    ew = ''
    if lon < 0 :
        lon *= -1.0
        ew = 'W'
    else:
        ew = 'E'

    speed = info['speed'] * 3.6/1.852

    geoid = 44
    course = info['heading']
    alt = info['altitude']

    bufGPRMC = StringIO()
    bufGPRMC.write('GPRMC,%010.3f,A,%08.3f,%s,%09.3f,%s,%.2f,%.2f,%06d,,' % (hms, lat, ns, lon, ew, speed, course, dateNMEA))
    
    gprmc = bufGPRMC.getvalue()

    cs_grpmc = 0
    for c in gprmc:
        cs_grpmc ^= ord(c)
    bufGPRMC.write('*%02X' % (cs_grpmc))

    bufLine=bufGPRMC.getvalue()
    
    sys.stdout.write('$')
    sys.stdout.write(bufLine)
    sys.stdout.write('\r\n')

    bufGPGGA = StringIO()
    
    bufGPGGA.write('GPGGA,%010.3f,%08.3f,%s,%09.3f,%s,1,04,0.0,%.3f,M,%.1f,M,,' % (hms, lat, ns, lon, ew, alt - geoid, geoid))
    
    gpgga = bufGPGGA.getvalue()
    cs_gpgga = 0
    for d in gpgga:
        cs_gpgga ^= ord(d)

    bufGPGGA.write('*%02X' % (cs_gpgga))

    bufLine=bufGPGGA.getvalue()
    sys.stdout.write('$')
    sys.stdout.write(bufLine)
    sys.stdout.write('\r\n')
    sys.stdout.flush()


class UDPListenerNMEA(object):
    ''' UDP Broadcast Packet Listener 
    Listens for Horuslib UDP broadcast packets, and passes them onto a callback function
    '''

    def __init__(self,
        callback=None,
        summary_callback = None,
        gps_callback = None,
        bearing_callback = None,
        port=55672):

        self.udp_port = port
        self.callback = callback
        self.summary_callback = summary_callback
        self.gps_callback = gps_callback
        self.bearing_callback = bearing_callback

        self.listener_thread = None
        self.s = None
        self.udp_listener_running = False


    def handle_udp_packet(self, packet):
        ''' Process a received UDP packet '''
        try:
            packet_dict = json.loads(packet)

            if self.callback is not None:
                self.callback(packet_dict)

            if packet_dict['type'] == 'PAYLOAD_SUMMARY':
                if self.summary_callback is not None:
                    self.summary_callback(packet_dict)


        except Exception as e:
            print("Could not parse packet: %s" % str(e))
            traceback.print_exc()


    def udp_rx_thread(self):
        ''' Listen for Broadcast UDP packets '''

        self.s = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        self.s.settimeout(1)
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            self.s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        except:
            pass
        self.s.bind(('',self.udp_port))
        #print("Started UDP Listener Thread.")
        self.udp_listener_running = True

        while self.udp_listener_running:
            try:
                m = self.s.recvfrom(MAX_JSON_LEN)
            except socket.timeout:
                m = None
            except:
                traceback.print_exc()
            
            if m != None:
                self.handle_udp_packet(m[0])
        
        #print("Closing UDP Listener")
        self.s.close()


    def start(self):
        if self.listener_thread is None:
            self.listener_thread = Thread(target=self.udp_rx_thread)
            self.listener_thread.start()


    def close(self):
        self.udp_listener_running = False
        self.listener_thread.join()

if __name__ == '__main__':
    
    try:
        _telem_horus_udp_listener = UDPListenerNMEA(summary_callback=udp_listener_nmea_callback,
                                                    gps_callback=None,
                                                    bearing_callback=None,
                                                    port=55673)
        _telem_horus_udp_listener.start()
        
        while True:
            time.sleep(0.1)

    except (KeyboardInterrupt, SystemExit): #when you press ctrl+c
            #print "\nKilling Thread..."
            _telem_horus_udp_listener.close()
#print "Done.\nExiting."
