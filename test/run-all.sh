
files="test/hello-test/hv-hello.simics \
	test/msgsnd-test/hv-msgsnd.simics \
	test/vmpic-test/hv-vmpic.simics \
	test/linux/hv-linux-1p.simics \
	test/linux/hv-linux-2p.simics \
	test/ipi_doorbell-test/hv-ipi.simics \
	test/ipi_doorbell-test/hv-ipi-2p.simics \
	test/fit-test/hv-fit.simics"

for i in $files
do
	simics $i -e c
done