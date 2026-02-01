# Hytale UDP Proxy
A simple UDP proxy made in C++ for Windows, made because of Hytale's client connection issues for dedicated servers (only on Windows as far as I know), it could work with others applications.

Notes:
* This is a bandaid fix for connections issues.

Compile it using whichever compiler you want. But make sure to have the next libraries installed:
* wxWidgets 3.2.9
* SQLite3 3.51.2

```bashc
g++ main.cpp ipv4_proxy.cpp ipv6_proxy.cpp proxy_common.cpp -o <executable_name>.exe -I"%WXWIN%\include" -I"%WXWIN%\lib\gcc_dll\mswud" -L"%WXWIN%\lib\gcc_dll" -lws2_32 -D__WXMSW__ -DUNICODE -DWXUSINGDLL -lwxmsw32ud_core -lwxbase32u
```

## How to use?
When starting it will ask for a socket in which the proxy will use, can leave it empty and will use Hytale's default port.
```bashc
Enter Proxy Socket (9520):
```
After it, enter the server address which is mandatory.
```bashc
Enter server address: <server address>
```
It supports the format `<address>:<port>` or just `<address>` and will connect with default 9520 port, available addresses formats are:
* A domain, for example: `example.com`.
* IPv4, for example: `10.0.0.1`
* IPv6, can be defined but it won't work, it should be between square brackets like: `[fe80::1]`

When it shows:
```bashc
Waiting for a client connection...
```
Connect to it using `127.0.0.1` or `127.0.0.1:<port>` if you chose other port than 9520 as Proxy Port.

<img width="429" height="250" alt="image" src="https://github.com/user-attachments/assets/cb1abd11-4adc-4317-b10e-7be39af6c948" />

You can stop it by pressing Ctrl+C once to stop.
## Why did I make this?
This was made because some of my friends had issues with Hytale multiplayer, in which they couldn't connect to a dedicated servers for them to play together. **Note: Affected devices were running Windows**.
### Why does Hytale have issues with servers connection (non-local or invitation code)?
I have not clue, but what I can say is what I have seen. Using Wireshark and Windows 11 Resource Monitor, I can see Hytale clients does creates UDP connections to send packets and listen for incomming ones using Wireshark and Resource Monitor. Just to see if there was atleast some upload, from Wireshark I saw uploads being made from the UDP connections created by the client, this means the client is indeed doing his job of trying to communicate with a target server. 


My next step was then watch for any UDP connection on an external server and see if it matched the clients' public IP and port used to connect, from my testing on affected devices, I didn't see any packets arriving on the server, which means UDP packets were being lost. It couldn't be network dependant, as I have move affected Windows devices to others networks with the issue persisting, but when booting Linux from a USB, Hytale could connect to any dedicated servers without issue.


Tl;dr: The client works as it's supposed to be, in my honest opinion, is Windows being Windows in some devices.
## Why didn't I make it work on Linux?
My friends that plays on Linux didn't have any issues with connecting to dedicated servers, neither when I switched to it on affected Windows devices.
