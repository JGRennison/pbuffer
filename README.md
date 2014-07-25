## pbuffer: Pipe Buffer

**Copy STDIN to STDOUT, storing up to a fixed number of bytes.**  

### Usage:

    pbuffer -m bytes [options]

### Options:
* -m, --max-queue bytes  
  Maximum amount of data to store.  
  Accepts suffixes: k, M, G, T, for powers of 1024.  
  This option is required unless using -h or -V.  
* -r, --read-size bytes  
  Maximum amount of data to read in one go.  
  Accepts suffixes: k, M, G, for multiples of 1024. Default: 64k.  
* -p, --progress  
  Show a progress line on STDERR.  
* -s, --human-readable  
  Show progress sizes in human-readable format (e.g. 1k, 23M).  
* -h, --help  
  Show this help  
* -V, --version  
  Show version information  

### Notes:
* No attempt is made to line-buffer or coalesce the input.
* In the event of a read error or end of input, this will wait until  
  all stored bytes have been output before exiting.

### URLs:
* This project is hosted at https://github.com/JGRennison/pbuffer
* A Ubuntu PPA is currently available at https://launchpad.net/~j-g-rennison/+archive/pbuffer

### License:
GPLv2
