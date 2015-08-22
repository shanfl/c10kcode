#!/bin/sh
# Example of how to run Poller_bench and get kernel profiling results, too

set -e 
set -x

# On my system, at least, readprofile is in /usr/sbin
PATH=$PATH:/usr/sbin

./configure --enable-ndebug
make clean
make Poller_bench profile
rm -f *.dat

# If we're root, allow ourselves enough fd's to run test
ulimit -n 21000
# On Linux, following magic incantations raise a systemwide limit
if [ -f /proc/sys/fs/file-max ]; then
	echo 42000 > /proc/sys/fs/file-max
fi
if [ -f /proc/sys/fs/inode-max ]; then
	echo 42000 > /proc/sys/fs/inode-max
fi

# Run for 5 seconds at each combination of {select,poll,/dev/poll} x {100,1000,10000 pipes}
./Poller_bench 5 1 spd 100 1000 10000 > log

echo Results:
cat log

# If you want kernel profiling results on Linux,
# make sure /usr/src/linux/System.map is up to date,
# boot with "profile=2", and run Poller_bench as root.  
# It will then output kernel profile files
# for each test condition named 'bench%d%c.dat', 
# where %d is the number of pipes,
# and %c is the polling method (s = select, p = poll, d = /dev/poll)

# Example of how to interpret those files:
if [ -f bench100p.dat ]; then
	for a in *.dat ; do
		readprofile -p $a | sort -rn | head > $a.top
	done
	echo Gross kernel profile results are in '*.top'
fi

# Another way to interpret these is a fine-grained profile; for
# this, you need to specify a function to show details for.
if [ -f bench100d.dat ]; then
	for a in bench*d.dat ; do
		./profile dp_poll $a > $a.dp_poll
	done
	echo "Fine kernel profile results for dp_poll() are in '*.dp_poll'"
	# You can then do 'objdump -d /usr/src/linux/vmlinux > foo' and
	# look at foo for the disassembly of dp_poll to get some idea 
	# what the hotspots mean.  Or so I've been told.
fi
