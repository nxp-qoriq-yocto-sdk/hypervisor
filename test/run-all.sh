
files="test/hello/hv-hello.simics \
	test/msgsnd/hv-msgsnd.simics \
	test/vmpic/hv-vmpic.simics \
	test/vmpic/hv-vmpic-coreint.simics \
	test/linux/hv-linux-1p.simics \
	test/linux/hv-linux-intmap.simics \
	test/linux/hv-partman.simics \
	test/linux/hv-linux-2p.simics \
	test/ipi/hv-ipi.simics \
	test/ipi/hv-ipi-2p.simics \
	test/ipi/hv-ipi-coreint.simics \
	test/whoami/hv-whoami.simics \
	test/byte-chan/hv-byte_chan.simics \
	test/mmu/mmu.simics \
	test/dma/hv-dma.simics \
	test/pmr/hv-pmr.simics \
	test/fit/hv-fit.simics \
	test/mcheck/hv-mcheck.simics \
	test/watchdog/hv-watchdog.simics \
	test/gdebug/gdebug.simics"

for i in $files
do
	echo $i
	simics $i -e c
done
