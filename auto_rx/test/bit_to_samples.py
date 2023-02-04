#!/usr/bin/env python
#
#	Convert one-byte-per-bit representation (i.e. from fsk_demod) to a pseudo-FM-demod representation.
#
import sys


_sample_rate = int(sys.argv[1])
_symbol_rate = int(sys.argv[2])

_bit_length = _sample_rate // _symbol_rate

_zero = b'\x00'*_bit_length
_one = b'\xFF'*_bit_length

_in = sys.stdin.read
_out = sys.stdout.buffer.write

while True:
	_char = _in(1)

	if _char == '' or _char == b'':
		sys.exit(0)

	if _char == '\x00':
		_out(_zero)
	else:
		_out(_one)

	sys.stdout.flush()
