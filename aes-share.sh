#!/bin/bash

usage()
{
cat << EOF
usage: ./aes-share.sh [OPTIONS] FILE [SHARES...]

Produce secret shares (computationally secure) of FILE and optionally
push to remote machines, or do the reverse.

OPTIONS:
   -h           Show this message
   -o           Output directory (defaults to /tmp)
   -k           Keep local copy of shares upon completion
   -r           Reconstruct from shares
   -l           Local operation (keep local copies, don't push to servers)
   -C           Don't use compression
   -t NUM       Require NUM shares to reconstruct (default is 2)
   -n NUM       Generate NUM shares (defaults to number of servers)

NOTE: For local operation, first file should be the encrypted data if
reconstructing, and the remaining arguments should be the shares of the
encryption key.

EXAMPLES:

    ./aes-share.sh ~/file    # share and push to configured servers.
    ./aes-share.sh -l ~/file # create shares in /tmp/

    # reconstruct from shares on configured servers:
    ./aes-share.sh -r file > /tmp/reconstructed-file
    # reconstruct from local shares:
    ./aes-share.sh -r /tmp/file.enc /tmp/shares/* > reconstructed-file

NOTE: for remote operation, each shared file must have a unique *basename*
since no path information is retained.
EOF
}

# load configured servers:
[[ -f servers.conf ]] && . servers.conf

out="/tmp"
keepshares=0
recon=0
loc=0
cmp="z"
t=2
n=${#servers[@]}
while getopts "ho:krlCt:n:" OPTION ; do
	case $OPTION in
	h)
		usage
		exit 0
		;;
	o)
		out="$OPTARG"
		;;
	k)
		keepshares=1
		;;
	l)
		loc=1
		keepshares=1
		;;
	r)
		recon=1
		;;
	C)
		cmp=""
		;;
	t)
		t="$OPTARG"
		;;
	n)
		n="$OPTARG"
		keepshares=1
		;;
	?)
		usage
		exit 0
		;;
	esac
done

# shift non-option args back to $1
shift $((OPTIND - 1))

if (( $recon == 0 )); then
	# get random key
	key=$(head -c 32 < /dev/urandom | xxd -ps -c 32)
	iv=$(head -c 16 < /dev/urandom | xxd -ps -c 16)

	# encrypt with key (XXX will show up in command line of openssl)
	bn=$(basename ${1})
	tar -c${cmp}O "$1" | \
		openssl aes-256-cbc -K $key -iv $iv -out "$out/${bn}.enc"

	# create shares of key
	sharedir=$(mktemp -d /tmp/shares-XXXXXX)
	mkdir -p $sharedir
	echo ${key}${iv} | xxd -ps -r | ./sshare -n ${n} -t ${t} -o $sharedir || \
		exit 1

	(( $loc == 1 )) && exit 0
	# else, distribute to servers
	for (( i = 1; i <= ${#servers[@]}; i++ )); do
		scp "$out/${bn}.enc" ${servers[$((i-1))]}
		scp $sharedir/$i ${servers[$((i-1))]}"${bn}.s$i"
	done

	(( $keepshares == 0 )) && rm -r $sharedir && rm "$out/${bn}.enc"
	exit 0
fi

infile="$1"
shift # remaining arguments (if present) should be shares.
if (( $# == 0 )); then
	# pull data from server based on filename in $1
	i=1
	until scp "${servers[$i]}${infile}.enc" "$out"/ || \
		(( $i == ${#servers[@]} )); do
		(( i++ ))
	done
	[[ ! -s "$out/${infile}.enc" ]] && echo "couldn't recover payload" && exit 1
	# now get the shares.
	i=0
	recovered=0
	while (( $recovered < $t && i < ${#servers[@]} )); do
		scp "${servers[$i]}${infile}.s*" "$out"/ && (( recovered++ ))
		(( i++ ))
	done
	(( $recovered < $t )) && echo "couldn't recover enough shares" && exit 1
	blob=$(./sshare "$out/${infile}".s* | xxd -ps -c 48)
	# set infile to absolute path of encrypted payload
	infile="$out/${infile}.enc"
else
	# assume infile and shares already on local filesystem
	blob=$(./sshare "$@" | xxd -ps -c 48)
fi
openssl aes-256-cbc -d -K ${blob:0:64} -iv ${blob:64} -in "$infile" | \
	tar -x${cmp}O
exit 0
