#Makefile for FilterGizmo eCAP REQMOD and RESPMOD adapters
# See here for information about the -fPIC directive:
# http://stackoverflow.com/questions/19364969/relocation-r-x86-64-32-against-rodata-str1-8


all:		# Makes reqmod plugin, then respmod plugin
	g++ -fPIC -DHAVE_CONFIG_H -I../src -I/usr/local/include -O2 -std=c++11 -c src/fg_reqmod.cc -o src/fg_reqmod.o
	g++ -shared -nostdlib /usr/lib/gcc/x86_64-linux-gnu/4.7/../../../x86_64-linux-gnu/crti.o /usr/lib/gcc/x86_64-linux-gnu/4.7/crtbeginS.o src/fg_reqmod.o -L/usr/local/lib /usr/local/lib/libecap.so -L/usr/lib/gcc/x86_64-linux-gnu/4.7 -L/usr/lib/gcc/x86_64-linux-gnu/4.7/../../../x86_64-linux-gnu -L/usr/lib/gcc/x86_64-linux-gnu/4.7/../../../../lib -L/lib/x86_64-linux-gnu -L/lib/../lib -L/usr/lib/gcc/x86_64-linux-gnu/4.7/../../.. -lstdc++ -lm -lc -lgcc_s /usr/lib/gcc/x86_64-linux-gnu/4.7/crtendS.o  /usr/lib/gcc/x86_64-linux-gnu/4.7/../../../x86_64-linux-gnu/crtn.o -fPIC -Wl,-soname -Wl,fg_reqmod.so -o src/fg_reqmod.so

	g++ -fPIC -DHAVE_CONFIG_H -I../src -I/usr/local/include -O2 -std=c++11 -c src/fg_respmod.cc -o src/fg_respmod.o
	g++ -shared -nostdlib /usr/lib/gcc/x86_64-linux-gnu/4.7/../../../x86_64-linux-gnu/crti.o /usr/lib/gcc/x86_64-linux-gnu/4.7/crtbeginS.o src/fg_respmod.o -L/usr/local/lib /usr/local/lib/libecap.so -L/usr/lib/gcc/x86_64-linux-gnu/4.7 -L/usr/lib/gcc/x86_64-linux-gnu/4.7/../../../x86_64-linux-gnu -L/usr/lib/gcc/x86_64-linux-gnu/4.7/../../../../lib -L/lib/x86_64-linux-gnu -L/lib/../lib -L/usr/lib/gcc/x86_64-linux-gnu/4.7/../../.. -lstdc++ -lm -lc -lgcc_s /usr/lib/gcc/x86_64-linux-gnu/4.7/crtendS.o  /usr/lib/gcc/x86_64-linux-gnu/4.7/../../../x86_64-linux-gnu/crtn.o -fPIC -Wl,-soname -Wl,fg_respmod.so -o src/fg_respmod.so

debug:		# Makes reqmod and respmod plugins with DEBUG flag
	g++ -fPIC -DHAVE_CONFIG_H -I../src -I/usr/local/include -DDEBUG -O2 -std=c++11 -c src/fg_reqmod.cc -o src/fg_reqmod.o
	g++ -shared -nostdlib /usr/lib/gcc/x86_64-linux-gnu/4.7/../../../x86_64-linux-gnu/crti.o /usr/lib/gcc/x86_64-linux-gnu/4.7/crtbeginS.o src/fg_reqmod.o -L/usr/local/lib /usr/local/lib/libecap.so -L/usr/lib/gcc/x86_64-linux-gnu/4.7 -L/usr/lib/gcc/x86_64-linux-gnu/4.7/../../../x86_64-linux-gnu -L/usr/lib/gcc/x86_64-linux-gnu/4.7/../../../../lib -L/lib/x86_64-linux-gnu -L/lib/../lib -L/usr/lib/gcc/x86_64-linux-gnu/4.7/../../.. -lstdc++ -lm -lc -lgcc_s /usr/lib/gcc/x86_64-linux-gnu/4.7/crtendS.o  /usr/lib/gcc/x86_64-linux-gnu/4.7/../../../x86_64-linux-gnu/crtn.o -fPIC -Wl,-soname -Wl,fg_reqmod.so -o src/fg_reqmod.so

	g++ -fPIC -DHAVE_CONFIG_H -I../src -I/usr/local/include -DDEBUG -O2 -std=c++11 -c src/fg_respmod.cc -o src/fg_respmod.o
	g++ -shared -nostdlib /usr/lib/gcc/x86_64-linux-gnu/4.7/../../../x86_64-linux-gnu/crti.o /usr/lib/gcc/x86_64-linux-gnu/4.7/crtbeginS.o src/fg_respmod.o -L/usr/local/lib /usr/local/lib/libecap.so -L/usr/lib/gcc/x86_64-linux-gnu/4.7 -L/usr/lib/gcc/x86_64-linux-gnu/4.7/../../../x86_64-linux-gnu -L/usr/lib/gcc/x86_64-linux-gnu/4.7/../../../../lib -L/lib/x86_64-linux-gnu -L/lib/../lib -L/usr/lib/gcc/x86_64-linux-gnu/4.7/../../.. -lstdc++ -lm -lc -lgcc_s /usr/lib/gcc/x86_64-linux-gnu/4.7/crtendS.o  /usr/lib/gcc/x86_64-linux-gnu/4.7/../../../x86_64-linux-gnu/crtn.o -fPIC -Wl,-soname -Wl,fg_respmod.so -o src/fg_respmod.so

socket:          # Makes reqmod and respmod plugins with DEBUG and SOCKET flags
	g++ -fPIC -DHAVE_CONFIG_H -I../src -I/usr/local/include -DDEBUG -DSOCKET -O2 -std=c++11 -c src/fg_reqmod.cc -o src/fg_reqmod.o
	g++ -shared -nostdlib /usr/lib/gcc/x86_64-linux-gnu/4.7/../../../x86_64-linux-gnu/crti.o /usr/lib/gcc/x86_64-linux-gnu/4.7/crtbeginS.o src/fg_reqmod.o -L/usr/local/lib /usr/local/lib/libecap.so -L/usr/lib/gcc/x86_64-linux-gnu/4.7 -L/usr/lib/gcc/x86_64-linux-gnu/4.7/../../../x86_64-linux-gnu -L/usr/lib/gcc/x86_64-linux-gnu/4.7/../../../../lib -L/lib/x86_64-linux-gnu -L/lib/../lib -L/usr/lib/gcc/x86_64-linux-gnu/4.7/../../.. -lstdc++ -lm -lc -lgcc_s /usr/lib/gcc/x86_64-linux-gnu/4.7/crtendS.o  /usr/lib/gcc/x86_64-linux-gnu/4.7/../../../x86_64-linux-gnu/crtn.o -fPIC -Wl,-soname -Wl,fg_reqmod.so -o src/fg_reqmod.so

	g++ -fPIC -DHAVE_CONFIG_H -I../src -I/usr/local/include -DDEBUG -DSOCKET -O2 -std=c++11 -c src/fg_respmod.cc -o src/fg_respmod.o
	g++ -shared -nostdlib /usr/lib/gcc/x86_64-linux-gnu/4.7/../../../x86_64-linux-gnu/crti.o /usr/lib/gcc/x86_64-linux-gnu/4.7/crtbeginS.o src/fg_respmod.o -L/usr/local/lib /usr/local/lib/libecap.so -L/usr/lib/gcc/x86_64-linux-gnu/4.7 -L/usr/lib/gcc/x86_64-linux-gnu/4.7/../../../x86_64-linux-gnu -L/usr/lib/gcc/x86_64-linux-gnu/4.7/../../../../lib -L/lib/x86_64-linux-gnu -L/lib/../lib -L/usr/lib/gcc/x86_64-linux-gnu/4.7/../../.. -lstdc++ -lm -lc -lgcc_s /usr/lib/gcc/x86_64-linux-gnu/4.7/crtendS.o  /usr/lib/gcc/x86_64-linux-gnu/4.7/../../../x86_64-linux-gnu/crtn.o -fPIC -Wl,-soname -Wl,fg_respmod.so -o src/fg_respmod.so

clean:		# Deletes the build output objects and shared objects
	rm src/*.o ; rm src/*.so

uninstall:	# Deletes the adapters from the installation location
	rm /usr/local/lib/fg_reqmod.so ; rm /usr/local/lib/fg_respmod.so

install:	# Installs the adapters to the expected location
	cp src/fg_reqmod.so /usr/local/lib ; cp src/fg_respmod.so /usr/local/lib
