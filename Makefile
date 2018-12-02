all : http_proxy

http_proxy: http_proxy.c
	gcc -o http_proxy http_proxy.c -pthread

clean:
	rm -f http_proxy
	rm -f *.o


