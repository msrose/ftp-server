# Simple FTP Server

A minimal, single-threaded FTP server written in C using only Unix socket APIs.

## Features

- Anonymous login only
- Basic FTP commands: USER, PASS, LIST, RETR, CWD, PWD, CDUP, QUIT
- Passive mode data transfers
- Directory navigation
- File downloads

## Building

```bash
make
```

## Usage

```bash
./ftp_server <directory>
```

Example:
```bash
./ftp_server /home/user/files
```

The server will start on port 2121 and serve files from the specified directory.

## Testing

You can test the server using various FTP clients:

```bash
# Using netcat to test basic connectivity
echo "QUIT" | nc localhost 2121

# Using ftp command (interactive)
ftp localhost 2121

# Using curl (may not work with all curl versions)
curl ftp://localhost:2121/
```

## Supported Commands

- `USER <username>` - Username (any value accepted)
- `PASS <password>` - Password (any value accepted)
- `LIST` - List directory contents
- `RETR <filename>` - Download a file
- `CWD <directory>` - Change working directory
- `PWD` - Print working directory
- `CDUP` - Change to parent directory
- `QUIT` - Disconnect

## Limitations

- Single-threaded (one client at a time)
- No file upload support
- No authentication
- Basic error handling
- Fixed data port (2120)
- Uses non-standard ports (2121/2120) to avoid requiring root privileges

## Cleanup

```bash
make clean
``` 