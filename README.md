This repo contains the port of minix to xen which I did in my final year of Uni.

The port is incomplete. It's missing a block device driver and network driver.

The code here may not even compile. I rebased onto the R3.1.1 branch so that it is clearer which changes I made. It's still not 100%, as in my 22 y.o. wisdom, I decided to reindent the code before working on it. My original code, from before the rebase is in minix-src-2006.tar.bz2. This is known to compile.

report.pdf has information about the port and how to build. It can only be compiled with the amsterdam compiler kit, and therefore in minix(at least it was the case in 2006 that ACK wasn't available on any other platform). A VM can be used to make this possible.

The minix kernel doesn't compile to an ELF or any other recognisable format. It's a custom binary format, and therefore you'll need the minixbuilder in Xen to build it. Again, this is based on APIs from 2006, so it will not work out of the box.

