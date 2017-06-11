#!/usr/bin/python

import re
import sys

import matplotlib as mpl
mpl.use('pdf')
import matplotlib.pyplot as pyplot
from matplotlib.ticker import EngFormatter


class myplot():
	def __init__(self):
		fig, self.ax = pyplot.subplots()
		self.ax.set_xscale('log')
		self.ax.set_yscale('log')
		self.ax.set_ylim([1,1000])

		formatter = EngFormatter(unit='B', places=0)
		self.ax.xaxis.set_major_formatter(formatter)

		formatter = EngFormatter(places=0)
		self.ax.yaxis.set_major_formatter(formatter)

		pyplot.title("Latency")
		pyplot.xlabel('Size (B)')
		pyplot.ylabel('Latency (cycles)')

	def plot_one(self, input):
		x = []
		y = []
		with open(input, 'rt') as f:
			for line in f.readlines():
				x.append(float(line.split(',')[0]))
				y.append(float(line.split(',')[1]))

		self.ax.plot(x, y, label=input)

	def save(self, output):
		pyplot.legend(loc='upper left');
		pyplot.savefig(output)


m = myplot()
for arg in sys.argv[1:]:
	m.plot_one(arg)
m.save('latency.pdf')
