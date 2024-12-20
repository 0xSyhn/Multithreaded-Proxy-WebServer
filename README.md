# Multithreaded-Proxy-WebServer

Implemented a Multithreaded Proxy Web Server using LRU Cache to concurrently handle multiple requests from the web client and cache the response  which came back from the Web Server so that the next time when you try to request the same data, the system wont have to send request to the Web Server instead, you get the data from Proxy Server.

Proxy Server was implemented using LRU Cache to optimize performance by storing frequently accessed data in memory while ensuring that the cache size remains manageable by discarding the least recently accessed items.

###Benefits of using LRU Cache:
-> Imporved Performance
-> Efficient memory utilization

###How Does the Cache Work?
When data is accessed:
-> If it exists in the cache (cache hit), it's moved to the "most recently used" position.
-> If it doesn’t exist (cache miss), the data is fetched, added to the cache, and the least recently used item is evicted   if the cache is full.

###Why did I use C++?
-> I wanted to learn how to build at a low level.
-> I noticed many projects were built in C, so I wanted to try something different.

###Why did i manually set up sockets instead of using available Libraries such as libcurl, Boost.Asio?
-> I wanted to learn about socket programming and understand how networks are established between clients and servers.
-> To gain hands on experience working at low level.