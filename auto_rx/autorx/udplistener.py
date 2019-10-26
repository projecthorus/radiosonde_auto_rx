#!/usr/bin/env python
#
#   radiosonde_auto_rx - UDP Listener
#
#   Copyright (C) 2018  Mark Jessop <vk5qi@rfhead.net>
#   Released under GNU GPL v3 or later
#

import traceback
import socket
import sys

def udp_rx_loop(hostname='localhost', port=50000):
    """
    Listen for incoming UDP packets, and emit them via stdout.
    """

    s = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
    s.settimeout(1)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    except:
        pass
    s.bind((hostname, port))
    
    while True:
        try:
            m = s.recvfrom(1024)
        except socket.timeout:
            m = None
        except KeyboardInterrupt:
            break
        except:
            traceback.print_exc()
        
        if m != None:
            try:
                sys.stdout.write(m[0])
                sys.stdout.flush()
            except:
                pass

    s.close()

if __name__ == "__main__":
    #
    # Basic UDP listener, used to feed JSON data into auto_rx for debug & testing purposes.
    #

    # User a user-defined listener port if provided.
    if len(sys.argv) > 1:
        _port = int(sys.argv[1])
    else:
        _port = 50000
    
    udp_rx_loop(port=_port)
