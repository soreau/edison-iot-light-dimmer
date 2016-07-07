#!/bin/bash

/home/root/scripts/init_DIG.sh -o 3 -d output
/home/root/scripts/init_DIG.sh -o 2 -d input

for ((i=6; i<14; i++)); do
	/home/root/scripts/init_DIG.sh -o $i -d input
done
