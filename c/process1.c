#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <fcntl.h>

#include <spicenet/config.h>
#include <spicenet/snp.h>

#define READ_SIZE (2048)
#define MIN_BUFFER_SIZE (8192)

const char* C_FIFO_LOCATION = "/tmp/cfifo";
const char* PY_FIFO_LOCATION = "/tmp/pyfifo";

char* buffer = NULL;
size_t bufferLen = 0, bufferCapacity = 0;
int lastCheck = 0;

int cFIFO, pyFIFO;

void closeFIFOs(void)
{
    close(pyFIFO);
    close(cFIFO);
}

int sendMessage(const char* msg, int len)
{
    ssize_t bytesWritten = write(cFIFO, msg, len);
    if(bytesWritten == -1)
    {
        printf("Could not write to FIFO file\n");
        return 1;
    }
    printf("%zd bytes sent out of %d\n", bytesWritten, len);
    return 0;
}

void doSomething(const char* buffer)
{
    printf("RECEIVED MESSAGE:\n%s\n", buffer);
}

int setup()
{
    printf("CREATING FIFOs\n");
    if(mkfifo(PY_FIFO_LOCATION, 0777) == -1)
    {
        if(errno != EEXIST)
        {
            printf("Could not create PY FIFO file\n");
            return 1;
        }
    }
    if(mkfifo(C_FIFO_LOCATION, 0777) == -1)
    {
        if(errno != EEXIST)
        {
            printf("Could not create C FIFO file\n");
            return 1;
        }
    }

    printf("OPENING PY FIFO\n");
    pyFIFO = open(PY_FIFO_LOCATION, O_RDONLY | O_NONBLOCK);
    if(pyFIFO == -1)
    {
        printf("Could not open PY FIFO file\n");
        return 1;
    }
    printf("OPENING C FIFO\n");
    cFIFO = open(C_FIFO_LOCATION, O_WRONLY);
    if(cFIFO == -1)
    {
        printf("Could not open C FIFO file\n");
        return 1;
    }

    atexit(closeFIFOs);
    return 0;
}

int checkForMessages()
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(pyFIFO, &readfds);
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    //Check for updates on the pipe
    int result = select(pyFIFO + 1, &readfds, NULL, NULL, &tv);
    if(result > 0 && FD_ISSET(pyFIFO, &readfds))
    {
        char data[READ_SIZE];

        ssize_t bytesRead = read(pyFIFO, data, READ_SIZE);
        if(bytesRead > 0)
        {
            printf("Message received of length %zd\n", bytesRead);
            size_t newBufferLen = (size_t)bytesRead + bufferLen;
            //Grow buffer if necessary
            if(newBufferLen > bufferCapacity)
            {
                size_t newBufferCapacity = bufferCapacity == 0 ? newBufferLen : bufferCapacity * 2;
                if(newBufferCapacity < newBufferLen) newBufferCapacity = newBufferLen;
                char* temp = (char*)realloc(buffer, newBufferCapacity);
                if(temp == NULL)
                {
                    printf("Buffer grow failed");
                    return 1;;
                }
                buffer = temp;
                bufferCapacity = newBufferCapacity;
            } 

            //Add new data to buffer
            memcpy(buffer + bufferLen, data, (size_t)bytesRead);
            bufferLen = newBufferLen;

            //Parse messages by null character
            while(bufferLen > 0)
            {
                char* nullPtr = (char*)memchr(buffer, '\0', bufferLen);
                if(nullPtr == NULL) break;
                
                //Do something with the message
                size_t msgLen = nullPtr - buffer;
                char saved = *nullPtr;
                *nullPtr = '\0';
                doSomething(buffer);
                *nullPtr = saved;

                //Move everything back, shrink buffer if necessary
                bufferLen = bufferLen - (msgLen + 1);
                memmove(buffer, nullPtr + 1, bufferLen);
                if(bufferCapacity > MIN_BUFFER_SIZE && bufferLen < (bufferCapacity >> 2))
                {
                    size_t newCapacity = bufferCapacity / 2;
                    if(newCapacity < bufferLen) newCapacity = bufferLen;

                    char* temp = realloc(buffer, newCapacity);
                    if(temp != NULL)
                    {
                        buffer = temp;
                        bufferCapacity = newCapacity;
                    }
                    else
                    {
                        printf("Buffer shrink failed");
                        return 1;
                    }
                }
            }
        }
        else if(bytesRead == 0)
        {
            printf("Writer closed pipe\n");
            free(buffer);
            buffer = NULL;
            bufferLen = 0;
            bufferCapacity = 0;

            close(pyFIFO);
            pyFIFO = open(PY_FIFO_LOCATION, O_RDONLY | O_NONBLOCK);
        }
        else
        {
            printf("Error reading from stream");
            return 1;
        }
    }
    if(result < 0)
    {
        printf("Error with select");
        return 1;
    }
    return 0;
}

int main(int argv, char **argc)
{
    if(setup()) return EXIT_FAILURE;
    sendMessage("HELLO FROM C!!!!!", 18);
    while(1) 
    {
        if(checkForMessages()) return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
