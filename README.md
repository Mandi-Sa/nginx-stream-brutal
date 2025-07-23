
# Brutal Nginx

 Using [TCP Brutal](https://github.com/apernet/tcp-brutal) congestion control algorithm in NGINX 

# NGINX Configuration

Add this to `/etc/nginx/nginx.conf`

```
load_module /path/to/ngx_stream_tcp_brutal_module.so;
```


```
stream {
	map $ssl_preread_server_name $backend_addr {
		example.com backend1:8443;
		default backend2:8443;
	}

	server {
		listen 443;
		ssl_preread on;
		proxy_pass $backend_addr;

		# Enable tcp brutal
		tcp_brutal on;
		# Send rate in bytes per second
		tcp_brutal_rate 1048576;
		# CWND gain in tenths (10=1.0)
		tcp_brutal_cwnd_gain 15;
    }
}
```

# Build

1. Start developement container (Optional)

```
docker run -it -v ./:/data -p 8080:8080 debian:latest /bin/bash
```

2. Install packages

```
apt update
apt install build-essential nginx unzip git wget curl libpcre3-dev zlib1g-dev
```

3. Copy moudle source code

```
mkdir ~/nginx-stream-brutal
cd ~/nginx-stream-brutal
cp /data/config .
cp /data/ngx_stream_tcp_brutal_module.c .
```

4. Downlaod NGINX source code

```
cd ~
wget http://nginx.org/download/nginx-1.28.0.zip
tar -xf nginx-1.28.0.tar.gz
cd ~/nginx-1.28.0
```

5. Compile
```
./configure --with-compat --add-dynamic-module=/root/nginx-stream-brutal
make modules
```

6. Compiled binary is in `./objs/ngx_stream_tcp_brutal_module.so`

# Credit

- [brutal-nginx](https://github.com/sduoduo233/brutal-nginx): upstream
