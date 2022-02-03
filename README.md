# Bluetooth helper

https://github.com/ThomasHabets/bthelper

This is not an official Google product.

## What is it

SSH Helper program so that you can SSH over bluetooth. This can be a useful
second way in in case you have a raspberry pi with broken network or firewall
config.

[Related blog posts][blog] that describes a more full setup guide, including
pairing.

## Example usage assuming you've already paired devices

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

## Example for console, not SSH

```
bt-listener -c 5 -e -- getty '{}' --login-options '-h {addr}'
```

```
bt-connecter -t AA:BB:CC:XX:YY:ZZ 5
```

[blog]: https://blog.habets.se/2022/02/SSH-over-Bluetooth-cleanly.html
