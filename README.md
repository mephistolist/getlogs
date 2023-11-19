# getlogs
Part of a bigger project in C that gets converted to Assembly. 
This part will quickly find the location of any access logs from Apache or Nginx and display them to the user. 
You make build with the following:

$ make

Doing the same actions in bash rendered the following:

root@tiny:/home/ph33r/tito/getlogs# time lsof 2>&1 | awk '/apache2|httpd|nginx/ && /access/ && /log/ { print $9 }' | sort -u
/var/log/apache2/access.log
/var/log/apache2/other_vhosts_access.log

real	0m1.438s 
user	0m0.007s 
sys	0m0.065s

Its clear the C/Assembly implementation is much faster on the same machine:

$ time ./getlog
/var/log/apache2/other_vhosts_access.log
/var/log/apache2/access.log

real	0m0.001s 
user	0m0.001s 
sys	0m0.000s

This may not seem noteworthy until someone is on a server that is almost out of CPU or memory when actions that normally take millisecons can take 30 seconds or greater. 

