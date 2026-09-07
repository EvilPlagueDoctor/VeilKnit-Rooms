# Linux Rooms

Install on Ubuntu/Zorin:

```bash
sudo apt install build-essential cmake pkg-config libssl-dev libgtk-3-dev
./Linux/build_all.sh
```

The console build can be produced without GTK using `./Linux/build_console.sh`. Start the VeilKnit daemon for the same Linux user before starting Rooms. On first connection, approve **VeilKnit Rooms** from the daemon Applications page or console.

The Linux implementation uses OpenSSL AES-256-GCM and OpenSSL's CSPRNG; it does not use the earlier portable test cipher.
