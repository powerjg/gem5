DAYSTROM_USER=
DAYSTROM_PATH=/z/www/htdocs/dist/current/test-progs/hello/bin/arm/linux
UPLOAD_LOCATION=$(DAYSTROM_USER)daystrom.gem5.org:$(DAYSTROM_PATH)

all: hello64-static hello64-dynamic hello32-static


upload: hello64-static hello64-dynamic hello32-static
	scp hello64-static hello64-dynamic hello32-static $(UPLOAD_LOCATION)

hello64-static: hello.c dockcross-x64
	./dockcross-x64 bash -c '$$CC hello.c -o hello64-static -static'
	
hello64-dynamic: hello.c dockcross-x64
	./dockcross-x64 bash -c '$$CC hello.c -o hello64-dynamic'

hello32-static: hello.c dockcross-x86
	./dockcross-x86 bash -c '$$CC hello.c -o hello32-static -static'

hello.c:
	cp ../../../src/hello.c .

dockcross-x64:
	docker run --rm dockcross/linux-x64 > ./dockcross-x64
	chmod +x ./dockcross-x64

dockcross-x86:
	docker run --rm dockcross/linux-x86 > ./dockcross-x86
	chmod +x ./dockcross-x86

clean:
	rm -f dockcross-* hello.c hello*
