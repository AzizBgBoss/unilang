# Networking examples

These examples require a host that installs the networking callbacks. The SDL
desktop host provides nonblocking TCP, UDP, HTTP, and UDP-broadcast discovery.
`tools/headless.c` installs a deterministic in-memory test host; its fixed
HTTP responses and loopback peers are fixtures, not Internet access.

Compile the examples from the repository root:

```sh
python compiler.py examples/network/http_post.c -o /tmp/http_post.ulc
python compiler.py examples/network/tcp_framed.c -D HOST_MODE=1 -o /tmp/tcp_host.ulc
python compiler.py examples/network/tcp_framed.c -D HOST_MODE=0 -o /tmp/tcp_join.ulc
python compiler.py examples/network/udp_ports.c -o /tmp/udp.ulc
python compiler.py examples/network/pong_multiplayer.c -D PONG_HOST=1 -o /tmp/pong_host.ulc
python compiler.py examples/network/pong_multiplayer.c -D PONG_HOST=0 -o /tmp/pong_join.ulc
```

- `http_post.c` sends a POST with two headers and polls for the status and
  response body. Point its numeric host and port at a local plain-HTTP test
  endpoint.
- `tcp_framed.c` demonstrates host/join TCP and framed message polling. Run
  the host before the join client.
- `udp_ports.c` demonstrates independent local and remote ports. Start one
  copy with `-D LOCAL_PORT=9000 -D REMOTE_PORT=9001`, and the other with those
  values reversed.
- `pong_multiplayer.c` supports single player, hosting, and joining. In the
  join list, Up/Down selects a discovered game or the manual IP entry. The IP
  editor changes the selected octet with Up/Down and moves between octets with
  Left/Right. A or Start confirms; B or Select goes back. The host runs the
  match and sends its state to one joiner. On desktop, C maps to A, V to B,
  Enter to Start, and Backspace to Select. Run the SDL host with `--net` to
  enable networking.

The test smoke source is compiled by `python tools/regress.py`. Run it through
the headless host after building that host with your C compiler:

```sh
cc -O2 -std=c11 -I C-interpreter tools/headless.c -o tools/headless
python compiler.py examples/network/headless_smoke.c -o /tmp/network_smoke.ulc
tools/headless /tmp/network_smoke.ulc -frames 1 -quiet
```

The deterministic headless fixture never opens sockets. See the [host
integration guide](../../docs/vm/host-integration.md#optional-networking) for
host setup and security policy details.

Run the VM opcode, deterministic fake-backend, real desktop-socket loopback
(including a local HTTP fixture), compiler, and headless smoke checks together
on a POSIX development host with:

```sh
sh tools/test_network.sh
```
