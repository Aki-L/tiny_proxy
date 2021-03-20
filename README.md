# Tiny_proxy

* tiny_proxy is a small HTTP/HTTPS proxy, which implements all the basic features of an HTTP/HTTPS proxy, and also some new features in HTTP/1.1 protocol like chunked transfer encoding/decoding and default persistent connection.

* Files in this distribution:

  ​		README: this

  ​		Makefile: )

  ​		csapp.*: csapp library, source for rio package

  ​		proxy.*: source file

  ​		tools/: selfmade utils for source file

  ​			-- bqueue: blocking queue

  ​			-- concurrent_hashmap

  ​			-- parse_requestline: requestline parser

  ​			-- threadpool

  ​			-- Makefile

* To build: just do make
