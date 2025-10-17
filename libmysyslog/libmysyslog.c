#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "libmysyslog.h"
int mysyslog(const char* msg, int level, int driver, int format, const char* path) {
 FILE *file;
 file = fopen(path, "a");
 if (file == NULL) {
 return -1;
 }
 time_t now;
 time(&now);
 char *timestamp = ctime(&now);
 timestamp[strlen(timestamp) - 1] = '\0';
 const char *level_str;
 switch (level) {
 case DEBUG: level_str = "DEBUG"; break;
 case INFO: level_str = "INFO"; break;
 case WARN: level_str = "WARN"; break;
 case ERROR: level_str = "ERROR"; break;
 case CRITICAL: level_str = "CRITICAL"; break;
 default: level_str = "UNKNOWN"; break;
 }
 if (format == 0) {
 fprintf(file, "%s [%s] %s\n", timestamp, level_str, driver, msg);
 }
 fclose(file);
 return 0;
}
