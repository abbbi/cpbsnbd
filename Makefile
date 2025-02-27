all: 
	 gcc -fPIC -shared cpbsnbdkit.c -o pbsplugin.so -lproxmox_backup_qemu

clean:
	rm -f *.o *.so
