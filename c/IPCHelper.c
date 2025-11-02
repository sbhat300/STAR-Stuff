#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <fcntl.h>

#include "IPCHelper.h"

#define READ_SIZE (2048)
#define MIN_BUFFER_SIZE (8192)

const char* C_FIFO_LOCATION = "/tmp/cfifo";
const char* PY_FIFO_LOCATION = "/tmp/pyfifo";

struct bufferStruct
{
    char* buffer;
    size_t bufferLen;
    size_t bufferCap;
};

struct messageStruct
{
    int messageLen;
    char* message;
    int messageCap;
    int messagePos;
};

struct bufferStruct buffer;
struct messageStruct message;

int cFIFO, pyFIFO;

void closeFIFOs(void)
{
    close(pyFIFO);
    close(cFIFO);

    free(buffer.buffer);
    free(message.message);
}

int sendMessage(const unsigned char* msg, int len)
{
    if(len > 255)
    {
        printf("Message length exceeds 255 bytes");
        return 1;
    }
    unsigned char numBytes = (unsigned char)len;

    struct iovec iov[2];
    iov[0].iov_base = (void*)&numBytes; 
    iov[0].iov_len = 1;
    iov[1].iov_base = (void*)msg;
    iov[1].iov_len = numBytes;
    ssize_t bytesWritten = writev(cFIFO, iov, 2);
    if(bytesWritten == -1)
    {
        printf("Could not write to FIFO file\n");
        return 1;
    }
    printf("%zd bytes sent out of %d (+1)\n", bytesWritten, len + 1);
    return 0;
}

void doSomething(int len, const char* msg)
{
    printf("RECEIVED MESSAGE WITH LENGTH %d:\n", len);
    for(int i = 0; i < len; i++) printf("%02hhx", *(msg + i));
    printf("\n");
}

int setup(void)
{
    buffer.buffer = NULL;
    buffer.bufferCap = 0;
    buffer.bufferLen = 0;

    message.message = NULL;
    message.messageCap = 0;
    message.messageLen = 0;
    message.messagePos = 0;
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

int checkForMessages(void)
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
            printf("Message in buffer of length %zd\n", bytesRead);
            size_t newBufferLen = (size_t)bytesRead + buffer.bufferLen;
            //Grow buffer if necessary
            if(newBufferLen > buffer.bufferCap)
            {
                size_t newBufferCapacity = buffer.bufferCap == 0 ? newBufferLen : buffer.bufferCap * 2;
                if(newBufferCapacity < newBufferLen) newBufferCapacity = newBufferLen;
                char* temp = (char*)realloc(buffer.buffer, newBufferCapacity);
                if(temp == NULL)
                {
                    printf("Buffer grow failed");
                    return 1;
                }
                buffer.buffer = temp;
                buffer.bufferCap = newBufferCapacity;
            } 

            //Add new data to buffer
            memcpy(buffer.buffer + buffer.bufferLen, data, (size_t)bytesRead);
            buffer.bufferLen = newBufferLen;

            while(1)
            {
                if(message.messageLen == 0)
                {
                    if(buffer.bufferLen < 1) break;

                    message.messageLen = (unsigned char)buffer.buffer[0];
                    message.messagePos = 0;
                    
                    buffer.bufferLen--;
                    memmove(buffer.buffer, buffer.buffer + 1, buffer.bufferLen);
                }

                int bytesNeeded = message.messageLen - message.messagePos;
                int bytesAvailable = buffer.bufferLen;

                if(bytesAvailable >= bytesNeeded)
                {
                    if(message.messageCap < message.messageLen)
                    {
                        int newCap = message.messageLen;
                        char* temp = (char*)realloc(message.message, sizeof(char) * newCap);
                        if(temp == NULL)
                        {
                            printf("Message grow failed");
                            return 1;
                        }
                        message.message = temp;
                        message.messageCap = newCap;
                    }

                    memcpy(message.message + message.messagePos, buffer.buffer, bytesNeeded);
                    message.messagePos += bytesNeeded;
                    doSomething(message.messageLen, message.message);
                    message.messageLen = 0;
                    message.messagePos = 0;

                    buffer.bufferLen -= bytesNeeded;
                    memmove(buffer.buffer, buffer.buffer + bytesNeeded, buffer.bufferLen);
                    
                }
                else
                {
                    if(message.messageCap < message.messageLen)
                    {
                        int newCap = message.messageLen;
                        char* temp = (char*)realloc(message.message, sizeof(char) * newCap);
                        if(temp == NULL)
                        {
                            printf("Message grow failed");
                            return 1;
                        }
                        message.message = temp;
                        message.messageCap = newCap;
                    }

                    memcpy(message.message + message.messagePos, buffer.buffer, bytesAvailable);
                    message.messagePos += bytesAvailable;
                    buffer.bufferLen = 0;
                    break; 
                }
            }

            if(buffer.bufferCap > MIN_BUFFER_SIZE && buffer.bufferLen < (buffer.bufferCap >> 2))
            {
                size_t newCapacity = buffer.bufferCap / 2;
                if(newCapacity < buffer.bufferLen) newCapacity = buffer.bufferLen;

                char* temp = realloc(buffer.buffer, newCapacity);
                if(temp != NULL)
                {
                    buffer.buffer = temp;
                    buffer.bufferCap = newCapacity;
                }
                else
                {
                    printf("Buffer shrink failed");
                    return 1;
                }
            }

            if(message.messageCap > MIN_BUFFER_SIZE && message.messagePos < (message.messageCap >> 2))
            {
                size_t newCapacity = message.messageCap / 2;
                if(newCapacity < message.messagePos) newCapacity = message.messagePos;

                char* temp = realloc(message.message, newCapacity);
                if(temp != NULL)
                {
                    message.message = temp;
                    message.messageCap = newCapacity;
                }
                else
                {
                    printf("Buffer shrink failed");
                    return 1;
                }
            }
        }
        else if(bytesRead == 0)
        {
            printf("Writer closed pipe\n");
            free(buffer.buffer);
            buffer.buffer = NULL;
            buffer.bufferLen = 0;
            buffer.bufferCap = 0;

            free(message.message);
            message.message = NULL;
            message.messageCap = 0;
            message.messageLen = 0;
            message.messagePos = 0;

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