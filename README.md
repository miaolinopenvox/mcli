mcli -- interactive client for malamute enterprise message broker
=================================================================

 

**mcli** is a **malamute** client, connect to **malamute** through 2 connection,
one as producer, the other one as consumer.

producer can send message to stream or to any client, consumer can listen to
many streams or subjects.

 

**mcli** should be useful when debug **malamute** application.

 

It is also a good demo on how to use **linux raw socket** or file handle with
**zmq** socket or **malamute** client together through zloop reactor.

 

This is my first project use malamute and zeromq. if you have any suggestion,
let me know.

 

**mcli** is written in C++, use STL, readline, cJSON and of course **malamute**.

 

build mcli:
-----------

./make

 

clean mcli:
-----------

make clean

or

make distclean

 

run mcli:
---------

./mcli

 

Copyright:
----------

MPL

 

Author
------

Miao Lin \<lin.miao\_at\_openvox.cn\>

 
