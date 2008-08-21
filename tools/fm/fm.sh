#!/bin/sh -e

declare -r MODPROBE=${MODPROBE:-/sbin/modprobe}
declare -r TUNCTL=${TUNCTL:-tunctl}
declare -r IP=${IP:-/sbin/ip}
declare -r IPTABLES=${IPTABLES:-/sbin/iptables}
declare -r ETHTOOL=${ETHTOOL:-/usr/sbin/ethtool}
declare -r RMMOD="$MODPROBE -r"

get_tun_ifs()
{
	local eth

	for eth in /sys/class/net/*
	do
		eth=$(basename $eth)
		if [ -n "$($SUDO $ETHTOOL -i $eth 2>/dev/null | fgrep tun)" ]
		then
			echo $eth
		fi
	done
}

declare USER=$(id -nu)

if_up()
{
	local -i f i net

	for f in $(seq $1 $2)
	do
		for i in $(seq $3 $4)
		do
			let net=10*\(1+$f*5+$i\)

			$SUDO $TUNCTL -u $USER -t fman${f}_eth$i
			$SUDO $IP link set fman${f}_eth$i address 02:00:c0:a8:$(printf "%02x" $net):01
			$SUDO $IP addr add 192.168.$net.1/23 broadcast 192.168.$(expr $net + 1).255 dev fman${f}_eth$i
			$SUDO $IP link set fman${f}_eth$i up
			$IP addr show dev fman${f}_eth$i
			$SUDO $IPTABLES -I INPUT -i fman${f}_eth$i -j ACCEPT
		done
	done
}

if_down()
{
	local -i f i

	for f in $(seq $1 $2)
	do
		for i in $(seq $3 $4)
		do
			$SUDO $IPTABLES -D INPUT -i fman${f}_eth$i -j ACCEPT
			$SUDO $IP link set fman${f}_eth$i down
			$SUDO $TUNCTL -d fman${f}_eth$i
		done
	done
}

declare -i f0=0 f1=0
declare -i i0=0 i4=0

while getopts su:f:i:ah opt
do
	case $opt in
	u)
		USER=$OPTARG
		shift 2
		OPTIND=0
		;;
	s)
		SUDO=sudo
		shift
		OPTIND=0
		;;
	f)
		if [ $OPTARG -gt 1 ]
		then
			echo Valid FM indexes are 0 and 1! >&2
			exit -2
		fi
		f0=$OPTARG
		f1=$OPTARG
		shift 2
		OPTIND=0
		;;
	i)
		if [ $OPTARG -gt 4 ]
		then
			echo Valid MAC indexes are 0 \(10 Gb/s\), 1, 2, 3, 4! >&2
			exit -3
		fi
		i0=$OPTARG
		i4=$OPTARG
		shift 2
		OPTIND=0
		;;
	a)
		f0=0
		f1=1
		i0=0
		i4=4
		shift
		OPTIND=0
		;;
	h)
		echo -e Usage:\\t$(basename $0) [-s] [-u username] [-f fman] [-i mac] up
		echo -e \\t$(basename $0) [-s] [-u username] -a up
		echo -e \\t$(basename $0) [-s] [-f fman] [-i mac] down
		echo -e \\t$(basename $0) [-s] -a down
		echo -e \\t$(basename $0) -h
		echo
		echo -e \\t-s\\t\\t- Execute individual and relevant commands with sudo
		echo -e \\t-u username\\t- Set the owner of the TUN/TAP interfaces to username
		echo -e \\t-f fman\\t\\t- the FM index \(0/1\)
		echo -e \\t-i mac\\t\\t- the MAC/port index \(0 10 Gb/s, 1-4 1Gb/s\)
		echo -e \\t-a\\t\\t- All the MACs
		echo -e \\t-h\\t\\t- this help message
		exit 0
		;;
	*)
		exit -1
	esac
done

case $@ in
	u|up)
		$SUDO $MODPROBE tun
		$SUDO chmod 666 /dev/net/tun
		if_up $f0 $f1 $i0 $i4
		;;
	d|do|dow|down)
		if_down $f0 $f1 $i0 $i4
		if [ -z "$(get_tun_ifs)" ]
		then
			$SUDO $RMMOD tun
		fi
		;;
	*)
		echo Unknown command: $@ >&2
		exit -10
esac
