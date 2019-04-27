#!/usr/bin/env python
import sys

_sample_rate = int(sys.argv[1])
_symbol_rate = int(sys.argv[2])

_bit_length = _sample_rate // _symbol_rate

_zero = '\x00'*_bit_length
_one = '\xFF'*_bit_length

while True:
	_char = sys.stdin.read(1)
	if _char == '':
		sys.exit(0)

	if _char == '\x00':
		sys.stdout.write(_zero)
	else:
		sys.stdout.write(_one)

	sys.stdout.flush()
