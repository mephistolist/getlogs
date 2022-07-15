/* This is part of an another project to find the location of all access logs from Apache and/or Nginx.
   You may compile with the following:
   
   gcc -march=native -pipe -Ofast -fno-defer-pop -s getlogs.c -o getlogs -lpthread -s

   Doing the same actions in bash rendered the following:

   $ time lsof 2>&1 | awk '/apache2|httpd|nginx/ && /access/ && /log/ { print $9 }' | sort -u
   /var/log/apache2/access.log
   /var/log/apache2/other_vhosts_access.log
   /var/log/apache2/xplico_access.log

   real	0.30s
   user	0.12s
   sys	0.09s
   cpu	67%

   real	0.30s
   user	0.02s
   sys	0.00s
   cpu	7%

   real	0.30s
   user	0.00s
   sys	0.00s
   cpu	0%

Its clear the C implementation is much faster on the same machine:

   $ time ./getlogs
   /var/log/apache2/other_vhosts_access.log
   /var/log/apache2/xplico_access.log
   /var/log/apache2/access.log

   real	0.00s
   user	0.00s
   sys	0.00s
   cpu	76%

*/

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>

void symfind(char * b){

   struct stat sb;
   char *linkname;
   ssize_t r;
   char *match = "access";
   //char line[100];

   if (lstat(b, &sb) == -1) {
      perror("lstat");
      exit(EXIT_FAILURE);
   }

   linkname = malloc(sb.st_size + 1);
     if (linkname == NULL) {
       fprintf(stderr, "insufficient memory\n");
       exit(EXIT_FAILURE);
     }

   r = readlink(b, linkname, sb.st_size + 1);

   if (r == -1) {
     perror("readlink");
     exit(EXIT_FAILURE);
   }

   if (r > sb.st_size) {
     fprintf(stderr, "symlink increased in size "
         "between lstat() and readlink()\n");
          exit(EXIT_FAILURE);
     }

    linkname[r] = '\0';

    if (strstr(linkname, match) != NULL) {
        printf("%s\n", linkname);
    }

    //printf("%s\n", linkname);

    free(linkname);

}

void findpid(char *filename){

    char pro[60] = "/proc/";
    char fd[10] = "/fd/";
    char pid[10];
    char dir[60];

    FILE *file;
    file = fopen(filename,"r");
    while(fgets(pid, sizeof(pid), file)!=NULL)
    strtok(pid, "\n"); // Strip newline.
    strncat( pro, pid, 10 );
    strncat( pro, fd, 10 );
    strncat( dir, pro, 10);
    fclose(file);

    DIR *dp;
    struct dirent *ep;

    dp = opendir (pro);
    if (dp != NULL)
      {
      while((ep = readdir(dp)) != NULL) {
          if(strcmp(".", ep->d_name) && strcmp("..", ep->d_name)) {
            if(snprintf(dir, sizeof(dir), "%s%s", pro, ep->d_name) >=
                sizeof(dir)) {
              fprintf(stderr, "Processing for %s%s skipped (buffer overflow)\n",
                  pro, ep->d_name);
              continue;
            }
            symfind(dir);
          }
        }
      (void) closedir (dp);
      }
 else
     {
       perror ("Couldn't open the directory");
     }
}

int main(void)
{

    FILE * fp;

    if ((fp = fopen("/var/run/apache2/apache2.pid", "r")) != NULL) {
        char filename[] = "/var/run/apache2/apache2.pid";
	findpid(filename);
        fclose(fp);
    }

    else if ((fp = fopen("/var/run/httpd/httpd.pid", "r")) != NULL) {
        char filename[] = "/var/run/httpd/httpd.pid";
        findpid(filename);
        fclose(fp);
    }
    else if ((fp = fopen("/var/run/nginx.pid", "r")) != NULL) {
        char filename[] = "/var/run/nginx.pid";
        findpid(filename);
        fclose(fp);
    }
    else {
        printf("Not finding any of the usual suspects...\nBetter try manually: netstat -naltp | awk '/:80|:443|:8080/ && /LISTEN/'\n");
    }

    return (0);

}
  
