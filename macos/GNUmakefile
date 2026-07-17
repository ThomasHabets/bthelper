BINARY = bt-connecter
SOURCES = bt-connecter.swift
SWIFTFLAGS = -framework IOBluetooth -framework Foundation

$(BINARY): $(SOURCES)
	swiftc $(SOURCES) $(SWIFTFLAGS) -o $(BINARY)

clean:
	rm -f $(BINARY)

.PHONY: clean
