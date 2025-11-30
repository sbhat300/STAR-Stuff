#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <arpa/inet.h> 

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
    int messageLen; //Total eventual size of message in bytes
    char* message;
    int messageCap;
    int messagePos; //Current amount of bytes in the message
};

struct bufferStruct buffer;
struct messageStruct message;

int cFIFO, pyFIFO;

/**
 * Closes FIFOs and frees buffers
 */
void cleanup(void)
{
    close(pyFIFO);
    close(cFIFO);

    free(buffer.buffer);
    free(message.message);
}

/**
 * Sends message on the FIFO with payload size header
 */
int sendMessage(const unsigned char* msg, int len)
{
    if(len > UINT16_MAX)
    {
        printf("Message length exceeds 65535 bytes");
        return 1;
    }
    //Big endian order for payload size
    uint16_t numBytes = htons((uint16_t)len);

    //Write header and data to fifo
    struct iovec iov[2];
    iov[0].iov_base = (void*)&numBytes; 
    iov[0].iov_len = 2;
    iov[1].iov_base = (void*)msg;
    iov[1].iov_len = len;
    ssize_t bytesWritten = writev(cFIFO, iov, 2);
    if(bytesWritten == -1)
    {
        printf("Could not write to FIFO file\n");
        return 1;
    }
    printf("%zd bytes sent out of %d (+2)\n", bytesWritten, len + 2);
    return 0;
}

/**
 * Creates FIFOs and resets buffers
 * MUST be called once at the start of the program
 */
int setup(void)
{
    //Reset buffer andd message
    buffer.buffer = NULL;
    buffer.bufferCap = 0;
    buffer.bufferLen = 0;

    message.message = NULL;
    message.messageCap = 0;
    message.messageLen = 0;
    message.messagePos = 0;

    //Create fifos if they don't exist
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

    //Open in this order to prevent deadlock
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

    atexit(cleanup);
    return 0;
}

/**
 * Checks the FIFO for any new data, calls callback if a complete message is found
 * Put this in a loop
 * Protocol: [2-byte payload length][payload]
 */

int checkForMessages(void (*callback)(int, const char*))
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(pyFIFO, &readfds);
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    //Check for updates on the FIFO
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

            //Process buffer while there is still stuff in it
            while(1)
            {
                if(message.messageLen == 0)
                {
                    //Get new size from header if possible
                    if(buffer.bufferLen < 2) break;

                    uint16_t messageLen;
                    memcpy(&messageLen, buffer.buffer, 2);

                    message.messageLen = (int)ntohs(messageLen);
                    message.messagePos = 0;
                    
                    buffer.bufferLen -= 2;
                    memmove(buffer.buffer, buffer.buffer + 2, buffer.bufferLen);
                }

                int bytesNeeded = message.messageLen - message.messagePos;
                int bytesAvailable = buffer.bufferLen;

                //Check if we have enough bytes to form the full message
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
                    callback(message.messageLen, message.message);
                    message.messageLen = 0;
                    message.messagePos = 0;

                    buffer.bufferLen -= bytesNeeded;
                    memmove(buffer.buffer, buffer.buffer + bytesNeeded, buffer.bufferLen);
                    
                }
                else
                {
                    //Accumulate the message if not enough bytes in buffer
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

            //Shrink buffeer if necessary
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

            //Shrink message if necessary
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
            //Reset state when other end of fifo is closed
            printf("Writer closed pipe, reopening...\n");
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