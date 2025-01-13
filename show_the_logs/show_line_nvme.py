#!/usr/bin/python3

from utils import *
import sys
import matplotlib.pyplot as plt

if not (len(sys.argv) > 1):
    print("argument not provided")
    sys.exit(1)

filename = "../../nvme_probe/records.bin"

data = read_records_nvme(filename)
# print(len(data))
x1, y1 = [], []

for i in range(0, len(data) - 1):
    m = data[i + 1] - data[i]
    x1.append(i)
    y1.append(m)
print(len(x1))
fig = plt.figure(1)
ax = fig.add_subplot(111)
ax.plot(x1, y1, linewidth=0.25)

ax.set_xlabel("Time")
ax.set_ylabel("Number of cycles")
ax.set_ylim(0, 350000)

filename = "nvme" + str(sys.argv[1]) + ".png"
# plt.savefig(filename, dpi=300)
