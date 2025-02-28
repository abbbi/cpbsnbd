all: 
	 gcc -fPIC -shared cpbsnbdkit.c -o nbdkit-pbs-plugin.so -lproxmox_backup_qemu

clean:
	rm -f *.o *.so
