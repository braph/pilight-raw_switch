MODULE_NAME = raw_switch

build:
	cd pilight; cmake .
	gcc -fPIC -shared $(MODULE_NAME).c -iquote pilight/libs/pilight/protocols/433.92/ -iquote pilight/inc -o $(MODULE_NAME).so -DMODULE=1

install:
	mkdir -p /usr/local/lib/pilight/protocols
	install -m 664 $(MODULE_NAME).so /usr/local/lib/pilight/protocols/$(MODULE_NAME).so

clean:
	rm -f $(MODULE_NAME).so
