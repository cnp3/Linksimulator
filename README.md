# LINGI1341-linksim
A link simulator for the first networking project (LINGI1341)

This program will proxy UDP traffic (datagrams of max 528 bytes), between
two hosts, simulating the behavior of a lossy link.

Using it is a simple as choosing two UDP ports, one for the proxy, and one for the receiver of the protocol.

Client machine:

```bash
./sender server_address proxy_port
```

Server machine:
```bash
./link_sim -p proxy_port -P server_port &
./receiver :: server_port
```

You can control the direction (i.e. forward, reverse or both ways) of the
traffic which is affected by the program.

Use this to test your programs in the events of losses, delays, truncation, ...

Feel free to hack it and submit pull request for bug fixes, ...
