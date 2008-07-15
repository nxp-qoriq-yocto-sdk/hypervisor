
Linux Test Notes
----------------

1. 8578 Simics Memory Map Info

  -2 banks of flash are present with 8MB of flash in
   each bank:

      bank0   0xef800000
      bank1   0xef000000

2. hv-linux-1p

   Memory Map

   partition #1: loaded by the
   hypervisor from flash

     Src         Dest
     Guest       Guest
    Physical    Physical      Image
    (Flash)      (DDR)
   ---------------------------------------
    ef100000    00000000      vmlinux
      n/a       01000000       DTB
    ef600000    01300000    rootfs.ext2.gz

3. hv-linux-2p

   Memory Map

   a. partition #1: loaded by the
      hypervisor from flash

      Src        Dest
     Guest       Guest
    Physical    Physical      Image
    (Flash)      (DDR)
   ---------------------------------------
    ef100000    00000000      vmlinux
      n/a       01000000       DTB
    ef600000    01300000    rootfs.ext2.gz

   b. partition #2: loaded by the
      hypervisor from flash

     Src         Dest
     Guest       Guest
    Physical    Physical      Image
    (Flash)      (DDR)
   ---------------------------------------
    ef800000    00000000      vmlinux
      n/a       01000000       DTB
    efd00000    01300000    rootfs.ext2.gz

4. hv-partman

   Memory Map: hv-linux-partman

   partition #1: loaded by the hypervisor from
   flash

     Src         Dest
     Guest       Guest
    Physical    Physical      Image
    (Flash)      (DDR)
   ---------------------------------------
    ef100000    00000000      vmlinux
      n/a       01000000       DTB
    ef800000    01300000    rootfs.ext2.gz

   partition #2: loaded by partition manager
   running in partition #1

     Src         Dest
     Guest       Guest
    Physical    Physical      Image
    (Flash)      (DDR)
   ---------------------------------------
      n/a       00000000      vmlinux
      n/a       01000000       DTB
      n/a       01300000    rootfs.ext2.gz


