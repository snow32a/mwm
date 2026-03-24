I = -I/usr/include/freetype2 -I/usr/include/libpng16 -DWITH_GZFILEOP -I/usr/include/harfbuzz -I/usr/include/glib-2.0 -I/usr/lib64/glib-2.0/include -I/usr/include/sysprof-6 -pthread
build/mwm:	src/main.c
	gcc */*.c -g $(I) -lX11 -lfreetype -O3 -o build/mwm
clean:
	rm ./build/*.*
