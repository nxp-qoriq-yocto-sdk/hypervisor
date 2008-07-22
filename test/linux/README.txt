
Linux Test Notes
----------------

1. 8578 Simics Memory Map Info

   One 128MB bank of flash is present at 0xe8000000.

   U-boot, hypevisor image, and hypervisor

    Physical                  Image
    (Flash)
   ---------------------------------------
    e8020000                  hypervisor DTB
    e8040000                  hypervisor image

2. hv-linux-1p

   Memory Map

   partition #1: loaded by the
   hypervisor from flash

     Src         Dest
     Guest       Guest
    Physical    Physical      Image
    (Flash)      (DDR)
   ---------------------------------------
    e8100000    00000000      vmlinux
      n/a       01000000      guest DTB
    e8600000    01300000      rootfs.ext2.gz

3. hv-linux-2p

   Memory Map

   a. partition #1: loaded by the
      hypervisor from flash

      Src        Dest
     Guest       Guest
    Physical    Physical      Image
    (Flash)      (DDR)
   ---------------------------------------
    e8100000    00000000      vmlinux
      n/a       01000000      guest DTB
    e8600000    01300000      rootfs.ext2.gz

   b. partition #2: loaded by the
      hypervisor from flash

     Src         Dest
     Guest       Guest
    Physical    Physical      Image
    (Flash)      (DDR)
   ---------------------------------------
    e8800000    00000000      vmlinux
      n/a       01000000      DTB
    e8d00000    01300000      rootfs.ext2.gz

4. hv-partman

   Memory Map: hv-linux-partman

   partition #1: loaded by the hypervisor from
   flash

     Src         Dest
     Guest       Guest
    Physical    Physical      Image
    (Flash)      (DDR)
   ---------------------------------------
    e8100000    00000000      vmlinux
      n/a       01000000      DTB
    e8800000    01300000      rootfs.ext2.gz

   partition #2: loaded by partition manager
   running in partition #1

     Src         Dest
     Guest       Guest
    Physical    Physical      Image
    (Flash)      (DDR)
   ---------------------------------------
      n/a       00000000      vmlinux
      n/a       01000000      DTB
      n/a       01300000      rootfs.ext2.gz


