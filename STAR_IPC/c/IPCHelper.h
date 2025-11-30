
void cleanup(void);
int sendMessage(const unsigned char* msg, int len);
int setup(void);
int checkForMessages(void (*callback)(int, const char*));