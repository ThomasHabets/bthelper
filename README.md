# SSH Bluetooth helper

https://github.com/ThomasHabets/bthelper

This is not an official Google product.

## What is it

SSH Helper program so that you can SSH over bluetooth. This can be a useful
second way in in case you have a raspberry pi with broken network or firewall
config.

Related blog posts:
* [Raspberry pi bluetooth console](https://blog.habets.se/2022/01/Raspberry-Pi-Bluetooth-console.html)
* [SSH over bluetooth](https://blog.habets.se/2022/01/SSH-over-Bluetooth.html)

## Example usage

### On the server

```
rfcomm watch hci0 2 socat TCP:127.0.0.1:22 file:/proc/self/fd/6,b115200,raw,echo=0
```

### On the client

```
ssh -oProxyCommand="sshbthepler AA:BB:CC:XX:YY:ZZ 2" myhostname-console
```

Or in `~/.ssh/config`:

```
Host myhostname-console
    ProxyCommand sshbthelper AA:BB:CC:XX:YY:ZZ 2
```

And then just `ssh myhostname-console`.
