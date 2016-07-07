#!/bin/bash

PATH=/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/sbin:/sbin

gateway="$(ip route | awk '/default/ { print $3 }')"

while ! ping -c 1 -W 2 $gateway &> /dev/null; do
	echo "Waiting for connection"
	sleep 1
	gateway="$(ip route | awk '/default/ { print $3 }')"
done

connected=1

reconnect () {
	echo "Reconnecting"
	systemctl restart wpa_supplicant
	sleep 10
}

while true; do
	if ping -c 5 -W 2 $gateway &> /dev/null; then
		if ((\!connected)); then
			echo "Restarting relay-server"
			systemctl restart light-controller
			connected=1
		fi
		echo "Connected"
		sleep 10
	else
		reconnect
		while ! ping -c 1 -W 2 $gateway &> /dev/null; do
			echo "Not connected"
			connected=0
			reconnect
		done
	fi
done
