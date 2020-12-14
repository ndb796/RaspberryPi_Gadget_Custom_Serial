int rawserial_open(int max, int vid, int pid, int usage_page, int usage);
int rawserial_recv(int num, void *buf, int len, int timeout);
int rawserial_send(int num, void *buf, int len, int timeout);
void rawserial_close(int num);
