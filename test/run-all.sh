
files="test/hello/hv-hello.simics \
	test/msgsnd/hv-msgsnd.simics \
	test/vmpic/hv-vmpic.simics \
	test/linux/hv-linux-1p.simics \
	test/linux/hv-linux-intmap.simics \
	test/linux/hv-partman.simics \
	test/linux/hv-linux-2p.simics \
	test/ipi/hv-ipi.simics \
	test/ipi/hv-ipi-2p.simics \
	test/whoami/hv-whoami.simics \
	test/hcalls/hv-hcalls.simics \
	test/byte-chan/hv-byte_chan.simics \
	test/fit/hv-fit.simics"

for i in $files
do
	echo $i
	simics $i -e c
done
