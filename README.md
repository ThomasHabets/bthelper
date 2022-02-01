# Bluetooth helper

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
bt-listener -t localhost:22 -c 2
```

### On the client

```
ssh -oProxyCommand="bt-connecter AA:BB:CC:XX:YY:ZZ 2" myhostname-console
```

Or in `~/.ssh/config`:

```
Host myhostname-console
    ProxyCommand bt-connecter AA:BB:CC:XX:YY:ZZ 2
```

And then just `ssh myhostname-console`.