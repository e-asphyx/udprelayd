# udprelayd
udprelayd's main purpose is tunnelling UDP traffic through number of slow and unstable connections.

## How it works
It appends incremental sequence number for every incoming datagram and sends it through number of sockets to remote node. Latter accepts only first datagram with same sequence number and forwards it to final destination with sequence number stripped.
```
                           Relay 0  ,---------------.
                        ,;========= | seq | payload | ===:.
                        ||          `---------------‘    ||
                        || Relay 1  ,---------------.    ||
Peer 0 <===> Node 0 <===++========= | seq | payload | ===++==> Node 1 <===> Peer 1
                        ||          `---------------‘    ||
                                         ...
                        || Relay N  ,---------------.    ||
                        ':========= | seq | payload | ===:‘
                                    `---------------‘
```

## Usage
```
udprelayd [-d|--detach] [-p|--pidfile pidfile] config
```

## Config file syntax
The file contains keyword-argument pairs, one per line. Lines starting with `#' and empty lines are interpreted as comments. The possible keywords and their meanings are as follows.
* **listen**
  * Bind main socket to this address. Format is `host[:port]`. Use `*' as host to listen on all possible addresses.
* **forward**
  * Forward stripped packets to this address. Format is host:port. At least one of listen and forward addresses must be specified.
* **relay**
  * Format: `relay [local host[:port]] [remote host:port]`. At least one of local and remote addresses must be specified.
* **track**
  * Integer number. Keep sequence numbers of last N datagrams received from remote node to remove duplicates.

### Config file example
```
# Incoming address
listen *:1194

## Destination address
#forward localhost:1195

# For every relay rule forward inbound packet to remote address using specified local address
# Sequence number will be added to every packet
relay local *:1196 remote raspberrypi.local:1196
relay local *:1197 remote raspberrypi.local:1197
relay local *:1198 remote raspberrypi.local:1198

# Keep sequence numbers for last N packets received from relays
track 1024
```

### General notes
udprelayd opens one socket for every relay statement plus one socket for communicating with it's peer. The next rule applies for every relay statement:
* If both local and remote addresses is specified, corresponding socket will be bound to this address and remote address will be used as only destination for this path.
* If only local address is specified, corresponding socket will be bound to this address and every outbound datagram will be send to source address of last datagram received on this socket. Thus udprelayd will drop all outbound datagrams until first datagram will be received on this socket.
* If only remote address is specified, corresponding socket will not be bound to any address and remote address will be used as only destination for this path.

The same rules applies to listen and forward addresses of main socket.
