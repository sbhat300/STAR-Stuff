
import os
import select
import struct
from typing import Callable

C_FIFO_LOCATION = '/tmp/cfifo'
PYTHON_FIFO_LOCATION = '/tmp/pyfifo'

READ_SIZE = 2048

buffer = b''
message_len = 0 
message = b'' 

def send_message(msg: bytes):
    '''
    Sends message on the FIFO with payload size header
    '''
    if(len(msg) > 65535):
        print("Message length is greater than 65535)")
        return
    #Big endian order for payload size
    message_len = struct.pack('!H', len(msg)) 
    written = os.write(py_fifo, message_len + msg)
    print(f'{written} bytes sent out of {len(msg) + 2} (+2)')

def setup():
    '''
    Creates FIFOs and resets buffers
    MUST be called once at the start of the program
    '''
    global C_FIFO_LOCATION, PYTHON_FIFO_LOCATION, c_fifo, py_fifo
    #Create fifos if they don't exist
    print('CREATING FIFOS')
    if not os.path.exists(C_FIFO_LOCATION):
        os.mkfifo(C_FIFO_LOCATION)
    if not os.path.exists(PYTHON_FIFO_LOCATION):
        os.mkfifo(PYTHON_FIFO_LOCATION)

    print('OPENING C FIFO')
    c_fifo = os.open(C_FIFO_LOCATION, os.O_RDONLY | os.O_NONBLOCK)
    print('OPENING PY FIFO')
    py_fifo = os.open(PYTHON_FIFO_LOCATION, os.O_WRONLY) 

def check_for_messages(callback: Callable[[int, bytes], None]):
    """
    Chceks the FIFO for any new data, calls callback if a complete message is found
    Put this in a loop
    Protocol: [2-byte payload length][payload]
    """
    global buffer, c_fifo, message_len, message
    
    #Check for updatees on the FIFO
    read_ready, _, _ = select.select([c_fifo], [], [], 0)
    
    if not read_ready:
        return 

    try:
        data = os.read(c_fifo, READ_SIZE)
    except OSError as e:
        print(f"Pipe read error: {e}")
        return

    if not data:
        #Reset state when other end of pipee is closed
        print('Writer closed pipe, reopening...')
        os.close(c_fifo)
        c_fifo = os.open(C_FIFO_LOCATION, os.O_RDONLY | os.O_NONBLOCK)
        buffer = b''
        message = b''
        message_len = 0
        return

    #Add newly read data to buffer
    buffer += data
    print(f'Message in buffer of length {len(data)}')

    #Process data in buffer while there is still stuff in it
    while True:
        if message_len == 0:
            #Get new message length from header
            if len(buffer) < 2:
                break 
            
            message_len = struct.unpack('!H', buffer[:2])[0] 
            buffer = buffer[2:] 
            message = b''     
            
        bytes_needed = message_len - len(message)
        bytes_available = len(buffer)
        
        #Check if we have enough bytes to from a message
        if bytes_available >= bytes_needed:
            message += buffer[:bytes_needed]
            callback(len(message), message)
            buffer = buffer[bytes_needed:]
            message_len = 0
            message = b''
        else:
            #Accumulate in message if not enough bytes in buffer
            message += buffer
            buffer = b''
            break
            
def cleanup():
    '''
    Closes FIFOs
    '''
    print('Closing fifos')
    os.close(py_fifo)
    os.close(c_fifo)