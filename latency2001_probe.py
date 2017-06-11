#!/usr/bin/python

# Get the load latency for a range of working set sizes.
#
# Starts with a power of 2 sweep then goes back and fills in the areas
# of large (> %10) change.
#
# Copyright (C) 2014 Anton Blanchard <anton@au.ibm.com>, IBM
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version
# 2 of the License, or (at your option) any later version.

import subprocess
import csv
import sys
from optparse import OptionParser

class latency():
	def __init__(self, min_size=1*1024, max_size=256*1024*1024, largepage=False, alloc_cpu=-1, run_cpu=-1, report_time=False, latency_binary='./latency2001'):
		self.min_size = min_size
		self.max_size = max_size
		self.largepage = largepage
		self.alloc_cpu = alloc_cpu
		self.run_cpu = run_cpu
		self.report_time = report_time
		self.latency_binary = latency_binary
		self.sizes = []
		self.arch = subprocess.check_output(['uname', '-m']).rstrip()


	def first_pass(self):
		size = self.min_size
		while size <= self.max_size:
			sys.stderr.write("%d\n" % size)

			ns = self.latency(size)
			self.sizes.append((size, ns))
			size *= 2


	def latency(self, size):
		cmd = ['setarch', self.arch, '-R', self.latency_binary, '-C' ]
		if self.largepage:
			cmd.append('-l')
		if self.alloc_cpu != -1:
			cmd.append('-a')
			cmd.append('%d' % self.alloc_cpu)
		if self.run_cpu != -1:
			cmd.append('-c')
			cmd.append('%d' % self.run_cpu)
		cmd.append('%d' % size)
		output = subprocess.check_output(cmd).splitlines()

		reader = csv.DictReader(output, fieldnames=['size', 'cycles', 'ns'])

		for row in reader:
			pass

		if self.report_time:
			return float(row['ns'])
		else:
			return float(row['cycles'])


	def fill_in_blanks(self, difference=0.1):
		prev_size = 0
		prev_ns = 0
		for (size, ns) in sorted(self.sizes):
			if prev_size:
				diff = 1.0 * ((ns - prev_ns) / prev_ns)

				if abs(diff) > difference:
					new_size = prev_size + ((size - prev_size) / 2)

					if (new_size & (1024-1)) == 0:
						sys.stderr.write("%d\n" % new_size)
						ns = self.latency(new_size)
						self.sizes.append((new_size, ns))

			prev_size = size
			prev_ns = ns


	def dump_csv(self):
		for (size, ns) in sorted(self.sizes):
			print '%d,%.2f' % (size, ns)


parser = OptionParser()

parser.add_option('-a', dest='alloc_cpu', type=int, default=-1,
		help='CPU to allocate memory on')

parser.add_option('-c', dest='run_cpu', type=int, default=-1,
		help='CPU to run on')

parser.add_option('-l', action='store_true', dest='largepage', default=False,
		help='Use large pages')

parser.add_option('-t', action='store_true', dest='report_time', default=False,
		help='Report latency as time (default processor cycles)')

(options, args) = parser.parse_args()

l = latency(alloc_cpu=options.alloc_cpu, run_cpu=options.run_cpu, largepage=options.largepage, report_time=options.report_time)
l.first_pass()
for i in range(20):
	l.fill_in_blanks()

l.dump_csv()
